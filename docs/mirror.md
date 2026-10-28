# A live mirror of a rig's display

`tools/mirror/mirror.tcl` makes a second stim2 draw what a rig's stim2
draws, frame for frame, while the session runs. The data comes from the
rig's dserv, not from the rig's stim2. The live gaze (`eyetracking/position`)
is drawn over it. The rig's stim2 never gets eye data, so the mirror
subscribes to the gaze itself.

The mirror's stim2 can be:

- **On a monitor**: fullscreen on a box's own display. This doc sets one
  up.
- **A video pipe**: `--headless` on a render node, with the frames going
  through `frameSink` to an encoder. This needs no change to the driver.
  It is the route to a browser view (see "Later" below).

    stim2 --port-base 5610 -w 960 -h 540 &                     # windowed, for a try
    dlsh tools/mirror/mirror.tcl -dserv 192.168.88.40

| option | |
|---|---|
| `-dserv H` | the rig's dserv (required) |
| `-host H`, `-port P` | the mirror's stim2 (default `localhost:5612`, a stim2 started with `--port-base 5610`). Port 4612 is a rig's own and is refused without `-live`. |
| `-wait MS` | the longest a frame waits for its commands (default 50) |
| `-eye NAME` | the gaze datapoint in degrees (default `eyetracking/position`; `off` for none) |
| `-trail MS` | gaze trail length (default 100) |
| `-late MS` | repaint the gaze this long before the next vsync (default 5) |
| `-report S` | log the frame counts and lag every S seconds (default 10) |
| `-verbose` | log every frame's commands |

## How it works

It is `replay.tcl -render` with dserv as the source. Both use
`tools/lib/framelock.tcl`, which holds the message-port client, the
`stim/cmd` and `stim/frame` decoders, setup, the frame planner and the
one-request-per-frame builder. See `replay.md`, "Frame-locked".

- **Subscriptions.** The driver registers with the rig's dserv (`%reg`,
  text lines back to a port of its own) and matches these:
  - `stim/script`, `stim/lib/*`, `stim/bind`, `stim/cmd`, `stim/frame` and
    `stimdg`;
  - the streams that `stim/bind` names, added as the table changes;
  - `ess/screen_*`, `ess/in_obs`, `ess/status`, `ess/rmt_connected`,
    `ess/system`, `ess/protocol` and `ess/variant`;
  - the gaze.
- **Joining.** It reads the current values with `%get`: the script, the
  libraries published with it (no more than 60 s before it, since an
  older load's stay in dserv), `stimdg`, the geometry and the bound
  streams. It then shows a "waiting for the next trial" screen. The trial
  under way was set up by commands the mirror never saw.
- **The boundary.** The mirror starts at the end of an observation period
  (`ess/in_obs` 1 to 0) or at a start (`ess/status` to `running`). It
  sets its stim2 up the way `configure_stim` does, in playback form:
  - the prelude, the libraries, the script and `stim_init`;
  - `stimdg`;
  - the rig's half-screen degrees and `ess/screen_bg`.

  Each bound stream's latest value goes in with the first frame. Setup
  takes 5–25 ms.
- **Each rig frame.** Each `stim/frame` record is one rig swap. The frame
  is drawn once its commands are in:
  - **Commands.** A command's ACK stamp names the frame that first showed
    it. The frame's `cmds_applied` advance says how many commands to
    expect.
  - **`!` commands.** A `!` command (`!fixon`) is answered after its swap,
    so its `stim/cmd` record arrives after the frame's. The frame waits
    for it, up to `-wait`. A command the rig applied that `stim/cmd` does
    not carry also ends the wait at `-wait`. ESS's raw queries are such
    commands. These waits are counted as "command waits ran out".
  - **Datapoints.** The `ds_dispatched` advance places them.
  - **The request.** The inputs, the gaze and
    `stepFrame <eval> <flip>`, on the rig's tick clock, go as one
    request.
- **Behind.** A 120 Hz rig showing motion makes frames faster than a
  60 Hz monitor can show them. Only the newest ready frame is drawn,
  after the inputs of all of them. The others count as "dropped".
- **The gaze between frames.** `overlay set gaze ...; frameRepaint` puts
  the gaze over the last frame. This happens once per refresh, `-late` ms
  before the next vsync. A present holds the driver until its vsync, so a
  repaint early in the interval would push a rig frame that arrives just
  after it to the following vsync. That cost about 11 ms at p50 and 27 ms
  at p95 before the repaint moved late.
- **Reloads and restarts.** `configure_stim` publishes an empty
  `stim/script` first and the new script last. The empty one sends the
  mirror to "waiting for the rig", and the new one to "waiting for the
  next trial". The swapcount going back means the rig's stim2 restarted,
  and the mirror waits for ESS to load again. If the mirror's own stim2
  goes away, the driver reconnects. If dserv drops the subscription, it
  registers again. It keeps no state beyond what it can read back.
- **The waiting screen** is a line of text from the `text` stimdll. While
  waiting, stim2 is not stepped. Each setup starts with `resetObjList`.

## The mirror box

Measured on an Intel N305 (`dserv-dev`, 192.168.88.16), Debian trixie,
with a 1920x1080 60 Hz monitor on HDMI. It follows office-stim
(192.168.88.50, 1920x1080 120 Hz), whose dserv is officepi
(192.168.88.40).

What it needs:

- **stim2 0.28.4 or newer** from its release `.deb`. The `.deb`'s
  postinst provides the `stim` user, `Xwrapper.config` and `linux.cfg`.
- **dlsh-interp 0.20.6 or newer.** This is the `dlsh` interpreter and
  `dlsh.zip`. The box had only `dlsh-dg`.
- **`stim` in the `video` and `render` groups.** The postinst adds both.
  The install adds the installing user too, and to `systemd-journal`.
- **chrony** for the lag numbers.
- Nothing extra for VAAPI. `intel-media-va-driver` and ffmpeg 7.1 were
  already installed.

Install, from a stim2 checkout (or its `tools/` copied to the box; the
`.deb` ships them in `/usr/local/stim2/tools` from the next release):

    sudo sh tools/mirror/install-mirror.sh 192.168.88.40

This does the following:

- installs dlsh-interp, unless it is already there at 0.20.6 or newer;
- copies `tools/{lib,mirror,replay}` to `/usr/local/stim2/tools`;
- writes `/etc/default/stim2-mirror` with `MIRROR_DSERV`. An existing
  file is kept and only that line changes. `MIRROR_ARGS` holds more
  options, and `MIRROR_REFRESH` sets a monitor rate.
- installs and enables two units, and disables `stim2.service` (all three
  use the X server on vt1):
  - `stim2-mirror-display.service`: `xinit` with
    `tools/mirror/start-stim2-mirror.sh`, which runs stim2 `-F` at the
    monitor's own refresh on ports 5610–5613, as `stim`. Unlike
    `stim2.service` it does not wait for chrony.
  - `stim2-mirror.service`: the driver, as `stim`, restarted if it exits.

Watch it:

    journalctl -fu stim2-mirror

    16:37:51 run: 21 frames drawn, 0 dropped (behind), 0 not received; lag flip->present
    ms p50 15.3 p95 16.0 max 16.0 (p50: flip->arrival 0.6, arrival->ready 0.7,
    ready->present 13.9); 0 command waits ran out; 0 errors; gaze 1777 repaints, 249 samples/s

## Lag

The lag runs from the rig's flip (`flip_us`, CLOCK_REALTIME on the rig)
to the moment `stepFrame` returns on the mirror, after its swap. It is
split into three parts:

- **flip to arrival**: the `stim/frame` record's trip through dserv;
- **arrival to ready**: waiting for the frame's commands;
- **ready to present**: the draw and the wait for the mirror's vsync.

Both hosts' clocks were within about 1 ms of each other on 2026-10-28
(a min-RTT probe over ssh: mirror minus rig +0.5 ms). The mirror runs
chrony against public NTP. office-stim reports NTP-synchronized.

2026-10-28, emcalib/9point on office-stim, mirror on the N305 at 60 Hz:

| | flip to present p50 / p95 / max | to arrival | to ready | to present |
|---|---|---|---|---|
| gaze repainted as samples came | 26 / 43 / 43.5 ms | 8–10 ms | | |
| gaze repainted late in the refresh (now) | 15.3 / 16.0 / 16.0 ms | 0.6 ms | 0.7 ms | 13.9 ms |

Across the 2026-10-28 runs (6 setups on the Mac and the N305), there were:

- 0 frames dropped or missing;
- 0 command waits that ran out;
- 0 errors;
- 249 gaze samples/s, repainted at the display's 60 Hz.

The dev Mac, in a 960x540 window, showed 5–8 ms. That figure is
uncorrected for its clock, which was 1.5 ms behind.

What is left is the mirror's own refresh: a frame waits for the next
vsync of a 60 Hz panel. A faster monitor shortens it. When a 120 Hz rig
moves, half its frames are dropped as "behind" at 60 Hz. That is
expected and reported.

Checked on the dev Mac and on the N305:

- **A fresh join.** The mirror waits, then draws from the next start.
- **Joining while a trial ran** (`in_obs` 1). It drew from that period's
  end.
- **`ess::reload_system` during a session.** The mirror went waiting,
  then ready, then ran again at the next start.
- **On the box's monitor**, the mirror is fullscreen, with the fixation
  target stepping through the 9 points and the gaze over it.

- **A reboot of the box.** It came up into the mirror, joined, and
  followed the next session: 13 frames, 0 errors. Xorg started about 65 s
  after boot. The display unit still waited on `chronyc waitsync` then,
  as `stim2.service` does, and the console showed meanwhile. The unit no
  longer waits.

After that boot, chrony had the box 0.34 s behind and was slewing slowly,
so flip→present read −322 ms. A record cannot arrive before its flip, so
the report now says when the clocks disagree by more than 5 ms. Until
they agree, only ready→present (13.5 ms, timed on the box alone) is a
lag. `makestep` in `chrony.conf`, or PTP (below), would close this after
a boot.

Not yet tried live:

- the door protocol (bound streams, continuous motion);
- a restart of the rig's stim2 process.

## VAAPI

The N305's Quick Sync encodes from RGBA, using
`-vf hwupload,scale_vaapi=format=nv12`. That is the first setting that
`tools/mirror_bench/mirror_bench.sh`'s probe tries, and `-low_power 1`
also works. A 1080p60 test source runs at about 200 fps, input generation
included:

- `h264_vaapi` at 4 Mb/s: 600 frames in 3.0 s;
- `mjpeg_vaapi`: 600 frames in 3.0 s.

HDMI needs no encoding. The encoders are for the video pipe.

## Later

- **The mirror as `stim/mirror` JPEG datapoints** (`DSERV_JPEG`) for
  `ess_control.html`'s Stimulus Display panel, with
  `EyeTouchVisualizer.js`'s eye and window layer over it. This would
  retire per-protocol `set_viz_config`. It needs a headless mirror stim2
  with a sink beside `src/framelog.cpp`'s writer, or `frameSink` into
  `mjpeg_vaapi`. The driver stays as it is.
- **PTP.** The N305 has hardware PTP and already runs `ptp4l` as a
  grandmaster for extio. Its system clock follows public NTP. Syncing it
  with the rig over PTP would make the lag numbers exact to microseconds.
