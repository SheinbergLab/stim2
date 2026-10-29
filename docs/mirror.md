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

    dlsh tools/mirror/mirror.tcl -dserv 192.168.88.40 -launch     # a window, for a try

or with a stim2 of your own:

    stim2 --port-base 5610 -w 960 -h 540 &
    dlsh tools/mirror/mirror.tcl -dserv 192.168.88.40

The tools ship with stim2: `/usr/local/stim2/tools` from the Linux .deb,
and from 0.28.8 `stim2.app/Contents/Resources/tools` in the Mac app
(without the Linux setup files), so no checkout is needed:

    dlsh /Applications/stim2.app/Contents/Resources/tools/mirror/mirror.tcl -dserv HOST -launch

### One program: `stimMirror` and `stim2 --mirror`

Any stim2 can become a mirror and go back, with no restart:

    stimMirror 192.168.88.40 ?options?   ;# mirror that rig (mirror.tcl's options)
    stimMirror                           ;# status: running, dserv, mode, load, lag
    stimMirror stop                      ;# back to an ordinary, unstepped stim2
    stimMirror discover ?ms?             ;# the dservs on this link, by mDNS

- **`stim2 --mirror "HOST ?options?"`** is `stimMirror` run once the
  startup files have.
- **The imgui panel** (right-click) has a "Mirror a rig" section. It
  lists the rigs found by mDNS and has a host field, Mirror and Stop
  buttons, and the status. While mirroring, the panel stays live: gaze
  repaints draw it, and when the rig is idle a stepped stim2 shows it
  again every 50 ms.
- **The ESS guard.** A stim2 that ESS has configured for a rig is that
  rig's display: its live prelude set `::dservhost`. `stimMirror`
  refuses it, because ESS's commands and the mirror's would interleave.
- **After `stop`,** the stim2 is unstepped, with the overlay cleared and
  in playback (nothing subscribed). The next `configure_stim` from ESS
  starts from its usual clean slate.
- **Discovery.** Every dserv advertises `_dserv._tcp` through the host's
  mDNS responder (dserv `docs/discovery.md`). stim2 browses with the
  system's own tool: `dns-sd` on a Mac, `avahi-browse` (avahi-utils) on
  Linux. `tools/mirror/discover.tcl` does it standalone too.


    stim2 -w 960 -h 540 --port-base 5610 --mirror "192.168.88.40 ?options?"

This runs the same `mirror.tcl` inside stim2, in a thread of its own with
its own Tcl interpreter: the shape of a dserv subprocess (`src/mirror.cpp`).
The build embeds `tools/lib/framelock.tcl` and `tools/mirror/mirror.tcl` in
the binary, so the driver always matches the stim2 it runs in.
`$STIM2_TOOLS` names a `tools/` directory to source instead, for working on
the driver.

- **Isolation.** The thread's interpreter has its own dserv subscription,
  state and timers, and runs the driver's event loop. stim2's interpreter,
  whose `::dsCmds` and bindings a rig reload clears, sees only the
  requests any mirror sends: setup, frames and gaze.
- **`stimSend`.** Requests go by `stimSend`, dserv's subprocess `send`
  pointed at the render thread. A request joins the queue message-port
  commands use and runs at the same point in the loop, so stepping
  behaves exactly as with the separate driver.
- **Quitting,** by the window or by `exit`, stops the thread and joins it.
- **Options** are `mirror.tcl`'s, without `-host`, `-port`, `-live` or
  `-launch`.
- **Without `--mirror`** none of this runs.

Integrated or separate:

- **Integrated:** one program to start and stop, and no `dlsh` interpreter
  to install. It saves one localhost hop per request.
- **Separate:** a crash in a C extension the driver loads cannot take the
  display down. systemd restarts just the driver. The driver can be run
  by hand with `-verbose`, or from another machine.

Both run the same file.

`-dserv localhost` mirrors a rig whose dserv runs on this machine. The
mirror's stim2 must not be the one ESS drives: ESS uses 4612, the mirror
5612.

| option | |
|---|---|
| `-dserv H` | the rig's dserv (required) |
| `-ws`, `-wss` | subscribe over dserv's WebSocket from the start (`-wss`: with TLS, needs the Tcl `tls` package); without them, the WebSocket is used when dserv cannot register the mirror |
| `-wsport P` | dserv's WebSocket port (default 2565, its mDNS TXT `web`) |
| `-launch` | start the mirror's stim2 as well: the installed one, on `-port`'s base, in a window in the rig's aspect (`ess/screen_w` x `_h`, no wider than 960). It is stopped when the driver exits, and the driver exits when it is closed. `-size WxH`, `-fullscreen` and `-stim2 PATH` adjust it. |
| `-host H`, `-port P` | the mirror's stim2 (default `localhost:5612`, a stim2 started with `--port-base 5610`). Port 4612 is a rig's own and is refused without `-live`. |
| `-wait MS` | the longest a frame waits for its commands (default 50) |
| `-eye NAME` | the gaze datapoint in degrees (default `eyetracking/position`; `off` for none) |
| `-trail MS` | gaze trail length (default 100) |
| `-hold MS` | keep the gaze up this long without a new sample (default 500) |
| `-touch on\|off` | draw ESS's touches (`ess/touch_press`, `_drag`, `_release`) on the overlay (default on) |
| `-late MS` | repaint the gaze this long before the next vsync (default 5) |
| `-report S` | log the frame counts and lag every S seconds (default 10) |
| `-verbose` | log every frame's commands |

## Reaching a dserv: call-back or WebSocket

The mirror subscribes in one of two ways:

- **dserv's call-back (`%reg`).** The default. The driver asks dserv, on
  4620, to send it datapoints, and dserv opens a connection back to the
  driver for them. This works wherever dserv can reach the console: a lab
  LAN.
- **dserv's WebSocket** (`/ws`, port 2565, the way `ess_control.html`
  subscribes). Everything travels on one connection the mirror opens, so
  it works where dserv cannot reach the console: NAT, a VPN, a cloud
  dserv. Requests are JSON (`subscribe`, `get`, `eval dservKeys`).
  Datapoints are pushed as JSON and turned back into the call-back form,
  binary values to their exact bytes.

The driver tries the call-back first. If dserv cannot register it, either
because it cannot connect back or because 4620 does not answer within 3 s,
the driver moves to the WebSocket by itself. `-ws` uses the WebSocket from
the start.

**TLS.** A dserv with certificates (`ssl=1` in its mDNS record) serves its
WebSocket only as `wss://`. That needs the Tcl `tls` package, which
`dlsh.zip` does not have yet. Until it does, `-wss` reports so, and the
WebSocket path reaches only dservs without TLS. The call-back path has no
TLS.

dserv's address is looked up once per join: a `.local` name has cost 5 s
a lookup on some networks.

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
  60 Hz monitor can show them. Every ready frame is still computed, in
  the same request: updated and drawn, with its scripts run, by
  `stepFrame -nopresent` (stim2 0.28.7). Only the newest is shown, and the
  others count as "computed but not shown". This matters to a script that
  latches state in a particular frame: door sets its run's time zero in
  its first run frame. With that frame skipped, as before 0.28.7, the
  whole physics timeline shifted a frame, and a crash could land after
  `!feedback`. A mirror on an older stim2 computes only the newest
  frame.
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
- **The gaze** is placed on dserv's timestamps. Between trials, while
  ESS is busy, dserv has held `eyetracking/position` back by about 250 ms
  and then sent it in a burst. Timed by arrival, with a 100 ms window, the
  disc blinked. Now the trail is the path the samples traced, and the
  disc stays up until nothing has arrived for `-hold` ms (500 ms). The
  report counts hides. A frame request slower than 100 ms is logged.
- **A rig without flip times** (a Mac: its swap sync is glfinish) records
  a `!` command's flip as null. Such commands are recognised by their `!`.
  Before, they were placed a frame late after a 50 ms wait ("command waits
  ran out" on every one). The lag is then measured from dserv's receipt of
  the frame record, and the report says so.
- **Lag on a rig without flip times** is measured from dserv's
  timestamp, on dserv's clock. A dserv whose clock has drifted from this
  machine's adds a constant: 51 ms against a local dserv on a Mac that had
  been running for hours, the same on any thread or process.
- **Touch,** like the gaze, is on the overlay: a disc where the finger
  is from press to release (following a drag), then a fainter one at the
  release point for 300 ms. The mirror reads ESS's pixel touches
  (`ess/touch_press`, `_drag`, `_release`), published for every touch,
  and maps them to degrees by the rig's screen geometry. The `_deg`
  versions exist only inside observation periods, and only after a
  protocol has set up touch windows.
- **What the overlay shows** is switched in the imgui panel while
  mirroring (Gaze, Touch). The switches set
  `::stim2_mirror_show(gaze|touch)`, which the driver's overlay commands
  check where they run, so a switch needs nothing from the driver. The
  mirror otherwise stays the stimulus and what is in its space: trial
  counts and outcomes stay in `ess_control.html`.
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

**Frame-script timing (found on door, 2026-10-28, fixed).** A frame script
that integrates its own physics on `$::StimTimeF` sees, on the rig, the
clock at the moment the loop ran that frame's update. That moment falls a
jittery fraction of a millisecond after the previous flip. A replay that
assumed the previous flip could take a physics step one frame later than
the rig did. Door steps at dt = 1/240 s, two steps per 120 Hz frame, and a
crash taken a frame late arrives together with `!feedback`, which ends the
run first, so the reveal had no X.

Three changes fix it (the third covers a mirror slower than its rig):
- stim2 0.28.5 holds the clock for each frame and logs the instant as
  `stim/frame`'s `eval_us` (see `stim-frame.md`). Replay and mirror step
  each frame at it.
- `door_stim.tcl` now rounds its per-frame step count, so jitter of less
  than half a step cannot move a step between frames. It also times presses
  on the stim clock (`stimClock`) rather than the wall clock, which in a
  replay or mirror would be the mirror's own.
- stim2 0.28.7's `stepFrame -nopresent` lets the mirror compute every rig
  frame it falls behind on and show only the newest (see "Behind").

The check on the dev Mac stood in for the rig. A real-time stim2 ran door
trials 1-10 of `human_runner_door-preview6-go_261027144820.ess`: commands
went to it with `@` ACK stamps, presses to its dataserver port, and its
`stim/frame` was recorded. A stepped stim2 then replayed the records, and
each trial's outcome and press steps were compared.

| rig | outcomes match | press steps match |
|---|---|---|
| stim2 0.28.4, the recorded door script | 10 / 10 | 0 / 11, off by 2–4 steps (wall clock); the replay also took 2 presses the rig did not |
| this stim2, the fixed door script | 10 / 10 | 11 / 12; one press 1 step late |

The press that remains is timed by the datapoint's dserv timestamp on the
rig clock, an estimate good to about a millisecond. A press within that of
a step boundary can still land a step apart.

Further runs of the same check:

- **Door script fixes alone.** With replay evaluating at the previous flip
  and ignoring `eval_us`, all 10 trials matched on outcome and press steps.
  The door script fixes did the work on door. `eval_us` matters where a
  frame's instant is far from the previous flip (late-draw rigs compute at
  flip - budget) or where a script is not hardened against jitter.
- **Skipped frames.** A replay skipping every other frame, as a 60 Hz
  mirror of a 120 Hz rig did, matched 4/10 trials. Door's run time zero
  was set a frame late, and every press step shifted.
- **Every frame computed, every other one shown** (`-nopresent`): 10/10.

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
