# Trial clips over HTTP: the render server

`tools/render/render_server.tcl` renders any trial of a recorded session
into a frame-locked movie, on a headless stim2 of its own, and serves it to
a browser with the timing that puts the clip on the trial's timeline. It is
the back end of a trial viewer: the page asks for a trial and plays the
clip, with its own traces and gaze drawn in step with it.

    dlsh tools/render/render_server.tcl                 # https://127.0.0.1:8140/ on a dserv host
    dlsh tools/render/render_server.tcl -host 0.0.0.0 -data /usr/local/dserv/work/obs
    dlsh tools/render/render_server.tcl -http           # http://127.0.0.1:8140/ regardless

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
| `POST /export` | a reel: trials with their gaze and captions, and title cards, rendered into one movie (below). Answers `{clip, cached, bytes}` when it is ready; `clip` is under `/clips/_exports/` |

`NAME` is a session's base name, with or without `.obs.dgz`,
`.trials.dgz` or `.ess`, so a viewer holding a trials DG passes its
`filename`. The server looks for `NAME.obs.dgz`, then `NAME.ess`, in its
data directories, then on each `-dserv` (below). Names with anything but letters, digits, `.`, `_` and
`-` are refused, and `/clips/` serves only `.mp4` and `.json` files inside
the cache.

`timing` is the clip's timing JSON (`docs/replay.md`): BEGINOBS and ENDOBS
on the clip's timeline, each rig frame's place and flip, the commands, the
rig's screen in degrees and pixels. Data timed from BEGINOBS is at
`t + obs_begin_ms` in the clip; a trials DG's `em_seconds`, which counts
from the period's first eye record, is at `t + em_zero_ms`.

dserv's `www/dg_viewer.html` is the viewer: with a server answering
(`?render=URL`, else the dserv's `df render_url` setting, else port 8140
on the page's host), its world panel plays
the selected trial's clip, the clip is the clock while it plays, and the
gaze is drawn over it (`www/js/stim2_view.js`). Its Sessions panel lists
the dserv host's sessions (`df`'s `list_sessions`: those whose obs file
is in the work directory) and opens them from dserv's read-only
`/data/trials/NAME.trials.dgz`. `df` deletes a trials file once it is
exported, so for a session without one the panel first asks `df` to
extract it again (`viewer_trials`, sent without waiting; refused while an
experiment runs). dserv also serves
`/data/obs/NAME.obs.dgz`, streamed in 64 KB pieces so its web loop keeps
serving datapoints meanwhile. A session stim2 cannot replay (a 422 here:
recorded before stim2 kept frame records) shows the viewer plugin's own
drawing, with the stim2 checkbox reading "stim2: no clip".

## Exports: reels for talks and papers

`POST /export` takes a reel as JSON and answers with one H.264 movie.
stim2 draws all of it: each trial frame-locked as for a clip, with the
gaze and a caption on its overlay layer (`docs/headless.md`, Overlay),
and title cards as text over the rig's background. Every segment is
rendered at the same size, frame rate and x264 setting, so ffmpeg joins
them without encoding again (concat demuxer, `-c copy`); segments that
differ (trials from rigs with different screens) are scaled, padded and
encoded once.

    { "fps": 60, "crf": 14,
      "items": [
        {"type": "card", "text": "Day 1", "dur": 2, "size": 0.07},
        {"type": "trial", "file": "NAME", "obsid": 12,
         "caption": "Trial 13", "caption_at": "nw",
         "gaze": {"t": [0, 0.002, ...], "x": [...], "y": [...]}},
        {"type": "card", "text": "Day 9", "dur": 1.5},
        {"type": "trial", "file": "NAME2", "trial": 41}
      ] }

- **trial**: `file` and `obsid` (0-based) or `trial` (1-based), as for
  `/trial`. `caption` (up to 500 characters) goes at `caption_at` (`nw`,
  `n`, `ne`, `sw`, `s`, `se`), `caption_size` of the screen's height a
  line (default 0.045), over a translucent box. `gaze` is the gaze to
  draw: `t` seconds from the trial's first eye record (a trials DG's
  `em_seconds` minus its first value, as dg_viewer draws it), `x` `y`
  degrees; without it no gaze is drawn. `gaze_style` `{size, color}`
  sets the disc.
- **card**: `text`, `dur` seconds (default 2), `size` of the height a
  line (default 0.07), `color`, `bg` (default the rig's background). A
  card draws on the stim2 of the nearest trial, so a reel needs one.
- Checked field by field before anything renders: a 400 says what is
  wrong. Text reaches stim2 only as data (list elements), never as Tcl.
- Rendered by the same worker as clips, ahead of prefetches, and held
  while the rig runs an experiment (503). `/status` shows it as
  `export/ID` with its progress.
- Cached by what went into it: the request, each session file, the stim2
  version and `-width`. The same request again answers at once.

The reel itself is `tools/lib/reellib.tcl`, usable without the server:
`reel::render SPEC OUT -sock CMD` with a Tcl dict spec, and
`reel::from_wire` for the JSON form. dg_viewer's **mp4** button (beside
stim2/gaze) exports the selected trial with the gaze it shows (blinks
left out when masked) and an optional caption.

## On the rig: the stim2-render service

A machine that runs dserv can render its own clips: a rig whose dserv
and stim2 share it (a Pi with its display, say), the dserv box of a rig
whose display is another machine, or a server. stim2 ships the unit,
not enabled. Installing the `stim2-render` package turns it on:

    sudo apt install ./stim2-render_VERSION_all.deb

(or from the dserv-agent panel, where it is an optional component, or
at setup with `?with=stim2-render`). The package depends on `stim2`,
`dlsh-interp` and `ffmpeg`; removing it turns the service off. Without
the package, `sudo systemctl enable --now stim2-render` does the same,
given `/usr/local/bin/dlsh` (the unit does not start without it) and
`ffmpeg`.

It listens on port 8140 of every address, reads
`/usr/local/dserv/work/obs`, and keeps clips in `/var/cache/stim2-render`.
dg_viewer finds it with no setting: with `df render_url` empty it asks
port 8140 of the dserv's host. Options go in `/etc/default/stim2-render`
(then `sudo systemctl restart stim2-render`):

    RENDER_OPTS="-width 640 -cache-max 1000"

or, without the package, in a drop-in (`sudo systemctl edit
stim2-render`; the settings file wins where both set `RENDER_OPTS`):

    [Service]
    Environment=RENDER_OPTS=-width 640 -cache-max 1000

Its log is `journalctl -u stim2-render`.

**Staying out of the experiment's way.** A render shares the rig's CPU,
and on a Pi its GPU and memory with the rig's own stim2:

- **The guard.** While the rig's `ess/status` is `running`, nothing is
  rendered. A viewer asking for a clip not yet made gets a 503 ("the rig is
  running an experiment") and plays on without it; a render under way stops
  where it is (between batches of 10 rig frames) and finishes, and the queue
  goes on, once the experiment stops. Cached clips are still served. The
  status comes from `-guard`'s dserv text port, by default this machine's
  (`localhost:2570`), asked at most once a second while there is work. A
  port with no dserv or no `ess` does not hold rendering (asked again every
  30 s); `-guard none` turns it off. A stim box with no dserv of its own
  names the rig's: `-guard 192.168.88.29:2570`.
- **Priority.** The unit runs it at `Nice=15`, `CPUSchedulingPolicy=batch`
  and idle disk priority, so dserv and the rest come first; the rig's stim2
  runs at FIFO priority, ahead of it regardless.
- **The cache's size.** `-cache-max` (default 2000 MB) bounds the cache: past
  it, the clips served longest ago go, down to 90%, after each render. A clip
  used in the last minute stays, and so does the copy of a session in
  memory.

On a Raspberry Pi 5 (hm0, 2026-11-09) a clip renders in about the trial's
own time at 960 pixels (a 7 s joystick trial: 7.4 s), so stepping forward
mostly finds the next clips ready; `-width 640` is quicker.

## HTTPS: Safari, and viewers on other machines

A dserv page served over https may only use an https render server:

- **Safari** blocks an https page from reaching any http server, even one
  on `localhost`.
- **Chrome** allows `http://localhost` from an https page (the viewer then
  fetches each clip into memory first), but not an http server on another
  machine.

The server speaks https when it has a certificate, on the same port, with
the same requests and answers. It uses dlsh's `tls` package, so it needs
dlsh 0.21.0 or later. Where the certificate comes from:

1. `-cert FILE -key FILE`, if given.
2. Otherwise `cert.pem` and `key.pem` in `$CREDENTIALS_DIRECTORY`, where
   systemd puts the files a unit's `LoadCredential=` lines name.
3. Otherwise dserv's own pair, `/usr/local/dserv/ssl/cert.pem` and
   `key.pem`, the files dserv itself looks for.
4. Otherwise http. `-http` also forces http.

When it finds a pair it can't use (a key it can't read, or a dlsh without
`tls`), it logs why and serves http. The log line before `listening on`
names the certificate it uses.

The certificate must be one the browser trusts and must name the host in
the URL the viewer uses:

- **On a dserv host** (the stim2-render service, or by hand), dserv's own
  pair is the right one, and it is found by default. The page came from
  that host's dserv, so its certificate already names the host, and any
  browser that trusts the dserv page trusts the clips.
- **A render host that is not the dserv host** needs a certificate of its
  own from the same lab CA, naming the address viewers reach it by. Give
  that https URL to the rig: `df render_url https://<render host>:8140`.

dg_viewer on an https page with no `render_url` tries
`https://<page host>:8140` first, then `http://`, so an https render server
on the dserv host needs no setting.

The stim2-render service runs as a dynamic user, which can't read dserv's
key when only root can. `LoadCredential=` hands the unit a copy, which the
server then finds by itself (`sudo systemctl edit stim2-render`):

    [Service]
    LoadCredential=cert.pem:/usr/local/dserv/ssl/cert.pem
    LoadCredential=key.pem:/usr/local/dserv/ssl/key.pem

## A rig whose dserv is elsewhere

A rig's dserv need not be the render host. With a Pi running dserv and a
stim box on the rig's own network, a lab machine runs the render server:

    dlsh tools/render/render_server.tcl -host 0.0.0.0 -dserv http://rpi500.local:2565

and the rig's `df render_url` setting (dserv's settings gear, or
`settings::put df render_url http://<render host>:8140 -persist` in `df`)
sends its viewers there.

A session not in the data directories is fetched from the dserv's
`/data/obs/NAME.obs.dgz` into `CACHE/remote/HOST_PORT/` with `curl`, and
loaded from there. The rig does no more than send the file, once.

- **Not waiting on the fetch.** The fetch is a child process, and each
  request runs in a coroutine of its own. A request waiting on a fetch
  holds up nothing else: renders, clips, `/status` (which lists the
  sessions being fetched) and other requests go on. Several requests for
  one session share its one fetch.
- **Changed files.** A copy checked in the last 30 s is used as it is.
  After that, a `HEAD` asks the dserv for the file's size and
  `Last-Modified`. A copy that matches stays; otherwise the file is fetched
  again. `curl -R` gives the copy the dserv file's mtime, which names its
  clips' directory, so clips survive a restart of either side.
- **Older dservs.** A dserv without `HEAD` on `/data/` gets a plain `GET`.
  Without `Last-Modified`, a download that matches the copy byte for byte
  keeps the copy, and with it the copy's mtime and clips.
- **A dserv that does not answer** leaves the copy from before in use. With
  several `-dserv`, they are asked in turn.

Errors are JSON `{error}` with 400 (a bad request), 404 (no such file),
422 (a file that cannot be replayed, with the reason), 500 (a render
that failed) or 503 (`busy: true`: the rig is running an experiment, so
the clip is not rendered now; the guard, above). Every answer carries `Access-Control-Allow-Origin: *`, so a
page served by dserv can use a server on another port or machine.

## Options

| option | |
|---|---|
| `-port P` | HTTP port (default 8140) |
| `-host A` | address to listen on (default `127.0.0.1`; `0.0.0.0` to serve other machines) |
| `-data DIR` | where sessions are (repeatable; replaces the defaults `~/data/obs`, `~/data/ess`, `/usr/local/dserv/work/obs`, those that exist) |
| `-dserv URL` | a dserv to fetch sessions from, `http(s)://host:port` (repeatable; asked after the data directories) |
| `-guard H:P` | the dserv text port whose `ess/status` holds rendering while `running` (default `localhost:2570`; `none` to render regardless) |
| `-cert FILE`, `-key FILE` | serve https with this certificate (PEM, chain included) and key, both or neither (dlsh 0.21.0 or later). With neither, systemd's credentials or dserv's own pair are used if found; see HTTPS above |
| `-http` | serve http even where a certificate is found |
| `-cache DIR` | where clips go (default `~/.cache/stim2-render`; `/var/cache/stim2-render` with no `HOME`) |
| `-cache-max MB` | the cache's size: past it, the clips served longest ago go (default 2000; 0 for no limit) |
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
  served. `-cache-max` bounds it (above).
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
