# Replaying a data file into stim2

`tools/replay/replay.tcl` plays the stim2 side of a dserv `.ess` data file
back into a running stim2, standing in for ESS. The stim script that ran on
the rig runs again, unchanged, fed the same commands and the same bound
datapoints in the same order and at the same pace.

    stim2 --port-base 5610 -w 960 -h 540 &      # a second stim2, rig aspect
    dlsh tools/replay/replay.tcl FILE.ess -trial 7
    dlsh tools/replay/replay.tcl FILE.ess -trials 1-20 -speed 2
    dlsh tools/replay/replay.tcl FILE.ess -dry

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

## What is and is not reproduced

- **Reproduced:** what the script drew in response to what, in order and
  at the recorded pace. The commands, the trial table, the cursor path
  (and any other bound stream), the script's own frame logic.
- **The display is this machine's.** The degree mapping is the rig's (step
  4), but the refresh rate and pixel size are stim2's own. A frame on the
  rig is not a frame here, so the replay is paced by time, not by
  `stim/frame`. The frame record is still the authority on what the rig
  showed when; it is not used to drive the replay.
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
