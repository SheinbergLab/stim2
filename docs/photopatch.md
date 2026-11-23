# `photopatch`: a screen patch for a photodiode

A photodiode taped over a corner of the screen says when light actually
changed on the panel. `photopatch` makes that corner change on the frames
worth timing: a square that switches between a dark and a light level.
The diode is read by an extio box and timed by dserv's `photo` processor
(dserv `docs/photo_processor.md`), which publishes one `proc/photo/edge`
per change of light. Each frame that changed the patch has a
`stim/photopatch` record, so pairing an edge with its frame is an in-order
match of the two streams.

By default the patch changes on every frame a client changed the display
with a swap-acked (`!`) command, the way ESS shows each stimulus. Every
system then drives it with no change to the system.

It is a loadable module on stim2's post-pass chain
([postpass.md](postpass.md)) at priority 940: after the scene and any pass
that changes it (gazewin, shaderpass), before `tlock`'s row (950,
[tlock.md](tlock.md)).

**Off by default, and `postpass reset` (every script load) turns it off.**
A flashing corner distracts a subject: a rig turns it on only while a
diode is over it. Off, it costs a flag test per frame.

## Commands

    photopatch on ?marked|toggle|manual?
    photopatch off
    photopatch set ?-mode m? ?-corner tl|tr|bl|br? ?-size px? ?-inset px|{x y}?
                   ?-dark {r g b}? ?-light {r g b}?
    photopatch flip ?0|1?
    photopatch status
    photopatch reset

| option | default | |
|---|---|---|
| `-mode` | `marked` | which frames change it (below) |
| `-corner` | `tr` | top right: clear of `tlock`'s row (top left), and early in the scanout |
| `-size` | 60 | the square's side, pixels |
| `-inset` | 0 | pixels in from both edges of the corner; or `{x y}`: x from the corner's side edge, y from its top or bottom (`{20 60}` at `tr`: 20 in from the right, 60 down). Reads back as one number while the two agree |
| `-dark`, `-light` | `0` / `1` | `{r g b}` from 0 to 1, or one grey |

`set` with no options returns the settings; with some, changes them
(a refused value changes nothing) and returns the result. `on` starts
dark. The first frame after `on` counts as a change: the patch appearing
is an edge if the scene there was brighter.

## Which frames change it

- **`marked`**: a frame something is timing. That is a frame that
  releases the reply of a swap-acked command (`!`, which ESS's
  `rmtSend "!sample_on"` is), or delivers a swap tag (`swapTag`, a
  change the stim makes by itself, like a saccade-contingent
  displacement). However many replies and tags one frame carries, it
  changes the patch once. The frame's `!` replies carry its swapcount
  (with `@`, see [stimess.md](stimess.md)), as its `stim/photopatch`
  record does, so a command pairs with its patch change exactly.
  A `!` command that draws nothing has no frame: its reply comes back
  with no new swap, as it always has, and the patch does not change.
  Two marked frames in a row come faster than an LCD settles, so their
  edges can merge into one.
- **`toggle`**: every frame. Each new frame reaching the panel is an edge
  and a refresh stim2 missed is an edge that does not come: a check of the
  display pipeline, not of a stimulus. A static display is redrawn on
  every refresh while it is on.
- **`manual`**: only `photopatch flip`.

`photopatch flip` works in every mode: the next frame changes it, to the
other level or to the one given (`flip 1`; a frame already at that level
is not a change). It returns the level that frame will show.

A `frameRepaint` (a mirror between rig frames, the stepped imgui panel)
shows the last frame again: the patch is drawn at the level it had, and
nothing flips.

## The record

On the flip of each frame that changed the patch, `stim/photopatch` goes
to dserv on the frame log's stream (`stimFrameLog`, docs/stim-frame.md),
as int64s:

| | |
|---|---|
| `swapcount` | the swap, as its `stim/frame` record has it |
| `flip_wall_us` | the flip on the wall clock (-1 unknown) |
| `level` | now showing: 0 dark, 1 light |
| `serial` | which change since `on` |
| `ntags` | swap tags the frame delivered |
| `why` | 1 the patch appeared, 2 a marked frame, 3 toggle mode, 4 `flip` |
| `nacks` | `!` replies the frame released |

A light change (level 1) should come back as a rising edge, a dark one as
a falling edge. Nothing is recorded while the frame log is off.

`status` returns `on mode level armed settings frames changes marked
repeats not_drawn last_change_flip_s last_change_flip_wall_us`. A square
that does not fit the frame is not drawn, counted in `not_drawn`, and
logged once.

## On a rig

1. Tape the diode over the corner; set `-size` a little larger than its
   aperture so placement is not critical, and `-inset` if the bezel
   covers the edge.
2. Turn on dserv's `photo` gear section with the diode's group
   (dserv `docs/photo_processor.md`).
3. Turn on `photo patch` in the same gear section (and `patch_mode`,
   `patch_corner`, ... as needed). ESS sends `photopatch set ...;
   photopatch on` at each system load, right after the prelude's reset,
   and logs `stim/photopatch` beside `stim/frame`. By hand:
   `rmtSend "photopatch on"`. Watch `proc/photo/edge` while the system
   runs: one edge per `!` command that changed the display.
4. For a continuous check of the pipeline, `photopatch on toggle`: an
   edge every refresh, alternating in direction.

**Strobed backlights (Display++).** The diode sees a flash every refresh,
and a black patch has none. dserv's `photo` handles that in strobe mode
with `period_ms` set. A dark level a little above black (`-dark 0.1`)
keeps the flashes coming, so the period can be learned and a missing
flash means a missing frame.

The square is drawn with a scissored clear: the colour reaches the
framebuffer as given, with no blending and no sRGB encode, so a
calibrated LUT (or a Display++'s colour mode) sees exactly the values set.

## Tests

`tests/test_photopatch.tcl`, inside stim2 (headless, or windowed on the
Mac):

    stim2 --headless -w 320 -h 180 --port-base 5610 -f /abs/path/tests/test_photopatch.tcl

`tests/test_photopatch_acks.tcl`, a client against a running stim2, for
`!` commands (they only come from the message port):

    stim2 --headless -w 320 -h 180 --port-base 5610 &
    tclsh tests/test_photopatch_acks.tcl localhost 5612
