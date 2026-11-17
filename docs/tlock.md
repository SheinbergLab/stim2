# `tlock`: frame-locked triggers from a CRS Display++

A Display++ (like the Bits# inside it, and the older Bits++) watches its
video input for an unlock key: eight pixels of fixed colours. What follows
the key on that row is loaded into the monitor's digital outputs, the
DB25 lines and the trigger-out BNC, which the monitor then drives from its
own frame timing. CRS calls this T-Lock.

`tlock` draws that row. The result is a hardware edge at a fixed time
after a chosen frame reaches the panel, with no software between the
frame and the edge. Recorded on a dserv digital input, it splits the
delay from stim2's flip to the light into two parts:

| from | to | measures |
|---|---|---|
| `flip_us` in `stim/frame` | the trigger edge | the video link and the monitor's input stage |
| the trigger edge | a photodiode edge | the panel |

It is a loadable module on stim2's post-pass chain
([postpass.md](postpass.md)) at priority 950, after every other pass: the
row must reach the monitor exactly as written. Off by default, at the cost
of a flag test.

**Not yet measured on a monitor.** The packet is Psychtoolbox's, byte for
byte, and agrees with the Bits# Technical Manual (R07, chapter 12, on the
monitor's own USB disk under `Documentation`): the unlock key, the length
byte (addresses assigned, minus 1), the command at address 6, the mask at
7 and the 100 microsecond slots from address 8. The pass is tested for
what it draws. What the outputs do with it is still to be seen on a
scope: see "Bringing it up on a rig".

## Commands

    tlock on
    tlock off
    tlock set ?-row r? ?-x x? ?-slots n? ?-mask m? ?-command c? ?-idle word?
    tlock pulse bits ?-on ms? ?-dur ms? ?-frames n|always?
    tlock stop
    tlock toggle ?bits?
    tlock packet ?idle|pulse?
    tlock status
    tlock reset

- `tlock on`: every frame from now carries a packet. With no pulse armed
  it is the idle packet: every output at `-idle` (all low).
- `tlock pulse bits`: the next frame drawn carries `bits` (a 16-bit word,
  one bit per output) high from `-on` ms into the frame (default 0) for
  `-dur` ms (default 1), in steps of 0.1 ms. `-frames n` puts it on the
  next n frames; `-frames always` on every frame until `tlock stop` or
  another `tlock pulse`. Returns the pulse's serial number, from 1.
  Call it in the same script as the command that shows the stimulus, so
  both land on the same frame.
- `tlock stop`: disarm; the next frame carries the idle packet.
- `tlock toggle bits`: toggle mode, below. `tlock toggle 0` ends it.
- `tlock off`: stop drawing. If the last packet sent was a pulse, one more
  frame carries the idle packet first (`state closing` in the status).
- `tlock set` with no arguments returns the settings.

| setting | default | |
|---|---|---|
| `-row` | 2 | the row the packet is drawn on, from the top. Psychtoolbox's default |
| `-x` | 0 | its first pixel. Even: the device reads pixel pairs |
| `-slots` | 248 | 0.1 ms output slots in a packet, 1 to 248. The packet is 12 + 2 x slots pixels wide (508) |
| `-mask` | 0xFFFF | the output bits a packet may change |
| `-command` | 0 | the packet's command byte |
| `-idle` | 0 | the output word outside a pulse |

`tlock packet` returns the row as it would be drawn, R G B per pixel, for
checking against another encoder. `postpass reset`, which every script
load runs (`stim::prelude`), turns the pass off and restores the defaults:
a stim script that wants triggers says `tlock on` itself.

Under ESS a rig says it once instead: the setting `tlock enabled` in
dserv's settings gear. ESS then sends `tlock on` at every system load,
after the prelude, so stim scripts only call `tlock pulse`. On a rig
that has not set it the pass stays off, and a script shared between rigs
can test `[dict get [tlock status] on]` before arming a pulse.

`tlock toggle` in the same section turns toggle mode on at every load
too: a bit mask (`0x0001` for DOUT 0), 0 for off. With both set, every
session on the rig marks its frames with nothing in the stim script.

The rig also names the digital input the trigger output is wired to:
`tlock din`, the label of the extio DI line (or of a group
holding it). ESS then logs that line's edges in the data file beside
`stim/tlock`, and `tlock/din` says which datapoint they are. The section's light in the
gear (`tlock/health`) goes red when trigger frames are going out and no
edges are coming back.
Use a plain DI line for toggle mode: a chord group is settled by the
box's firmware, and edges one refresh apart may not survive that.

## Toggle mode: an edge for every frame

    tlock toggle 0x4000

Each frame stim2 draws then flips those outputs: high for one whole frame,
low for the next. The monitor repeats a packet until a new one arrives, so
the level changes only when a new frame reaches the panel:

- every edge on the output is a new frame, one refresh after its flip;
- a refresh stim2 missed is an edge that does not come;
- on a static display under a gaze window (`gazewin`,
  [gazewin.md](gazewin.md)), every repaint a gaze sample caused is an
  edge. `gazewin` draws first (priority 100) and the packet goes over its
  result, on a frame and on a repaint alike.

Toggle mode asks for no frames of its own: a static display with nothing
changing stays as it is, and the level with it. Each toggle frame sends a
`stim/tlock` record, so each edge has a `swapcount`: with `stim/gazewin`
for the same swap, that is the eye sample, the flip and the frame's
arrival at the panel, per frame. The digital input must stamp both
edges.

A pulse can run at the same time on other bits. `tlock off` and
`tlock toggle 0` leave the outputs at the idle word.

## What the monitor does with a packet

From the Psychtoolbox (`BitsPlusPlus.m`, `BitsPlusDIO2Matrix.m`) and
PsychoPy (`psychopy_crs/bits.py`) drivers:

- **One frame late.** A packet acts on the frame after the one it is
  drawn in. The offset is fixed, so it subtracts out; and the frame that
  carries the packet is the one whose `swapcount` the record has.
- **It repeats.** The monitor plays its last packet again on every frame
  until another replaces it. This is why the pass draws a packet on every
  frame while it is on, and why `tlock off` sends an idle packet first.
  If stim2 misses a refresh right after a pulse frame, the pulse plays
  twice: `missed` in `stim/frame` shows it.
- **Slots are 0.1 ms** from the start of the frame. At 120 Hz a frame is
  83 slots; the later slots of the default 248 go unused. PsychoPy sends
  `1 + 10000/Hz` slots (84 at 120 Hz); `-slots 84` makes the packet 180
  pixels instead of 508.
- **Bits.** Bits 0 to 9 are the DB25's digital outputs (DOUT 0 to 9).
  The trigger-out BNC is most likely `0x4000`: the Bits# Technical Manual
  (R07, chapter 12) gives it as decimal 16384 and as 64 in the green
  (high) byte, though one sentence there calls it "the 16th bit", which
  would be `0x8000`. `tlock pulse 0xC000` drives both; narrow it once
  with a scope. Bits 10 to 13 are ignored.
- **Levels.** The manual calls the outputs TTL-compatible: low is 0 to
  0.4 V, high is 2.6 to 5 V. Measure the high level before wiring one to
  a 3.3 V input.
- **The row is blanked.** A packet that starts at the far-left pixel has
  its whole row shown black by the monitor, so the subject sees a black
  line, not the packet. A black row 2 over a lit background is the
  quickest sign that the monitor read the packet.
- **Slots past the frame.** The manual says to define enough slots to
  fill the frame (`round(10000/Hz)`, 83 at 120 Hz) and, of Psychtoolbox's
  fixed 248, to leave the unused ones at 0. The idle and toggle packets
  here fill every slot with the level, so if a held level shows glitches
  at the frame boundary, try `tlock set -slots 84`.

## The packet

One row of pixels, R G B:

| pixel | R | G | B |
|---|---|---|---|
| 0 to 7 | the unlock key: 69 33 56, 40 230 208, 19 190 102, 119 84 207, 52 12 192, 233 108 172, 41 201 80, 183 124 221 |||
| 8 | 0 | 0 | pairs - 1 (slots + 1) |
| 9 | 6 | command | 2 (the digital outputs) |
| 11 | 7 | mask high byte | mask low byte |
| 13 + 2i | 8 + i | slot i high byte | slot i low byte |

Every other pixel of the packet is black. The encoder is
`stimdlls/src/tlock_packet.h`.

## The display path

The monitor finds the key only if every byte arrives as written:

- the monitor's native resolution, no scaling by the GPU or a compositor;
- an identity gamma ramp on the GPU (stim2 loads none; check nothing else
  has);
- no dithering by the GPU driver, 8 bits per channel, full-range RGB;
- the monitor in a video mode (Bits++, the standard colour mode, is the
  one this was written for), not its status screen or mass-storage mode.

The row is drawn before the overlay, so an overlay drawn over it breaks
the packet. A frame grab, a `frameSink` movie and a mirror's frames
include the row.

The subject sees that row as a black line across the screen (the monitor
blanks it). The manual suggests a black strip drawn over the top rows if
the line appearing and disappearing matters.

## The record: `stim/tlock`

With the frame log on (`stimFrameLog`, [stim-frame.md](stim-frame.md); ESS
turns it on and logs this datapoint), each frame that carries a pulse, and
every frame in toggle mode, sends one int64 datapoint:

| # | field | |
|---|---|---|
| 1 | `swapcount` | of the swap that showed the packet, as in its `stim/frame` record |
| 2 | `flip_us` | that flip, CLOCK_REALTIME microseconds; -1 if the swap sync cannot time flips |
| 3 | `bits` | the pulse's output word; 0 on a frame with no pulse |
| 4 | `on` | its start, in 0.1 ms slots |
| 5 | `dur` | its length, in slots |
| 6 | `serial` | which `tlock pulse` call; 0 with no pulse |
| 7 | `base` | the output word outside the pulse: the idle word, with the toggle bits at this frame's level |
| 8 | `toggle` | the frame's count in toggle mode, from 1; 0 when toggle mode is off |

A pulse with `-frames n` sends one record per frame; `-frames always`
sends one, for its first frame (in toggle mode, every frame). The outputs
change one refresh after `flip_us`, plus `on` for a pulse.

## Static displays

A pulse armed while nothing is animating makes the main loop draw the
scene again ([postpass.md](postpass.md), "Static displays"), without a
step or frame scripts when another pass can repaint it. The redraws stop
once the idle packet has followed the pulse. `-frames always` costs one,
since the monitor repeats the packet by itself.

## Measured on a rig

A Display++ (firmware 24/06/2015) at 1920x1080, 120 Hz, in Bits++ mode,
2026-11-17:

- the monitor reads the packet with the defaults (row 2, x 0, 248 slots):
  the row goes black;
- DOUT 0 (`0x0001`) on the DB25 pulses and toggles as asked, at close to
  5 V: a 3.3 V input needs a divider;
- in toggle mode a dynamic stimulus gives an edge per refresh, and a
  display the graphics card can only draw at half rate gives an edge
  every other refresh.

Not yet measured: the trigger-out BNC and its bit, and the edge times
against `flip_us`.

## Bringing it up on a rig

1. `tlock on` over a lit background: row 2 going black says the monitor
   read the packet. Then `tlock pulse 0xFFFF -dur 2 -frames always`, with
   a scope on the BNC: a 2 ms pulse every refresh. Nothing: check the display path above, then try `-slots 84`
   and `-row 0`.
2. Narrow `bits` to the one that drives the BNC.
3. `tlock pulse <bit>` (one frame): one pulse, not a train. A train means
   the idle packet is not arriving.
4. Wire the BNC to a dserv digital input and compare its edge with
   `stim/tlock`'s `flip_us`: the difference should be one refresh plus a
   constant, with jitter well under a millisecond.

## Checking over the monitor's serial port

`tools/dpp_serial.py` talks to the Display++ over its USB serial port
(Python, standard library only). It cannot read the monitor's digital
outputs, but it answers two questions without a scope:

The monitor ships with its USB port as a disk (`USB_MSD`), not a serial
port: `lsusb -v` shows one Mass Storage interface and there is no
`/dev/ttyACM*`. Serial needs `USB_MSD="OFF"` and `USB_CDC="ON"` in
`Firmware/Config.xml` on that disk, then a power cycle (the manual's
installation chapter). Keep a copy of the file first.

- `dpp_serial.py scan`, then `dpp_serial.py line 2`: the pixels of a
  video row as the monitor received them, with the packet decoded. An
  intact packet says the display path changed nothing. The monitor goes
  to its status screen to answer; `dpp_serial.py video` puts it back.
- `dpp_serial.py status 2`: the monitor's inputs (trigger in, DIN 0 to 9)
  with its own timestamps. With an output looped back to an input, this
  shows a pulse or a toggle arriving.

## Tests

`tests/test_tlock.tcl` runs inside stim2, headless or windowed: the
command, the packet's layout, the row on the frame byte for byte and
nothing else changed, pulses on one frame, several and always, toggle
mode, a packet
too wide for the frame, `tlock off` and `postpass reset`.
`tests/tlock_packet_check.cpp` checks the encoder against Psychtoolbox's
without a display: `ctest --test-dir build -R tlock`.
