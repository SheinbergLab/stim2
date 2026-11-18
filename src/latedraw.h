/*
 * latedraw.h -- "draw late" scheduler for continuous dynamic displays
 *
 * Normally, once waitForSwap() returns for frame N, the render thread steps
 * and draws frame N+1 within tens of microseconds and then blocks in the
 * swap wait for the remaining ~7 ms of an 8.3 ms period.  Every command or
 * datapoint arriving during that wait is applied to the display one frame
 * later than it could have been.  With late-draw enabled, the step+draw of
 * frame N+1 is deferred until
 *
 *     target = lastFlip + period - effective_budget
 *
 * and until then the main loop keeps running normally (commands answered,
 * datapoints applied) without drawing.  The budget is the time the
 * step+draw+submit AND the GPU's tail after the submit need before the
 * vblank.  The user's value is a floor.  The effective budget is
 *
 *     max(floor, work + margin), at most period - 1 ms
 *
 * where work is what frames on this machine, with this scene, have lately
 * taken (draw + GPU tail, measured every frame) and margin is the time the
 * swap needs between the GPU finishing and the vblank.  So the budget finds
 * its own level on each system: nobody tunes a floor per rig, and a floor
 * that is lower than the machine can do costs nothing.
 *
 * It was not always so.  The first rule raised the budget by half (or to
 * twice the work) on any late frame and took 5 s to give back each 0.25 ms.
 * On a rig whose frames needed 4.3 ms that held a 3 ms floor between 5 and
 * 7 ms for whole sessions: the budget sank until a frame missed near 3.9 ms,
 * jumped to the 7.33 ms cap and spent a minute coming down, over and over
 * (docs/late-draw.md, "How the rule was arrived at").
 *
 * Two costs the CPU submit time cannot see are handled separately:
 *
 *   - GPU tail.  After glfwSwapBuffers() returns, the GPU may still be
 *     rendering; the flip waits for it.  The integration measures that tail
 *     with a GL fence (gpu_ms) and the raise rule uses draw + gpu, so a
 *     scene that is cheap to submit but slow to render still widens the
 *     budget by the right amount.
 *   - Warm-up.  The first frame the scheduler governs after a swap it did
 *     not schedule -- the static redraw that ends an idle gap between
 *     trials, a command's explicit redraw, the first frame after enable --
 *     is a fresh scene: first use of programs and buffers, caches cold.
 *     Measured on a 120 Hz rig, such a frame slipped a vblank with a 3 ms
 *     floor although the CPU draw took under 1 ms.  So that frame is not
 *     deferred at all: it is drawn at once, with the whole period in front
 *     of it, and a slip there is counted (warmups / warmup_late) but does
 *     not raise the budget, because no budget would have helped.
 *
 * Everything here is pure arithmetic on a plain struct that lives on the
 * render thread (no locks, no GL, no Tcl), so it can be unit-checked
 * without a display (tests/latedraw_check.cpp).  The integration (clock
 * reads, swap-sync queries, the GL fence, the Tcl command) is in
 * stim2.cpp / tclproc.c.
 */
#ifndef LATEDRAW_H
#define LATEDRAW_H

#ifdef __cplusplus
extern "C" {
#endif

#define LATEDRAW_DRAW_RING 256      /* draw/gpu-time samples kept for stats */
#define LATEDRAW_FLIP_RING 16       /* flip intervals kept for the period */
#define LATEDRAW_MIN_SAMPLES 32     /* frames measured before work is believed */
#define LATEDRAW_WORK_RANK 5        /* work = the 5th largest of the ring (~98%) */
#define LATEDRAW_MARGIN_MS 1.0      /* GPU done to vblank: the starting margin */
#define LATEDRAW_MARGIN_STEP_MS 0.25/* added when a frame within plan is late */
#define LATEDRAW_EXCESS_MS 0.75     /* work this far over plan: not the margin's */
#define LATEDRAW_EXCESS_RUN 30      /* two such within this many frames: a step */
#define LATEDRAW_ENV_DECAY_MS 0.25  /* the work envelope gives back this much */
#define LATEDRAW_ENV_DECAY_FRAMES 600 /* ... per this many frames (5 s at 120 Hz) */
#define LATEDRAW_CAP_MARGIN_MS 1.0  /* effective <= period - this */
#define LATEDRAW_LATE_INTERVAL 1.5  /* flip interval > this * period = late */
#define LATEDRAW_BLAMELESS_SLACK_MS 2.5  /* a late frame that was ready this
                                           long before its refresh was not
                                           late for want of budget */

typedef struct latedraw_state_s {
  int enabled;
  double floor_ms;                  /* the user's budget */
  double effective_ms;              /* current budget (>= floor, <= cap) */
  double env_ms;                    /* work envelope: follows the ring's high
                                       quantile up at once, down slowly */
  double margin_ms;                 /* GPU done to vblank; only grows */
  long last_excess_draw;            /* draws at the last over-plan late frame */

  long draws;                       /* frames drawn under late-draw */
  long late;                        /* of those, scheduled ones detected late */
  long warmups;                     /* frames drawn at once (not deferred) */
  long warmup_late;                 /* of those, detected late */
  long late_blameless;              /* scheduled, late, but ready in time:
                                       counted, the budget left alone */
  long late_excess;                 /* of late: the frame's own work ran over
                                       plan (a stall, or a heavier scene) */

  double draw_ring[LATEDRAW_DRAW_RING];   /* ms, CPU: step to swap submit */
  int draw_n, draw_i;

  double gpu_ring[LATEDRAW_DRAW_RING];    /* ms, GPU tail after the submit */
  int gpu_n, gpu_i;

  double flip_ring[LATEDRAW_FLIP_RING];   /* ms, consecutive flip intervals */
  int flip_n, flip_i;
} latedraw_state_t;

/* Reset everything and set the floor; effective starts at the floor.
   enabled is left 0: the caller enables once it has checked the swap-sync
   strategy can time flips. */
void latedrawInit(latedraw_state_t *s, double budget_ms);

/* Time (seconds, same clock as last_flip_s) before which the next frame
   must not be stepped/drawn: last_flip + (period - budget).  A budget >=
   period gives a target at or before the flip, i.e. no deferral. */
double latedrawTarget(double last_flip_s, double period_ms, double budget_ms);

/* Whole milliseconds to wait before `target` from `now`, rounding down;
   0 when the remainder is under 1 ms (or already past). */
int latedrawWaitMs(double now_s, double target_s);

/* Warm-up test: the frame about to be scheduled follows a swap the
   scheduler did not govern when the display's swap counter is not where the
   last governed frame left it (governed_swapcount < 0 = none yet).  Such a
   frame is drawn at once instead of deferred. */
int latedrawIsWarmup(long swapcount, long governed_swapcount);

/* Work the scheduler plans for: the LATEDRAW_WORK_RANK-th largest
   draw + GPU tail among the ring's frames (the largest when it holds fewer
   than that; 0 when empty).  A high quantile, not the maximum, so a single
   stalled frame does not move it, while a scene that got heavier does
   within a few frames. */
double latedrawWorkMs(const latedraw_state_t *s);

/* The budget for the state as it stands:
     fewer than LATEDRAW_MIN_SAMPLES frames measured -> the cap
       (period - 1 ms): nothing is known about this scene yet;
     else env + margin, at least the floor, at most the cap
       (a floor above the cap: the cap wins, as "no deferral" would).
   Unknown period (<= 0): the floor. */
double latedrawBudgetMs(const latedraw_state_t *s, double period_ms);

/* Late-frame test for the swap just completed: the display's missed-frame
   counter advanced, or the flip interval exceeded 1.5 periods.  A zero or
   negative interval (no new flip timestamp) is not late by itself. */
int latedrawIsLate(long missed_before, long missed_after,
                   double flip_interval_ms, double period_ms);

/* Account for one frame drawn under late-draw, slack unknown: see
   latedrawOnFrameSlack. */
int latedrawOnFrame(latedraw_state_t *s, double draw_ms, double gpu_ms,
                    int late, int warmup, double period_ms);

/* Account for one frame: record draw_ms and gpu_ms, count it, and set
   effective_ms for the next.  slack_ms is the time from the GPU finishing
   the frame (or the swap submit, without GPU timing) to the refresh it was
   aimed at; negative = not known or finished after it.

   Every frame: the work envelope follows latedrawWorkMs() up at once and
   comes down LATEDRAW_ENV_DECAY_MS per LATEDRAW_ENV_DECAY_FRAMES frames, so
   a display that alternates light and heavy stretches (a gaze window on for
   part of each trial) keeps the heavy stretch's budget across the light one.

   A late scheduled frame is one of three things:
   - ready in time (slack_ms >= LATEDRAW_BLAMELESS_SLACK_MS): the render
     thread was held off, or the display server or driver took the time.
     Counted in late_blameless; nothing changes.
   - its own work ran more than LATEDRAW_EXCESS_MS over the envelope: a
     stall, or the first frame of a heavier scene.  Counted in late and
     late_excess.  One such frame changes nothing (no budget short of the
     stall's own length would have saved it); a second within
     LATEDRAW_EXCESS_RUN frames is a step in the load, and the envelope is
     lifted to it at once instead of waiting for the quantile.
   - neither: the work was as planned and the frame still missed, so the
     margin is too small for this machine's swap path.  Counted in late; the
     margin grows by LATEDRAW_MARGIN_STEP_MS and stays there (it is reset
     only by latedrawInit: probing downward would cost a missed frame each
     time it found the edge).
   A late frame before LATEDRAW_MIN_SAMPLES frames are in is counted and
   teaches nothing.  A warm-up frame is counted in warmups / warmup_late and
   recorded in the rings, and changes neither envelope nor margin beyond
   what its measurements do through the ring.  Returns 1 for a late frame of
   any kind. */
int latedrawOnFrameSlack(latedraw_state_t *s, double draw_ms, double gpu_ms,
                         int late, int warmup, double period_ms,
                         double slack_ms);

/* Remember a flip interval (ms) for the period fallback; ignores <= 0. */
void latedrawNoteFlipInterval(latedraw_state_t *s, double interval_ms);

/* The period to schedule against: refresh_ms when > 0, else the median of
   the recorded flip intervals (at least 4 needed), else 0 = unknown. */
double latedrawPeriodMs(const latedraw_state_t *s, double refresh_ms);

/* Draw-time and GPU-tail statistics over the rings (0 when empty). */
double latedrawDrawMedianMs(const latedraw_state_t *s);
double latedrawDrawMaxMs(const latedraw_state_t *s);
double latedrawGpuMedianMs(const latedraw_state_t *s);
double latedrawGpuMaxMs(const latedraw_state_t *s);

#ifdef __cplusplus
}
#endif

#endif /* LATEDRAW_H */
