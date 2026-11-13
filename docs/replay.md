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
    dlsh tools/replay/replay.tcl FILE.ess -render out/trial.mp4 -each -fps 60   # one movie per trial
    dlsh tools/replay/replay.tcl FILE.ess -render out/trial.mp4 -each -launch   # starts and stops its own stim2

`FILE` may also be the obs DG dfconf writes for each `.ess` at close
(`FILE.obs.dgz`, `dslog::readESS`), with the same options. For `-render`
the DG must have been converted with wide types on (dfconf's `df
wide_datafiles` setting, the default from dserv 0.60.16; `dslog::wideTypes
1` by hand): `stim/frame` is int64, and a narrow conversion leaves its
columns empty, which the script reports. The `.ess` always works.

| option | |
|---|---|
| `-host H`, `-port P` | the stim2 to drive (default `localhost:5612`, a stim2 started with `--port-base 5610`). Port 4612 is the live display's and is refused unless `-live` is given. |
| `-launch` | start a stim2 of our own on `-port`'s base and stop it on exit. With `-render` it is headless at the rig's pixel size and refresh from the file's `ess/screen_*` records, so the movie is the rig's picture; otherwise a window in the rig's aspect, no wider than 960. `-host` must be localhost, and nothing may already answer on `-port`. Startup is a few seconds; the driver waits for it. `-keep` leaves that stim2 running for the next file, `-size WxH` overrides the size, `-stim2 PATH` names the binary (default: the app bundle these tools came with, else the installed one). |
| `-live` | allow `-port 4612`. Only when you mean to draw on the live display. |
| `-trial N`, `-trials A-B` | one trial or a range, 1-based by observation period. A trial's window runs from the previous period's end to its own, so the commands that set it up (`nexttrial`, `targets_on`) travel with it. Default: the whole file. |
| `-speed X` | playback rate, default 1.0. `0` sends as fast as stim2 takes it. |
| `-gap S` | longest pause kept between two events (default 1 s). Idle time between trials is compressed to this. |
| `-stream NAME` | also inject records named `NAME`. The streams named in the file's `stim/bind` table are always injected, and so is the source of any `gazewin source` command in the file. Numeric records are injected as text (`"x y"`), as the obs reader gives them. |
| `-dry` | read and plan, print the summary and the first events, send nothing. |
| `-verbose` | print every event as it goes out. |
| `-render OUT` | frame-locked into a movie on the rig's timeline (below). `OUT` ending `.mp4`/`.mov`/`.mkv` goes through ffmpeg; anything else gets raw RGBA (a file, or `"|command"`). `-speed` is ignored; `-gap` compresses only when given. |
| `-each` | with `-render OUT` into a movie file: one movie per trial of the range (default every trial), holding that trial's rig frames and gaze. `OUT` names them: a `%` format takes the trial number (`out/planko_t%03d.mp4`), otherwise `_tNNN` goes before the extension. A trial's last frame holds for one movie frame; the rig's idle time before the next trial opens the next movie. The stim2 runs straight through, as for one movie, so each trial starts from the state the previous ones left, exactly as on the rig. |
| `-eye MODE` | with `-render`, draw the logged gaze (`eyetracking/raw` with `em/settings`) over each movie tick (below). `auto` (default), `analog`, `video`, `degrees` or `off`. |
| `-touch` | with `-render`, draw the touchscreen presses (ESS TOUCH events, type 48, in screen pixels, put in degrees from the file's `ess/screen_*`) as rings that follow the finger through each drag (TOUCH MOVE), with a 500 ms fading trail, until the release (250 ms past the last sample when none was logged), then at the release point, fading over 400 ms. Sessions recorded before MOVE was logged have presses and releases only. |
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
records. The placement and the per-frame request are
`tools/lib/framelock.tcl`, shared with the live mirror (`mirror.md`):
each rig frame's commands, datapoints, gaze, `stepFrame` and movie
writes go to stim2 as one request. Each rig frame is drawn once, after
exactly the inputs it saw:

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

## As a library: `tools/lib/replaylib.tcl`

`replay.tcl` is the command line over `tools/lib/replaylib.tcl`, which a
program that renders many parts of one session uses directly (a trial
viewer's render service): the file is read once, and any trial can be
planned and rendered after any other, on one stim2.

    source /usr/local/stim2/tools/lib/replaylib.tcl   ;# also sources framelock.tcl
    set S    [replay::load FILE.obs.dgz]              ;# -streams, -geometry, -bg
    set pid  [replay::launch -port 5612 -geom [replay::get $S geom] -render 1 -size 960x540]
    set sock [replay::connect localhost 5612 $pid]
    replay::check_stimess $sock
    replay::render_trial $S $sock 7 clips/t007.mp4    ;# + clips/t007.json
    replay::render_trial $S $sock 3 clips/t003.mp4

| proc | |
|---|---|
| `load FILE ?opts?` | read a `.ess` or obs DG into a session (a namespace); errors if it has no script, stimdg or events |
| `plan S ?-trials {A B}? ?-render OUT? ?-each? ?-fps? ?-blend? ?-gap? ?-eye? ?-touch? ?-lead?` | what to play, as a dict; with `-render`, the frame-locked plan. The session is not changed. `-lead 1` starts the movie at the first trial's BEGINOBS (below; not with `-each`) |
| `dumpplan P FILE` | the `-dumpplan` text |
| `launch`, `connect`, `check_stimess`, `setup_dict` | a stim2 to draw on, and `framelock::setup`'s input |
| `play P SOCK` | paced by time |
| `render P SOCK ?-crf? ?-keyint?` | frame-locked into the plan's output |
| `render_trial S SOCK N OUT` | one trial from a fresh setup, into a movie file and its timing JSON; from BEGINOBS unless `-lead 0` |

Errors carry `-errorcode {REPLAY n}`, `n` being the command line's exit
code. `render_trial` sets the stim2 up again each time (`stepMode 0`,
prelude, script, stimdg, `reset`), so trials can come in any order; the
result is the command line's `-trial N`, frame for frame. Defaults suit a
browser: `-keyint 12` (a keyframe every 0.1 s at 120 fps, for scrubbing)
and `-eye off` (the viewer draws the gaze). As with `-trial N`, a script
that carries state from trial to trial inside stim2 starts each trial from
the script as loaded; `-each` is the straight-through, exact form.

### The clip starts at BEGINOBS

stim2 swaps only when something changes, so a trial's first new rig frame
often comes well after BEGINOBS: 250 to 300 ms in a search or planko
session, and seconds when a fixation period waits on the eye. The rig was
still showing what the last trial left, which a fresh setup does not
have. `render_trial` (plan `-lead 1`) starts the clip at BEGINOBS anyway,
with that stretch as a lead: one frame stepped with the scene list hidden
(`glistSetVisible 0`, restored after), so the rig's background
(`ess/screen_bg`, which setup sets), held to the first frame. The gaze
and touches are drawn over it as over any held frame, so a fixation
shows. It is the same thing dg_viewer paints before an older clip's
first frame. A frame from before BEGINOBS (between the previous ENDOBS
and it) is still stepped, so the trial's state builds up as on the rig,
but not written: the clip shows it from BEGINOBS on. `-lead 0` gives the
clip that starts at the first frame (timing version 1's).

The lead is cheap: one stepped frame and a held write, with a repaint
only where the gaze or a touch moves. Viewer clips get it too, not only
exports. The viewer's timeline starts at BEGINOBS (`em_seconds` 0 is
`em_zero_ms`, a few ms in), so it never showed the frames before it, and
with the lead the video is the clock from the first moment of the trial
instead of taking over from the wall clock at the first frame. A trial's
viewer clip and its export are then the same picture, the export with
the overlays drawn in.

### The timing JSON

The timing JSON puts the clip on the trial's timeline. Times are ms from
the clip's first movie frame: BEGINOBS when `start` is `beginobs`, the
first rig frame when it is `frame`:

| key | |
|---|---|
| `version` | 2 (1: no `start`, `lead_ms`, `first_frame_ms`; always from the first frame) |
| `start` | `beginobs` (the default) or `frame` (`-lead 0`) |
| `lead_ms`, `first_frame_ms` | the background lead, and where the first rig frame starts showing (0 when one came before BEGINOBS) |
| `trial`, `obsid`, `ntrials` | 1-based trial (observation period), the same 0-based, the file's count |
| `fps`, `movie_frames`, `width`, `height` | the clip |
| `screen` | the rig's `halfx`, `halfy` (degrees), `w`, `h`, `hz`, `bg` |
| `obs_begin_ms`, `obs_end_ms` | BEGINOBS and ENDOBS. Data timed from BEGINOBS (event times) is at `t + obs_begin_ms`, which is 0 from BEGINOBS |
| `em_zero_ms` | the period's first eye record (`null` without one). A trials DG's `em_seconds` counts from there (`em::normalize_timestamps`), not from BEGINOBS: it is at `t + em_zero_ms`, typically 2 to 5 ms after BEGINOBS |
| `touches` | the trial's touch strokes, drawn in the clip or not: `[t, x, y, t_up, x_up, y_up, path]` (ms, degrees), the release `null` when none was logged, `path` the drag as a flat `[t, x, y, ...]` or `null` for a tap |
| `window_start_ms` | the previous period's end, where the trial's window opens (before the clip) |
| `rig_frames` | per rig frame: `movie_ms` (where it starts in the clip; negative for a frame from before BEGINOBS, stepped but not in the clip), `flip_ms` (its flip; they differ by the rounding to movie ticks) and `swap` |
| `cmds` | each command, with the index of the rig frame that first showed it |
| `tick_off_us` | the stim clock minus the dserv clock, by which datapoint times were placed |

Measured 2026-11-08 on the dev Mac (M3), headless, a planko obs DG
(133 trials) loaded once in 0.8 s: a 7 s trial (809 rig frames) renders
in 0.5 s at 320x180, 1.7 s at 960x540 and 5.1 s at 1920x1080, where
x264 sets the pace. Planned and rendered through the library, the four
test files' plans (`-dry`, `-dumpplan`, with and without `-trials`,
`-each`, `-gap`) and their raw frames are identical to the script before
the split.

## For the browser player

This script is the reference for the WebAssembly viewer: the same five
record types, the same order of operations, `stim::inject` as the delivery
path, and the same limits. A browser build that ships `stimess-1.1.tm` in
its filesystem can evaluate exactly what this script sends.
