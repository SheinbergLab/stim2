# Trial clips over HTTP: the render server

`tools/render/render_server.tcl` renders any trial of a recorded session
into a frame-locked movie, on a headless stim2 of its own, and serves it to
a browser with the timing that puts the clip on the trial's timeline. It is
the back end of a trial viewer: the page asks for a trial and plays the
clip, with its own traces and gaze drawn in step with it.

    dlsh tools/render/render_server.tcl                 # http://127.0.0.1:8140/
    dlsh tools/render/render_server.tcl -host 0.0.0.0 -data /usr/local/dserv/work/obs

Each clip is `replay::render_trial` (`docs/replay.md`, "As a library"):
the rig's own script, fed the trial's commands and bound datapoints frame
by frame on the rig's clock. The session's file is read once and kept in
memory; any trial can follow any other.

## Endpoints

| request | answer |
|---|---|
| `GET /session?file=NAME` | loads the session: `{file, path, ntrials, screen, width, height}` |
| `GET /trial?file=NAME&trial=N` | trial N (1-based), or `&obsid=K` (0-based, a trials DG's `obsid`). Renders it unless cached, then answers `{clip, cached, timing}`; the request waits for the render. `&then=K,K` names the obsids to render next (the rows a viewer steps to: not every obs period is a trial); without it, the next `-prefetch` trials |
| `GET /clips/...` | the clip (`clip` above) and its JSON, with byte ranges (`Range`, `206`), which browsers need to seek |
| `GET /status` | the stim2, the sessions in memory, the clip rendering and its progress, the queue |

`NAME` is a session's base name, with or without `.obs.dgz`,
`.trials.dgz` or `.ess`, so a viewer holding a trials DG passes its
`filename`. The server looks for `NAME.obs.dgz`, then `NAME.ess`, in its
data directories. Names with anything but letters, digits, `.`, `_` and
`-` are refused, and `/clips/` serves only `.mp4` and `.json` files inside
the cache.

`timing` is the clip's timing JSON (`docs/replay.md`): BEGINOBS and ENDOBS
on the clip's timeline, each rig frame's place and flip, the commands, the
rig's screen in degrees and pixels. Data timed from BEGINOBS is at
`t + obs_begin_ms` in the clip; a trials DG's `em_seconds`, which counts
from the period's first eye record, is at `t + em_zero_ms`.

dserv's `www/dg_viewer.html` is the viewer: with a server answering
(`?render=URL`, else port 8140 on the page's host), its world panel plays
the selected trial's clip, the clip is the clock while it plays, and the
gaze is drawn over it (`www/js/stim2_view.js`). Its Sessions panel lists
the dserv host's sessions (`df`'s `list_trials_files`: those whose trials
file is in the work directory) and opens them from dserv's read-only
`/data/trials/NAME.trials.dgz`; dserv also serves
`/data/obs/NAME.obs.dgz`, streamed in 64 KB pieces so its web loop keeps
serving datapoints meanwhile. A session stim2 cannot replay (a 422 here:
recorded before stim2 kept frame records) shows the viewer plugin's own
drawing, with the stim2 checkbox reading "stim2: no clip".

Errors are JSON `{error}` with 400 (a bad request), 404 (no such file),
422 (a file that cannot be replayed, with the reason) or 500 (a render
that failed). Every answer carries `Access-Control-Allow-Origin: *`, so a
page served by dserv can use a server on another port or machine.

## Options

| option | |
|---|---|
| `-port P` | HTTP port (default 8140) |
| `-host A` | address to listen on (default `127.0.0.1`; `0.0.0.0` to serve other machines) |
| `-data DIR` | where sessions are (repeatable; replaces the defaults `~/data/obs`, `~/data/ess`, `/usr/local/dserv/work/obs`, those that exist) |
| `-cache DIR` | where clips go (default `~/.cache/stim2-render`) |
| `-width W` | clip width (default 960); the height follows the rig's aspect |
| `-prefetch N` | trials after the one asked for to render next (default 2) |
| `-sessions N` | sessions kept in memory (default 3, least recently used dropped) |
| `-stim2 PATH`, `-stim2-port P` | the stim2 it launches (default as `replay.tcl -launch`) and its message port (default 5652, `--port-base 5650`, beside a live stim2 and a replay's) |

## How it works

- **One stim2, one clip at a time.** The stim2 is launched at startup,
  headless at `-width` in the rig's aspect and at the rig's refresh. A
  session with another aspect or refresh relaunches it (about a second).
  Its version is part of the cache's directory name.
- **The queue.** A request goes to the front. After answering, the server
  queues the next `-prefetch` trials (or the request's `then=` obsids, the
  rows the viewer will step to next), so stepping forward through a
  session mostly finds clips waiting. A request for somewhere else drops
  the queued prefetches; the clip already rendering finishes first.
- **Still answering while it renders.** The render runs in a coroutine
  that yields every 10 rig frames (`render -progress`); a clip being sent
  goes first (the render waits on it, up to 100 ms a turn), so clips,
  `/status` and new requests are served meanwhile, and several clients
  waiting on one trial get the one render.
- **The cache.** `CACHE/NAME/c<format>_<W>x<H>_<mtime>_<size>_s<stim2>/tNNN.mp4`
  and `.json`. A re-converted file (new mtime or size), another clip size
  or another stim2 gets a new directory; a clip is written under a
  temporary name and renamed when complete, so a half-written one is never
  served. Nothing is evicted: delete the directory to reclaim it.
- **Clips** are H.264 at the rig's refresh, `-crf 14 -tune animation`, a
  keyframe every 12 frames (0.1 s at 120 Hz) so seeking lands quickly,
  `+faststart`. No gaze overlay: the viewer draws the gaze from its own
  data.

A trial starts from a fresh setup (`render_trial`), not from the state
earlier trials left in the rig's stim2: a script that carries state across
trials can differ here. `replay.tcl -each` is the straight-through form.

## Measured

2026-11-08 on the dev Mac (M3), stim2 0.28.13, 960x540 clips of a planko
session (`human_planko_dropHazard_1g_FREE_120_261103105258.obs.dgz`, 133
trials, 10.7 MB):

- loading the session: 0.84 s (the `.ess` of an 11-trial door session:
  40 ms)
- a first trial: 3.1 s, of which about 1.2 s was relaunching stim2 at
  the rig's 120 Hz
- after that, 1.4 to 1.9 s a trial (600 to 800 rig frames, 5 to 7 s of
  trial); a 7 s trial's clip is about 400 KB
- prefetched trials answer at once, from the cache

## Not yet

- **The display before the first rig frame.** stim2 swaps only when
  something changes, so a trial's first rig frame can come after
  BEGINOBS (`obs_begin_ms` < 0): the rig was still showing an earlier
  frame, which a fresh setup does not have.
- **Running inside stim2.** The server drives stim2 only through
  `framelock::send`, which has an in-process path (`stimSend`); the plan
  is a general driver thread in stim2 (from `src/mirror.cpp`) and stim2's
  own web server in front, so a render host runs one program.
- **Assets by path** (shaders, images, fonts) must be installed where the
  rig had them, as for any replay.
