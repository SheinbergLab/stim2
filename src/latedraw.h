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
 * datapoints applied) without drawing.  The budget is the CPU time the
 * step+draw+submit needs before the vblank; the user's value is a floor and
 * the effective budget adapts upward on a late frame (missed vblank or a
 * flip interval over 1.5 periods) and decays back toward the floor after
 * long clean runs.
 *
 * Everything here is pure arithmetic on a plain struct that lives on the
 * render thread (no locks, no GL, no Tcl), so it can be unit-checked
 * without a display (tests/latedraw_check.cpp).  The integration (clock
 * reads, swap-sync queries, the Tcl command) is in stim2.cpp / tclproc.c.
 */
#ifndef LATEDRAW_H
#define LATEDRAW_H

#ifdef __cplusplus
extern "C" {
#endif

#define LATEDRAW_DRAW_RING 256      /* draw-time samples kept for stats */
#define LATEDRAW_FLIP_RING 16       /* flip intervals kept for the period */
#define LATEDRAW_CLEAN_RUN 600      /* clean frames per 0.25 ms decay step */
#define LATEDRAW_DECAY_MS 0.25
#define LATEDRAW_RAISE_FACTOR 1.5
#define LATEDRAW_DRAW_FACTOR 2.0
#define LATEDRAW_CAP_MARGIN_MS 1.0  /* effective <= period - this */
#define LATEDRAW_LATE_INTERVAL 1.5  /* flip interval > this * period = late */

typedef struct latedraw_state_s {
  int enabled;
  double floor_ms;                  /* the user's budget */
  double effective_ms;              /* current adaptive budget (>= floor) */

  long draws;                       /* frames drawn under late-draw */
  long late;                        /* of those, detected late */
  long clean_run;                   /* consecutive non-late frames */

  double draw_ring[LATEDRAW_DRAW_RING];   /* ms, CPU submit per draw */
  int draw_n, draw_i;

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

/* The adaptation rules, as pure functions of their inputs: */
/* late frame:  max(effective * 1.5, draw * 2), capped at period - 1 ms and
   never below the floor (when the floor itself exceeds the cap, the cap
   wins: such a budget means "no deferral" anyway). */
double latedrawRaise(double effective_ms, double draw_ms, double floor_ms,
                     double period_ms);
/* 600 clean frames:  effective - 0.25 ms, never below the floor. */
double latedrawLower(double effective_ms, double floor_ms);

/* Late-frame test for the swap just completed: the display's missed-frame
   counter advanced, or the flip interval exceeded 1.5 periods.  A zero or
   negative interval (no new flip timestamp) is not late by itself. */
int latedrawIsLate(long missed_before, long missed_after,
                   double flip_interval_ms, double period_ms);

/* Account for one frame drawn under late-draw: records draw_ms, counts it,
   applies the raise/decay rules.  Returns 1 if the frame counted as late. */
int latedrawOnFrame(latedraw_state_t *s, double draw_ms, int late,
                    double period_ms);

/* Remember a flip interval (ms) for the period fallback; ignores <= 0. */
void latedrawNoteFlipInterval(latedraw_state_t *s, double interval_ms);

/* The period to schedule against: refresh_ms when > 0, else the median of
   the recorded flip intervals (at least 4 needed), else 0 = unknown. */
double latedrawPeriodMs(const latedraw_state_t *s, double refresh_ms);

/* Draw-time statistics over the ring (0 when empty). */
double latedrawDrawMedianMs(const latedraw_state_t *s);
double latedrawDrawMaxMs(const latedraw_state_t *s);

#ifdef __cplusplus
}
#endif

#endif /* LATEDRAW_H */
