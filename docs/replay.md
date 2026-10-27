# Replaying a data file into stim2

`tools/replay/replay.tcl` plays the stim2 side of a dserv `.ess` data file
back into a running stim2, standing in for ESS. The stim script that ran on
the rig runs again, unchanged, fed the same commands and the same bound
datapoints in the same order and at the same pace.

    stim2 --port-base 5610 -w 960 -h 540 &      # a second stim2, rig aspect
    dlsh tools/replay/replay.tcl FILE.ess -trial 7
    dlsh tools/replay/replay.tcl FILE.ess -trials 1-20 -speed 2
    dlsh tools/replay/replay.tcl FILE.ess -dry
    stim2 --headless -w 1920 -h 1080 -r 120 --port-base 5610 &
    dlsh tools/replay/replay.tcl FILE.ess -render movie.mp4     # frame-locked

| option | |
|---|---|
| `-host H`, `-port P` | the stim2 to drive (default `localhost:5612`, a stim2 started with `--port-base 5610`). Port 4612 is the live display's and is refused unless `-live` is given. |
| `-live` | allow `-port 4612`. Only when you mean to draw on the live display. |
| `-trial N`, `-trials A-B` | one trial or a range, 1-based by observation period. A trial's window runs from the previous period's end to its own, so the commands that set it up (`nexttrial`, `targets_on`) travel with it. Default: the whole file. |
| `-speed X` | playback rate, default 1.0. `0` sends as fast as stim2 takes it. |
| `-gap S` | longest pause kept between two events (default 1 s). Idle time between trials is compressed to this. |
| `-stream NAME` | also inject records named `NAME`. The streams named in the file's `stim/bind` table are always injected. |
| `-dry` | read and plan, print the summary and the first events, send nothing. |
| `-verbose` | print every event as it goes out. |
| `-render OUT` | frame-locked into a movie on the rig's timeline (below). `OUT` ending `.mp4`/`.mov`/`.mkv` goes through ffmpeg; anything else gets raw RGBA (a file, or `"|command"`). `-speed` is ignored; `-gap` compresses only when given. |
| `-eye MODE` | with `-render`, draw the logged gaze (`eyetracking/raw` with `em/settings`) over each movie tick (below). `auto` (default), `analog`, `video`, `degrees` or `off`. |
| `-crf N` | x264 quality for a movie file, default 14 (lower is better and bigger; 0 is lossless). Encoded with `-tune animation`, which suits flat stimulus graphics. |
| `-noblend` | with `-fps` below the rig's refresh, drop rig ticks instead of averaging them. |
| `-fps F` | the movie's frame rate with `-render`, default the rig's refresh. Below the refresh, into a movie file, every rig tick is still drawn, and each movie frame is the mean of the ticks it spans (2 ticks for 60 fps from a 120 Hz rig). That looks much smoother than dropping every other tick. QuickTime Player plays movies above 60 fps in slow motion after the first seconds; browsers and mrv2 play them right. Use `-fps 60` for a QuickTime copy (frames shorter than a tick then drop). |

Needs `dlsh` with the `dslog` and `tcljson` packages (dlsh.zip 0.20.4 or
newer) and a stim2 with stimess 1.1 or newer (`stim::prelude playback`).

## What it does

In the order ESS's `configure_stim` does it:

1. `stim::prelude playback` (see `docs/stimess.md`): the same hook reset
   and table clearing as on the rig, then no registration, no return
   socket, `dserv_send` and the mouse handlers as no-ops. `stim::bind` from
   the script records bindings without asking any server.
2. Each `stim/lib/<name>` body, then the `stim/script` body, then
   `stim_init`: the exact text the rig ran, from the file.
3. `stim::inject stimdg <base64> 6`: the trial table as logged, through the
   same `readdg` handler the rig used.
4. The rig's degree mapping: `screen_set HalfScreenDegreeX` and `...Y` from
   the file's `ess/screen_halfx` and `ess/screen_halfy`, so a target at
   10 degrees lands where it did relative to the rig's screen edges. The
   window's pixel size is stim2's own (`-w`/`-h` at startup); the script
   warns when its aspect differs from the rig's by more than 5%, since
   circles would then draw as ellipses, and suggests a size.
5. The window is retitled `replay: <file> (<trials>)` through
   `setWindowTitle` (stim2 0.28.2 or newer; older ones keep "Stim").
6. The timeline. Every `stim/cmd` record goes out as the command text that
   was sent; every record of a bound stream goes in with `stim::inject`,
   which delivers it exactly as stim2's render thread would. Both are
   ordered and paced by their dserv timestamps (`t_send` for commands).

At the end it reports events sent, errors, elapsed time, how many frames
stim2 swapped, and per stream the count injected and the last value.

Measured 2026-10-27 on the dev Mac against a throwaway stim2 (`--port-base
5610`) with an office-rig motiondir file: trials 2 and 3, 8 commands and
170 pointer records over a recorded 6.4 s, played at 2x in 3.2 s with no
errors and 225 frames swapped; the cursor binding reported all 170 arrivals.

## Frame-locked: `-render`

Uses stim2's step mode and manual frame sink (`docs/headless.md`), and
needs a file with `stim/frame` records and ACK-stamped `stim/cmd`
records. Each rig frame is drawn once, after exactly the inputs it saw:

- **Commands** by their ACK stamp. stim2 applied each one with
  `SwapCount = swap`, so it first shows in frame `swap+1`. A `!` command,
  answered after its swap and carrying a flip, shows in frame `swap`. The
  clock is set to the command's `dq` tick before it goes out.
- **Datapoints** by the frame's `ds_dispatched` advance: that many records,
  in order, among those dserv already had before the frame's record. A
  shortfall means a forwarded datapoint that is not in the file's streams.
  Their times go onto the rig clock through the median of ticks minus
  dserv time across the frames. A trial window starts from each stream's
  last record before it.
- **The frame:** `stepFrame` at max(clock, previous flip), with the
  frame's own flip.
- **The movie:** each frame is written once per movie tick it spans on the
  rig's flips. Static stretches hold, a frame the rig showed for two
  refreshes shows for two ticks, and frames shorter than a tick drop.

Measured 2026-10-27 with `momo_joystick_motiondir_261026195800.ess` at
the rig's 1920x1080 and 120 Hz:
- 3654 rig frames (no swapcount gaps) were drawn with 0 errors. All 51
  commands were placed by ACK stamp, and 1233 of 1233 pointer records by
  `ds_dispatched`; one forwarded datapoint was not in the file.
- The movie has 5702 frames, 47.5 s, which is the session's span.
- It took 25.1 s headless on the Linux box's Radeon and 27.7 s on the dev
  Mac.
- The check: the first correct-feedback command has `swap` 10072. Rig
  frame 10073 flips 5558.4 ms after the first frame, which is movie
  frame 667. Movie frame 667 is the first to show the green target.

### Gaze

The gaze is drawn on stim2's overlay layer (`overlay` and `frameRepaint`,
`docs/headless.md`), not as stim2 objects, so a script that resets its
objects every trial does not remove it. Each movie tick shows:
- a disc at the newest sample no more than 100 ms old, hidden when there
  is none (ESS logs eye data only inside observation periods);
- a fainter trail of the last 150 ms.

Where the rig held a frame for several ticks while the eye moved, the same
scene is repainted with the gaze at each tick's time.

How the samples are read:
- **Batching.** `eyetracking/raw` is batched by dserv: 10 (h, v) pairs per
  80-byte record, stamped with the first sample's time. The others are
  spread at the median record interval.
- **Calibration** follows dserv's `emconf`, using the `em/settings` logged
  in the file: either linear (`scale * (raw - center)`, with `invert`
  applied afterwards on the analog path; the video path's raw is already
  inverted) or the biquadratic fit when `use_biquadratic` is on.
- **`-eye auto`** treats a sample within ±90 as already in degrees, which
  is how the virtual source logs it, and anything else as counts on the
  analog path. That also handles a file where two sources were writing to
  `eyetracking/raw` at once.
- **An exact (0, 0) is not a sample.** It is the virtual eye at rest when
  the virtual source is mixed with a real one.

For files that would be ambiguous, logging `eyetracking/position` (already
in degrees) alongside the raw samples would remove the guesswork.

## What is and is not reproduced

- **Reproduced:** what the script drew in response to what, in order and
  at the recorded pace. The commands, the trial table, the cursor path
  (and any other bound stream), the script's own frame logic.
- **Not reproduced: random draws.** motionpatch places its dots with the
  process-wide, unseeded `rand()`, so a replay shows the same stimulus
  parameters with a different sample of dots. Matching the rig dot for dot
  needs a per-object seeded generator in the stimdll with the seed logged;
  with `-render` the frame count already matches the rig's.
- **The display is this machine's.** The degree mapping is the rig's (step
  4), but the refresh rate and pixel size are stim2's own. Paced by time,
  a frame on the rig is not a frame here; with `-render` it is, one for
  one, by `stim/frame`.
- **Nothing leaves stim2.** `dserv_send` and friends do nothing, so a
  script that reports outcomes back (planko's `complete`) reports to no
  one, which is correct: the outcome is already in the file.
- **Not covered yet:** assets stim2 opens by path (shaders, images, fonts,
  video, spine). A script that needs them needs them installed here under
  the same paths until the `stim/asset` manifest exists.
- **Files recorded before `stim::bind`** have an empty `stim/bind` table,
  so nothing is injected and a cursor driven by a hand-rolled subscription
  stays put: the old script registered its handler only when
  `::dservhost` was set. `-stream ess/dial/pointer` injects the records
  anyway, but the handler is still not installed; replay such files with
  the ported script, or accept a still cursor.
- **Commands logged as hash only** (longer than 4096 bytes) are skipped and
  counted. Configure-time commands that may appear in a file
  (`stim::prelude`, `stimFrameLog`, `set dservhost`) are skipped too.

## For the browser player

This script is the reference for the WebAssembly viewer: the same five
record types, the same order of operations, `stim::inject` as the delivery
path, and the same limits. A browser build that ships `stimess-1.1.tm` in
its filesystem can evaluate exactly what this script sends.
