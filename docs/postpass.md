# Post passes: full-screen passes over the finished scene

A **post pass** is something a loadable module draws over the whole
framebuffer after the stim's scene is finished: a gaze-contingent window
(`gazewin`, [gazewin.md](gazewin.md)), noise, a colour transform, a
display encoding. stim2 knows nothing about what a pass draws. The seam
is `src/postpass.h`; the chain is `src/postpass.cpp`.

A pass is not a graphics object. It is not in the object list or a group,
so `resetObjList` and `glistInit` leave it alone, and it works over any
display without the stim's script knowing.

## What stim2 does, and what a module does

stim2 owns only what a module cannot do from outside:

- **Where passes run**: after the scene and its post scripts, before the
  overlay, the frame grabs and the frame sink. So a `frameGrab`, a
  `frameSink` movie and a live mirror's frames are what the subject saw,
  and overlay annotations stay on top.
- **Static displays**: when a pass says what it would show has changed,
  the main loop repaints: the passes again over the saved scene, no step
  and no frame scripts, late in the refresh like `stimLateDraw`. A repaint
  is a real frame (`SwapCount`, a `stim/frame` record, grabs, the sink,
  swap tags), at most one per refresh.
- **`frameRepaint`** (a mirror, a replay render) runs the passes again.
- **Flip times** of the frames a pass drew, for latency records.
- **Datapoint taps**: a forwarded dserv datapoint handed to C as it
  arrives, without a Tcl binding.
- **`postpass reset`**: every pass off and back to its defaults.
  `stim::prelude` runs it at each script load, so a pass one system turned
  on never carries into the next.

A module owns everything else: its shader, its textures, its settings,
its Tcl command, and what "changed" means for it.

## The chain

Passes are ordered by priority, lower first. Each draws over the
framebuffer as the one before left it, so a pass needs no knowledge of
the others: it copies what it finds and draws.

| priority | for |
|---|---|
| 100 | a pass that selects or degrades the scene (`gazewin`) |
| 100 to 900 | a pass that should apply to that result (noise, a colour transform; `shaderpass` defaults to 200) |
| 900 and up | a display encoding that must come last |

On a repaint, the first wanted pass draws again from the scene it kept
(`composite`), and the later ones are staged again over its new output,
since what they kept was its old output.

    postpass            ;# or `postpass list`: {name N priority P on 0|1} per pass, in order
    postpass reset

## Writing a pass

Most passes need no C: `shaderpass` ([shaderpass.md](shaderpass.md)) runs
any fragment shader as a pass, with uniforms set from Tcl. Write a module
only for what a single shader over the scene cannot do: an input that
must reach C as it arrives (the gaze), extra textures or render targets
(gazewin's blur pyramid), or its own idea of when a repaint is due.

A module fills a `stim_postpass_t` (static storage) and registers it in
its `_Init`:

```c
#include "postpass.h"

static int  wanted(void *d) { return on && prog; }

static int  stage(void *d, const stim_postpass_frame_t *f)
{
  /* copy f->drawfb (read buffer f->drawnbuf) into my texture, then draw
     my shader over f->drawfb; leave the GL state as found */
  return 1;
}

static int  composite(void *d, const stim_postpass_frame_t *f)
{
  /* draw again from my texture; 0 if I have none of this size */
}

static stim_postpass_t pass = {
  "noise", 200.0f, NULL,
  wanted, stage, composite,
  repaint_due,      /* or NULL */
  NULL,             /* late_ms */
  NULL,             /* flip */
  reset
};

int Noise_Init(Tcl_Interp *interp)
{
  ...
  gladLoadGL();
  stimPostpassRegister(&pass);
  Tcl_CreateObjCommand(interp, "noise", noiseCmd, NULL, NULL);
  return TCL_OK;
}
```

| callback | |
|---|---|
| `wanted` | 1 while the pass is on and able to draw. Called on hot paths: a flag test |
| `stage` | the scene is finished in `f->drawfb`: keep a copy and draw over it. Returns 1 if it drew |
| `composite` | draw again from that copy, with the pass's inputs as they are now. 0: stim2 redraws the scene. Optional |
| `repaint_due` | static display: 1 if what the pass shows has changed since it last drew; else 0, with `*wait_s` the seconds until it could change by itself (-1: only an input can change it). Optional |
| `late_ms` | the repaint's draw-late budget, or -1 to follow `stimLateDraw`. Optional |
| `flip` | the flip that showed a frame this pass drew, on the real and the wall clock. Optional |
| `reset` | `postpass reset`. Optional |

A pass that animates by itself (dynamic noise) returns 1 from
`repaint_due` every time: a static display is then repainted once per
refresh. A pass that depends only on its settings (a colour transform)
returns 1 when a setting changed since it last drew, and 0 otherwise.

`stimdlls/src/shaderpass.cpp` is the short worked example and
`stimdlls/src/gazewin.cpp` the full one. shaderpass keeps its uniforms in
the shader module's typed uniform tables (`shaderutils.c`);
`stimdlls/src/passutil.h` holds what the two passes share: a program from a fragment body, a full-screen triangle
from `gl_VertexID`, a shader file's `-- Fragment` and `-- Uniforms`
sections, and the GL state saved and restored around the draw (`GLSave`).

What stim2 exports to a module for this:

| | |
|---|---|
| `stimPostpassRegister`, `stimPostpassUnregister` | the chain |
| `stimDatapointTapAdd`, `stimDatapointTapRemove` | a named datapoint to a C function, as received: `"name type ts len {value}"` and the real clock |
| `stimClockSeconds` | the stim clock: the real one live, the stepped one in a mirror or a replay render. Age inputs on this one and a render that runs faster or slower than real time behaves as the rig did |
| `stimRealSeconds` | the real clock (`glfwGetTime`), for latency records |
| `stimLogMessage` | stim2's log |

A module must not call GLFW itself: it would reach its own copy of the
library, not stim2's. In `stimdlls/CMakeLists.txt`, `CORE_LIBS_ONLY` links
a module against Tcl and GL alone. On Windows the exports are listed in
`src/stim2.def`.

## Cost

With no pass wanted, stim2 asks each registered pass once per frame
whether it is wanted, and tests once per forwarded datapoint whether any
tap exists. A stim2 with no pass module loaded does neither.

## Tests

The seam is exercised through the two pass modules:
`tests/test_gazewin.tcl` (stage, `postpass list`, `postpass reset`),
`tests/test_gazewin_step.tcl` (`frameRepaint`),
`tests/test_gazewin_client.tcl` (the datapoint tap, the static-display
repaint, flip times) and `tests/test_shaderpass.tcl` (a chain of two
passes in either order, on a frame and on a repaint; passes created and
deleted; reset).
