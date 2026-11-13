# `gazewin`: a gaze-contingent pass over any display

Off by default. When on, every frame stim2 shows is the scene the stim
drew, passed through one full-screen fragment shader that knows where the
eye is. The built-in shader gives a **window** (only the region around
gaze shows) or a **scotoma** (that region is hidden), with a soft or hard
edge, a mask that hides, dims or blurs, and a policy for lost gaze. A
custom shader file can do anything else with the scene and the gaze.

The pass runs after the stim's scene and its post scripts, so it needs no
knowledge of the stim: polygons, Box2D worlds, shader objects, video and
text are all just pixels to it. It does not live in the object list or a
group, so `resetObjList` and `glistInit` leave it alone.

## Quick start

    gazewin source eyetracking/position          ;# subscribes, like stim::bind
    gazewin set -mode window -radius 4 -edge 0.5 ;# degrees
    gazewin on
    ...
    gazewin off

A stim script that uses it sets the source and settings when it loads and
turns the pass on and off at the states that need it (ESS sends `gazewin
on` with the rest of its stim commands). Every script load resets it:
`stim::prelude` runs `gazewin reset`, so a pass one system turned on can
never carry over into the next system.

## Where the gaze comes from

`gazewin source NAME` names a dserv datapoint: two numbers, x and y in
degrees with the origin at the screen centre and y up. That is
`eyetracking/position` (two `DSERV_FLOAT`s) on every eye source in
`dserv/config/emconf.tcl`, and what `virtual_subject_eye.tcl` writes for a
desk test. stim2 subscribes to it through `qpcs::dsStimAddMatch` when
`::dservhost` is set, as `stim::bind` does; the command returns 1 if it
did.

- **Live**: each forwarded sample is decoded in C in `processDSCommands`,
  without a Tcl binding. The Tcl side still sees it (`::dsVals`, and a
  script's own `stim::bind` of the same name still works).
- **Replay**: `stim::inject` hands the source's datapoints to `gazewin
  datapoint`. Replay feeds the streams named in the session's `stim/bind`
  table; the gaze source is not one of them (being listed there would
  change how ESS logs it), so pass it: `replay ... -stream
  eyetracking/position`. The session must have logged that stream.
- **Tests**: `gazewin point x y` holds a fixed gaze (never stale) until
  `gazewin point off`.

A value can be base64 floats or doubles (dserv's wire form, by type 2 or
3) or plain text `"x y"`. A non-finite sample (NaN) counts as lost.

## Command

    gazewin                      ;# status dict (same as `gazewin status`)
    gazewin on | off
    gazewin reset                ;# off, no source, no point, defaults, built-in shader
    gazewin source ?NAME?        ;# "" clears; returns 1 if subscribed with dserv
    gazewin set ?-option value ...?   ;# no options: the settings dict
    gazewin point x y | off
    gazewin uniform NAME ?v1 ?v2 v3 v4??   ;# a custom shader's uniform; no values: read
    gazewin shader ?PATH | default?
    gazewin datapoint NAME TYPE TS LEN VALUE   ;# one sample, as stim::inject has it
    gazewin stats_reset

`gazewin on` builds the shader (an error, and the pass stays off, if it
does not compile). `on`, `off` and `shader` redraw; other changes show on
the next frame or repaint.

| option | default | meaning |
|---|---|---|
| `-mode` | `window` | `window`: the region within `-radius` of gaze is untouched, the rest treated. `scotoma`: the reverse |
| `-radius` | 5.0 | degrees |
| `-edge` | 0.5 | degrees: width of the soft edge, centred on the radius; 0 is a hard edge |
| `-color` | `background` | the mask colour: `background` or `{background a}` (stim2's background, tracking `setBackground`, at alpha `a`, default 1) or `{r g b ?a?}` 0..1. Its alpha is how strongly the treated region is pulled to it: 1 hides, 0.5 halves the contrast toward it, 0 leaves only the blur |
| `-blur` | 0 | mip levels of blur in the treated region (0 none; each level halves the resolution). Box-filtered mips, trilinear: a cheap blur, not a Gaussian |
| `-lost` | `mask` | while gaze is lost: `mask` treats the whole screen, `show` shows the scene untouched, `freeze` keeps the last position |
| `-hold` | 100 | ms: a sample older than this is lost (0: never) |
| `-late` | -1 | ms before the next vblank to repaint a static display; -1 follows `stimLateDraw` (its budget while it is on, else repaint at once) |

Status (`gazewin`) adds: `on`, `source`, `subscribed`, `shader`,
`point`, `samples` and `invalid` (counts), `gaze` (the last sample),
`age_ms`, `gaze_lost`, `stages` (frames drawn with the pass), `repaints`
(static-display repaints), and two latency records, each `{n p50 p95
max}` in ms over the last 1024 passes:

- `arrival_to_flip_ms`: from the sample reaching stim2 to the flip that
  showed it. Local clocks only.
- `sample_to_flip_ms`: from dserv's stamp to the flip, on the wall clock.
  Needs flip timestamps (a rig) and dserv's clock disciplined with
  stim2's (same box, or chrony/PTP).

Neither includes the camera and tracker latency before dserv; measure the
whole chain once with a photodiode.

## How it draws

Each frame, after the scene: the drawn framebuffer is copied into a
texture (one blit; mipmaps too when `-blur` > 0), and one triangle
covering the screen is drawn with the pass shader, which writes every
pixel. Then, as before, the overlay, the frame grabs, the frame sink and
the imgui panel. So a `frameGrab`, a `frameSink` movie and a mirror's
frames are what the subject saw; overlay annotations stay on top.

A pixel the pass leaves untouched is read with `texelFetch` and written
back unchanged: inside a window the scene is exact.

**Static displays.** A display that is not animating has no frames to
carry a new sample. While the pass is on, a pass of the main loop that
draws nothing checks whether the pass would now show something different
(a new sample, gaze lost or regained, a setting) and, if so, repaints: the
pass again over the saved scene, then present. No step and no frame
scripts, but it is a real frame: `SwapCount` advances, it has a
`stim/frame` record, and grabs, the sink and swap tags see it. A repaint
waits until `lastFlip + period - budget` (see `-late`), so the newest
sample makes the flip, and at most one happens per refresh however fast
samples arrive. With `-hold`, the loop also wakes when the last sample
goes stale, so a tracker that stops sending masks the screen by itself.

A dynamic display draws every frame anyway; the pass uses the newest
sample at draw time, which with `stimLateDraw` on is just before the
vblank.

**Not covered**: groups in `SWAP_ONLY` or `NO_SWAP` mode draw outside the
normal path and are not passed.

## Custom shaders

    gazewin shader /path/to/my_pass.glsl

The file uses the layout of stim2's `shaders/` directory: `-- Vertex`,
`-- Fragment` and `-- Uniforms` sections. Only the fragment is required (a
file with no section markers is all fragment); the default vertex shader
draws the full-screen triangle. A source without its own `#version` gets
the platform's (`#version 150`, or `#version 300 es` with highp precision).
`-- Uniforms` lines give defaults, `name v1 ?v2 v3 v4?` (values may be
quoted), and `gazewin uniform` changes them live.

The pass sets these uniforms when the shader declares them:

| uniform | type | |
|---|---|---|
| `scene` | `sampler2D` | the scene (unit 0); `texelFetch(scene, ivec2(gl_FragCoord.xy), 0)` is the pixel as drawn |
| `resolution` | `vec2` | framebuffer size, pixels |
| `halfdeg` | `vec2` | half the screen in degrees (`HalfScreenDegreeX/Y`) |
| `gaze` | `vec2` | degrees, origin at the centre, y up |
| `valid` | `int` | 1, or 0 while gaze is lost |
| `age_ms` | `float` | age of the sample |
| `time` | `float` | seconds (glfwGetTime) |
| `radius`, `edge`, `blur` | `float` | from `gazewin set` |
| `mode`, `lostmode` | `int` | 0 window / 1 scotoma; 0 mask / 1 show |
| `maskcolor` | `vec4` | from `-color` |

A pixel's position in degrees is
`(gl_FragCoord.xy / resolution * 2.0 - 1.0) * halfdeg`. A shader that
fails to compile is refused with the compiler's message and the one in
use stays.

## Cost when off

Nothing is created until `gazewin on`. Off, stim2 does one flag test per
frame and one per forwarded datapoint (whether a source is named), and
nothing else; the main loop, late-draw, the overlay and the frame record
are as they were. `tests/test_gazewin.tcl` checks that after `gazewin
reset` a frame is byte-identical to one drawn before the pass was ever
on, and frames from polygon, overlay, blank, shader-object, dynamic and
background-change displays were byte-identical between this build and
the one before it, headless and in a window.

## Tests

    stim2 --headless -w 320 -h 180 --port-base 5610 -f /abs/path/tests/test_gazewin.tcl
    stim2 -w 640 -h 480 --port-base 5610 -f /abs/path/tests/test_gazewin.tcl

    stim2 --headless -w 320 -h 180 --port-base 5610 &
    tclsh tests/test_gazewin_client.tcl localhost 5612 5611

The first checks pixels from inside stim2. The second plays dserv's role
on the datapoint port and checks the live path: samples decoded in C, the
repaint of a static display and its rate, the stale-sample wake-up, the
latency record, and that `reset` stops it all.

Not yet exercised: the GLES build (Raspberry Pi); the late repaint
timing, which needs flip timestamps (a Linux rig; a Mac has none and
repaints at once); the Linux and Windows builds; a live mirror and a
replay of a session that used the pass; and a real eye tracker.
