# `stimLateDraw`: drawing the next frame just before the vblank

Off by default. For a display in continuous dynamic mode (a frame-based
dynamic group, `OL_DYNAMIC` set), stim2 normally steps and draws frame N+1
within tens of microseconds of frame N's flip and then sits in the swap
wait for the rest of the period. On a 120 Hz rig the draw takes about
1 ms, so for roughly 7 ms of every 8.333 ms the next frame is already
finished and anything that arrives (a command, a bound datapoint) can only
affect the frame after it: a median of 8.2 ms from dequeue to the flip
that shows it.

With late-draw on, the step+draw of frame N+1 is deferred until

    target = lastFlip + period - budget

where `lastFlip` is the hardware flip timestamp of frame N, `period` the
refresh period, and `budget` the CPU time the step+draw+submit needs
before the vblank. Until `target` the main loop runs as usual without
drawing: commands are evaluated and answered at once, datapoints update
their bindings, `!` commands are evaluated and their acks held for the
coming swap. At `target` the objects are stepped and the frame drawn,
submitted and waited for exactly as before. A command arriving anywhere
in the window is therefore shown one frame sooner than today.

Frame-based animation timing (`NextFrameTime`), explicit redraws (the
`redraw` message path), static displays, and everything under a
swap-sync strategy without flip timestamps are untouched: the scheduler
is a single early-out in the main loop, taken only when it is enabled,
the display is dynamic, and a flip time and period are known.

## Command

    stimLateDraw ?budget_ms?

- `stimLateDraw 3`: enable with a 3 ms floor. Refused with a Tcl error
  naming the swap-sync strategy when that strategy has no flip timestamps
  (`glfinish`, e.g. a Mac) or has not yet reported a flip or a refresh
  period: swap at least one frame first on the rig. The error says that
  flip timestamps are required.
- `stimLateDraw 0`: disable. The statistics are kept for reading.
- `stimLateDraw`: status. Every form returns

      enabled 0|1 budget_ms <floor> effective_ms <adaptive> period_ms <p>
      draws <n> late <n> warmups <n> warmup_late <n> late_blameless <n>
      late_excess <n> work_ms <x> margin_ms <x>
      draw_ms_med <x> draw_ms_max <x> gpu_ms_med <x> gpu_ms_max <x>
      gpu_timed 0|1

  `draws` counts frames the scheduler governed since enable, `late` those
  scheduled against the budget that it detected as late. `warmups` counts
  the frames it drew at once instead of deferring (see "Warm-up frames"
  below) and `warmup_late` how many of those slipped; such slips are
  reported here but never move `effective_ms`. `draw_ms_*` summarise the
  last 256 governed draws (step start to the return of `glfwSwapBuffers`),
  `gpu_ms_*` the GPU tail of the same frames (from that return until the
  GPU passed a fence placed after the draw commands); `gpu_timed` is 1 when
  the fence entry points exist in this GL context, else the tail reads 0.
  `period_ms` is 0 when no period is known, in which case the scheduler is
  dormant and the loop behaves as if disabled. An older stim2 answers
  `invalid command name`, which is the capability probe; one that answers
  without `warmups` predates the warm-up rule and GPU timing.

## Scheduling rule

Every loop pass with the scheduler active computes `target` from the
latest flip and compares it with the clock (one `glfwGetTime()` and a
comparison; the swap-sync accessors are plain loads). If `now < target`
the pass skips the draw and the next wait is bounded: whole milliseconds
of the remaining time, rounded down, or no wait at all when under 1 ms
remains. The 1 ms timer thread keeps waking the loop meanwhile, so
commands are processed throughout. If `now >= target` the pass steps and
draws.

The period is `swapsyncLastRefreshMs()` when the strategy reports one,
else the median of the last 16 flip intervals (once at least four are
known), else unknown (dormant).

## Warm-up frames

The first frame the scheduler governs after a swap it did not make is
drawn at once, not at `target`. That covers the static redraw that ends
an idle gap between trials (`targets_on` after a second or two of nothing
moving), a command's explicit `redraw` in the middle of a dynamic run, and
the first frame after `stimLateDraw` is enabled. The test is simply
whether the swap counter still stands where the previous governed frame
left it; if not, somebody else swapped and this frame is a warm-up.

Why: on the 120 Hz office display box, with a 3 ms floor and a CPU draw
time under 1 ms, the second frame of a new trial slipped a vblank. That
frame is a fresh scene (a motion patch and discs built by `nexttrial`,
programs and buffers in first use) whose GPU cost no CPU stamp shows, and
the ~2 ms left after a 3 ms budget was not enough for it. Drawing that
one frame immediately gives it the whole period, costs nothing in
freshness (there was no input to be fresh about during the idle gap), and
a slip there is counted in `warmup_late` but does not raise the budget,
because no budget would have bought it back. The frame after it is
scheduled normally against `effective_ms`.

## Adaptation rule

The budget the user gives is a floor. The budget actually scheduled
against is

    effective = max(floor, work + margin)      at most period - 1 ms

and stim2 finds `work` and `margin` for the machine and the scene it is
running, so a floor does not have to be tuned per rig. A floor lower than
the machine can do is simply not reached.

- **work** is what frames have lately taken: draw time plus GPU tail,
  measured on every governed frame. It is the 5th largest of the last 256
  frames (about the 98th percentile), so one stalled frame does not move
  it and a scene that got heavier does within a few frames. It follows
  that figure up at once and comes down 0.25 ms per 600 frames (5 s at
  120 Hz), so a display that is heavy for part of each trial (a gaze
  window that is on only during the stimulus) keeps the heavy part's
  budget through the light part.
- **margin** is the time the swap needs between the GPU finishing and the
  vblank. It starts at 1 ms.
- Until 32 frames have been measured since enable, the budget is the cap:
  nothing is known about the scene yet.

After each governed swap a frame is late when `swapsyncMissedFrames()`
advanced or the flip interval exceeded 1.5 periods. A late scheduled
frame is one of three things:

- **Ready in time**: the GPU had finished it (or, without GPU timing, the
  swap had been submitted) at least 2.5 ms before the refresh it was
  aimed at. The render thread was held off, or the display server or the
  driver took the time. Counted in `late_blameless`; nothing changes.
- **Over plan**: its own work ran more than 0.75 ms over `work`. A stall,
  or the first frame of a heavier scene. Counted in `late` and
  `late_excess`. One such frame changes nothing, since only a budget as
  long as the stall would have saved it. A second within 30 frames is a
  step in the load, and `work` is lifted to it at once.
- **Neither**: the work was as planned and the frame still missed, so the
  margin is too short for this machine's swap path. Counted in `late`,
  and the margin grows by 0.25 ms. It does not come back down until
  late-draw is enabled again: looking for the edge again would cost a
  missed frame each time it was found.

A late warm-up frame only increments `warmup_late`. `stimLateDraw`
reports `work_ms`, `margin_ms` and `late_excess` with the rest.

### How the rule was arrived at

The first rule raised the budget to `max(effective * 1.5, work * 2)` on
any late frame and lowered it 0.25 ms per 600 clean frames. On a 120 Hz
rig, 2026-11-17, with a 3 ms floor and frames that needed about 4.3 ms
(draw 0.6, GPU tail 2.6 with a gaze window, 0.45 ms to the vblank):

- the budget sank until a frame missed near 3.9 ms, jumped to the 7.33 ms
  cap, and took a minute to come back, over and over: a median of 5.6 to
  6.6 ms across sessions;
- stray late frames with 3 ms of slack, about one per 15 s until a second
  real-time process and unpinned cores were dealt with, each did the
  same;
- GPU stalls of 5 to 6 ms on single frames, a few a minute, each did the
  same;
- a 2011 workstation and a 2024 one sat at the same budget, because the
  rule, not the hardware, set it.

Replaying the per-frame records of three of those sessions through the
rule above gives a steady 4.6 to 4.9 ms, with the stalls each costing the
one frame they would cost anyway.

## The record: `stim/latedraw`

With the frame log on (`stimFrameLog`, [stim-frame.md](stim-frame.md)),
each frame the scheduler governed sends one int64 datapoint beside its
`stim/frame` record, saying where the frame's time went:

| # | field | |
|---|---|---|
| 1 | `swapcount` | of the swap, as in its `stim/frame` record |
| 2 | `effective_us` | the budget the frame was scheduled with |
| 3 | `wake_late_us` | step start minus the scheduled start (`flip + period - budget`): how late the loop got to it. -1 on a warm-up |
| 4 | `draw_us` | step start to the return of `glfwSwapBuffers` (the step, the frame scripts and the draw calls) |
| 5 | `gpu_us` | from that return until the GPU passed the fence; -1 if not timed |
| 6 | `slack_us` | from the GPU finishing (or the submit) to the refresh aimed at; negative if it finished after. -1 on a warm-up |
| 7 | `interval_us` | the flip interval this frame ended |
| 8 | `flags` | 1 late, 2 warm-up, 4 late but ready in time, 8 late with its own work over plan |

`budget = wake_late + draw + gpu + slack` for a scheduled frame. A late
frame with a large `wake_late_us` lost its time before it started (the
loop was busy, or the thread was not run); one with a large `slack_us`
lost it after the GPU was done, in the swap's path to the display.

## What stays the same

- The `stim/frame` record layout is unchanged. Its flip intervals and
  `missed` counter already show misses in the data file; the late count
  and draw statistics live in the status dict.
- The `@` reply header and `!` swap-ack semantics are unchanged. A `!`
  command dequeued during the deferral window is evaluated at once and
  acked after the deferred swap, with that swap's flip time, exactly the
  "ack after the swap this command caused" promise.
- With late-draw off, the main loop is the same code path as before:
  `tests/test_ack_header.tcl` and `tests/test_frame_log.tcl` against a
  build with the feature present but disabled show no change.

## Validating on the rig

A rig with flip timestamps is where the effect can be measured: the
120 Hz X11 display box (`oml` strategy, flip timestamps from
`glXWaitForSbcOML`; the period is measured from consecutive UST/MSC
stamps, with the video mode's period standing in after the first swap) or
a Wayland rig (`presentation` strategy, period reported by the
compositor). A Mac's `glfinish` strategy refuses to enable. On a
compositor the frame must also make the compositor's repaint deadline,
so expect the adaptation to settle at a larger `effective_ms` there than
on direct X11 flips.

1. Start stim2 as usual, let ESS configure it, swap at least one frame.
   `swapStats` must show `refresh` of about 8.333 and a positive
   `lastflip`.
2. Enable with `stimLateDraw 3` (a 3 ms floor is three times the measured
   1 ms draw; adjust after reading `draw_ms_max`). The status dict shows
   `enabled 1 period_ms 8.333...`.
3. Turn the frame record on (`stimFrameLog <dserv-host> 1`, or let ESS do
   it) and run a dynamic protocol with `stim/frame` logged to the data
   file.
4. In the data file, check the frame record: consecutive `ticks_us` and
   `flip_us` differences stay at the period (8333 us, no doubles) and
   `missed` does not advance. `stimLateDraw` afterwards should report
   `late 0` and `effective_ms` at the floor; a few late frames with
   `effective_ms` above the floor mean the floor is too tight for that
   scene and the adaptation absorbed it.
5. Send plain `@` commands during animation (for instance a stream of
   `@set x 1` from the ESS side, or `tests/test_ack_header.tcl`'s header
   parsing). Their `ACK1` dequeue stamps relative to the preceding flip
   should now spread across the whole frame instead of clustering within
   0.1 ms after each flip, because the loop is awake and answering
   throughout the window rather than blocked in the swap wait.
6. Send `@!` commands during animation. Their done stamp to the next flip
   (`flipwall_us` in the same header, or the next `flip_us` in the frame
   record) should drop from about a full period to about the budget
   (3 ms), since the swap now happens at `target` rather than right after
   the previous flip.
7. Repeat with `stimLateDraw 0` as the control: dequeue stamps cluster
   just after the flips again and done-to-flip returns to a period.

If `missed` advances or flip intervals show doubles with late-draw on,
read the status: `late_excess` counts frames whose own work ran long (a
stall, which no budget fixes short of its length; `gpu_ms_max` shows how
long), `late_blameless` frames that were ready and missed anyway (look at
what else runs at real-time priority, and at core pinning), and a
`margin_ms` above 1 says the swap path on this machine needs more than
1 ms after the GPU is done. The floor does not need raising for any of
these: `effective_ms` is already where the measurements put it. The
`stim/latedraw` records in the data file give the same per frame. A double
interval at the second frame of a trial with `late` unchanged and
`warmup_late` advanced is the warm-up case: the scene's first frame cost
more than a period, which a budget cannot fix; look at what `nexttrial`
builds instead.

Measured 2026-10-26 on that box before this rule existed: 3653 frames at
a 3 ms floor, draw time median 0.75 ms and max 0.98 ms, two slips, one of
them the second frame after a 2.2 s idle gap, the other 24 frames into a
trial with nothing dispatched in that frame; the controller settled at
`effective_ms 5.0`.
