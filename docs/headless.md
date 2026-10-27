# Headless stim2, step mode and the frame sink

Three pieces that together let stim2 render a replay frame by frame, with
no display, into a movie:

- `--headless`: no visible window. stim2 draws into an offscreen
  framebuffer of exactly `-w` x `-h` pixels and nothing is swapped.
- `stepMode`, `stepClock`, `stepFrame`: the stim clock moves only when it
  is told to, and each `stepFrame` draws exactly one frame at a given time.
- `frameSink`: every presented frame as raw RGBA, to a file or a pipe
  (ffmpeg).

They are independent. Step mode and the sink work in a normal window too,
and a headless stim2 that is not stepped runs in real time like any other.

    stim2 --headless -w 1920 -h 1080 --port-base 5610 &

## Headless

| platform | context | needs |
|---|---|---|
| Linux | GLFW's null platform over Mesa's surfaceless EGL (`EGL_MESA_platform_surfaceless`) | access to a render node (`/dev/dri/renderD128`: the `render` group), or Mesa's llvmpipe. No X, no Wayland, no seat. |
| macOS | a hidden native window (macOS has no EGL) | a logged-in GUI session; not a server mode |
| Windows | a hidden native window | untested |

Either way the drawing goes to an FBO with RGBA8 colour and depth/stencil,
bound for the life of the process in place of the window's framebuffer. So:

- The size is exactly `-w` x `-h`, with no Retina or desktop scaling
  (`ScaleX`/`ScaleY` are 1). `ScreenWidth`/`ScreenHeight` are the same
  size, `-r` sets `RefreshRate` (60 by default), and the physical size
  keeps square pixels. A `~/.config/stim2` monitor file still applies, and
  a replay sets the rig's degrees itself.
- **Code that binds its own framebuffer must restore the previous binding,
  not 0.** stim2 and the stimdlls do (`mpv.c` was fixed for this).
- Not stepped, each frame is held for one refresh period of the real clock
  instead of waiting for vsync, so a dynamic display runs at `-r` Hz
  rather than as fast as the GPU allows.
- There is no flip: `swapStats` reports the `glfinish` strategy with no
  flip times, `stimLateDraw` refuses to enable, and there is no imgui
  overlay.
- `frameGrab`, `dumpRaw` and the sink read the FBO.

Startup prints the renderer, e.g. `[info] headless 640x360 @ 60 Hz,
renderer: Apple M3`. On Linux, check that it names the GPU (radeonsi,
etc.) and not `llvmpipe`, unless software rendering is what you want.

## Step mode

Times are ms on the tick clock: `StimTicksF`, `stimClock`'s `ticks_ms`,
and `stim/frame`'s `ticks_us` / 1000.

| command | |
|---|---|
| `stepMode ?0\|1? ?t_ms?` | `1`: the stim clock stops following real time. It starts at `t_ms`, or where it stands now. The timer thread stops, and stim2 draws no frame on its own: a redraw asked for meanwhile is held for the next `stepFrame`. `0`: back to the real clock and the normal loop, drawing any held redraw. Returns `enabled 0\|1 clock <ms> headless 0\|1`. |
| `stepClock ?t_ms?` | Move the clock forward without drawing, and do what the timer thread would have done on the way: refresh the stim times, kick a due time-based group, run the objects' timer functions. Returns the clock. |
| `stepFrame ?t_ms? ?flip_ms?` | One frame with the clock at `t_ms` (default: one `FrameDuration` on). A held redraw draws as is; otherwise a dynamic group steps its objects first, like the loop's update. The frame goes through the sink, `frameGrab` and `afterFrames` like any other. Its post-frame scripts and `stim/frame` record see the clock at `flip_ms` (default `t_ms`), where the rig's clock stood once its swap returned. Returns `kind redraw\|update\|static swapped 0\|1 swapcount <n> ticks <ms> sink_frames <n>`. |

The clock only moves forward: going back is an error, because `StimTime`
is unsigned and scripts assume time does not run backwards.

Everything that reads stim time follows the stepped clock:
- `StimTime`/`StimTicks` and their `F` forms;
- `getStimTime*`, which is what the stimdlls read (video, spine, shader,
  mesh, box2d, motionpatch);
- `resetStimTime` and `stimClock`;
- the ACK1 stamps and the `stim/frame` ticks.

swapsync and late-draw keep the real clock.

Commands and datapoints arrive and are evaluated as usual while stepped.
Only drawing waits for `stepFrame`.

### Replaying a rig's frames

`stim/frame` records the rig's frames: per swap, `ticks_us` and the
commands and datapoints applied by then (`cmds_applied`,
`ds_dispatched`). That is what a frame-locked replay needs. For each rig
frame k:

1. Send each command and inject each datapoint that the rig had applied
   before frame k, until the counts match its record. Move `stepClock` to
   each one's rig time first.
2. Call `stepFrame <eval> <flip>`, where `flip` is frame k's
   `ticks_us / 1000`. `eval` is when the rig's loop stepped and drew
   frame k, which was after frame k-1's flip and before frame k's. Frame
   k-1's flip is the natural choice. Late-draw moves it to
   `flip - budget`.
3. Show a gap in `swapcount` (a frame the rig dropped) by repeating the
   previous frame in the movie.

`tools/replay/replay.tcl` does not yet do this; it paces by time.

## Frame sink

    frameSink open FILE
    frameSink open "|ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s 1920x1080 -r 60 -i - -pix_fmt yuv420p out.mp4"
    frameSink            ;# open 0|1 target <t> width <w> height <h> frames <n> failed 0|1
    frameSink close      ;# ... and status <exit status>; waits for the command to finish

The sink takes every presented frame: the whole window (the FBO when
headless), as raw RGBA with rows top-down, alpha 255, and no header. It is
read just before the frame is presented, like `frameGrab`. The size is
fixed when the sink opens. A failed write (the reader exited) stops the
sink and sets `failed`; SIGPIPE is ignored, so it cannot kill stim2. A
sink still open at exit is closed, so ffmpeg finishes the file.

## In a container

`docker/headless/Dockerfile` builds a Debian trixie image. It contains:
- Tcl 9 and GLFW (null platform only, so there is no X or Wayland) from
  stim2's submodules;
- spine-c and box2d at the pinned commits;
- dlsh from its release .debs: `dlsh-interp` provides `dlsh.zip` and
  the `dlsh` interpreter, so `replay.tcl` runs inside the container;
- stim2 and the stimdlls.

It starts the way a Linux rig does, with
`-f /usr/local/stim2/config/linux.cfg`, which loads the stimdlls and runs
`screen_config`. On Linux stim2 sources no config unless given `-f`, and
dlsh is what defines `::pi`, which `screen_config` uses. The build context
is a directory holding `stim2/` and `dlsh/` side by side:

    docker build -f stim2/docker/headless/Dockerfile -t stim2-headless .
    docker run -d --name hl --device /dev/dri/renderD128 stim2-headless \
        -w 1920 -h 1080 -r 120 -f /usr/local/stim2/config/linux.cfg
    docker exec hl dlsh /path/to/replay.tcl FILE.ess

Mesa prints an `amdgpu: os_same_file_description` warning in a container
because the default seccomp profile blocks `kcmp`. The warning is harmless.

## Clients: one write per message

Send a message-port request as one write: the 4-byte length and the
payload together. Two writes on an unbuffered Tcl socket run into Nagle's
algorithm and the server's delayed ACK on Linux, which costs about 40 ms
per request. A stepped replay sends one request per frame, so this is the
difference between 48 ms and 1.6 ms a frame. `replay.tcl` and the test
clients write once.

## Measured

2026-10-27, dev Mac (M3), headless 640x360, driven over the message port
from dlsh:
- 120 `stepFrame` calls into an ffmpeg pipe took 276 ms, including one
  socket round trip per frame and H.264 encoding. That is about 2.3 ms per
  frame, and the movie has exactly 120 frames.
- Not stepped, a dynamic group ran 61 frames in 1 s.
- Stepped and idle, it drew 0 frames in 0.5 s.

`tests/test_headless_step.tcl` passes 29/29, and `test_frame_grab.tcl`
passes headless as well as windowed.

2026-10-27, Linux box 10.2.145.42 (Ryzen 9 9950X3D, its integrated Radeon),
in the container, with renderer `AMD Radeon Graphics (radeonsi, ...)` and
no display server:
- `test_headless_step` passes 29/29 and `test_frame_grab` 24/24.
- 1920x1080 `stepFrame` with readback to `/dev/null` takes 1.62 ms a
  frame. Into ffmpeg's default x264 it takes 5.58 ms a frame (600 frames,
  about 180 fps).
- `replay.tcl` (paced by time) played a whole motiondir session,
  `momo_joystick_motiondir_261026195800.ess`: 13 trials, 51 commands and
  1233 `ess/dial/pointer` records, at the rig's 1920x1080, 120 Hz and
  degree mapping. It finished with no errors and fed 3471 frames to the
  sink, with the motionpatch dots, the targets, the cursor and the
  feedback drawn as on the rig. The movie is shorter than the session,
  because stim2 swaps only when something changes; a frame-locked replay
  places frames on the rig's timeline.
