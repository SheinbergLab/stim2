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
      draws <n> late <n> warmups <n> warmup_late <n>
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

The budget the user gives is a floor. The effective budget is what is
actually scheduled against:

- After each governed swap, a frame is late when `swapsyncMissedFrames()`
  advanced since the previous swap, or the flip interval exceeded 1.5
  periods. On a late scheduled frame the effective budget is raised to
  `max(effective * 1.5, (draw_time + gpu_tail) * 2)`, capped at
  `period - 1 ms`, and the frame counted in `late`. A late warm-up frame
  only increments `warmup_late`.
- Every 600 consecutive non-late scheduled frames (5 s at 120 Hz) the
  effective budget is lowered by 0.25 ms, never below the floor. Warm-up
  frames neither extend nor reset that run.

`gpu_tail` is measured with a GL fence placed between the draw commands
and `glfwSwapBuffers`: after the swap call returns, the render thread
waits for the GPU to pass the fence and stamps the clock. The flip cannot
precede GPU completion and the thread is about to block in the swap wait
for that flip anyway, so the measurement is free; it is bounded at
100 ms so a stuck driver cannot hang the loop, and a context without
`glFenceSync` (ES2) just reports a tail of 0 and `gpu_timed 0`. The
result is that a scene which is cheap to submit but expensive to render
raises the budget by what it actually needs, instead of by the CPU time
alone.

So a miss can never cascade: the first late frame at least halves the
remaining slack, and a budget that proves too small walks back up until
misses stop, then slowly decays toward the floor. The pure arithmetic is
in `src/latedraw.cpp` and unit checked by `tests/latedraw_check.cpp`
(`ctest --test-dir build -R latedraw`), which needs no display.

Nothing in the scheduler is shared with the timer thread; all of its
state is plain data on the render thread.

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
read `draw_ms_max` and `gpu_ms_max` and raise the floor to at least twice
their sum. The adaptation will already have done so for the session
(`effective_ms`), but the floor is what survives a re-enable. A double
interval at the second frame of a trial with `late` unchanged and
`warmup_late` advanced is the warm-up case: the scene's first frame cost
more than a period, which a budget cannot fix; look at what `nexttrial`
builds instead.

Measured 2026-10-26 on that box before this rule existed: 3653 frames at
a 3 ms floor, draw time median 0.75 ms and max 0.98 ms, two slips, one of
them the second frame after a 2.2 s idle gap, the other 24 frames into a
trial with nothing dispatched in that frame; the controller settled at
`effective_ms 5.0`.
