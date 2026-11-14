# `shaderpass`: a fragment shader over the finished scene

`shaderpass` runs a fragment shader over every frame stim2 shows: noise
added to the display, a colour transform, a channel swizzle, anything
that is a function of the finished scene. The stim does not change, and
polygons, Box2D worlds, shader objects, video and text are all just
pixels to it.

It is a loadable module (`stimdlls/src/shaderpass.cpp`, loaded with the
other stimdlls by `stim2.cfg`). Each pass is a post pass in stim2's chain
([postpass.md](postpass.md)), so several can be on at once, in a chosen
order, with or without the gaze window ([gazewin.md](gazewin.md)).

## Quick start

    shaderpass create nz -file noise
    shaderpass uniform nz amount 0.15
    shaderpass on nz
    ...
    shaderpass off nz

`-file` is a path, or a name looked for along the shader module's search
path (`shaderGetPaths`, set with `shaderAddPath` / `shaderSetPath`): in
each directory and in a `passes/` directory under it, with or without
`.glsl`.

Two shaders ship in `shaders/passes/`:

| file | does | uniforms |
|---|---|---|
| `noise.glsl` | uniform noise added to the scene, a new field every frame | `amount` (peak deviation, 0..1), `grain` (pixels per element), `mono` (1: luminance noise, 0: each channel its own), `rate` (fields per second, 0 freezes) |
| `colormatrix.glsl` | each output channel is a weighted sum of the scene's r, g, b plus an offset | `red`, `green`, `blue`: four numbers each, `{r g b offset}` |

The colour matrix covers the common transforms:

    shaderpass create cm -file colormatrix
    # swap red and blue
    shaderpass uniform cm red 0 0 1 0;  shaderpass uniform cm blue 1 0 0 0
    # invert
    shaderpass uniform cm red -1 0 0 1; shaderpass uniform cm green 0 -1 0 1; shaderpass uniform cm blue 0 0 -1 1
    # grey
    foreach c {red green blue} { shaderpass uniform cm $c 0.2126 0.7152 0.0722 0 }

A pass belongs to the script that made it: `postpass reset`, which
`stim::prelude` runs at every script load, deletes every shaderpass. A
stim script creates its passes when it loads and turns them on and off at
the states that need them.

## Command

    shaderpass                    ;# the names
    shaderpass create NAME ?-file PATH? ?-fragment SOURCE? ?-priority P? ?-animate auto|0|1?
    shaderpass on NAME | off NAME
    shaderpass uniform NAME UNIFORM ?value ...?        ;# no values: read
    shaderpass sampler NAME UNIFORM ?TEXTUREID?        ;# a texture for a sampler2D; 0 removes
    shaderpass animate NAME ?auto|0|1?                 ;# returns whether it is animated
    shaderpass status NAME        ;# dict: name on priority file animate animated uniforms stages repaints
    shaderpass delete NAME
    shaderpass reset              ;# delete them all

`create` builds the shader and leaves the pass off. With neither `-file`
nor `-fragment` the pass copies the scene through. Creating a name that
exists replaces that pass; if the new shader does not build, the error is
the compiler's message and the old pass stays as it was.

## Uniforms and textures

Uniforms are kept in the shader module's uniform tables (`shaderutils.c`,
the code behind `shaderObjSetUniform`), so they are typed by the
program's own declarations: `bool`, `int`, `float`, `vec2`-`vec4`,
`mat2`-`mat4`, and arrays of them. Values are numbers, as separate words
or lists, flattened:

    shaderpass uniform p gain 0.5
    shaderpass uniform p which 2                       ;# an int
    shaderpass uniform p m {0 0 1} {0 1 0} {1 0 0}     ;# a mat3, column by column
    shaderpass uniform p pair {1 1} {0 0}              ;# vec2 pair[2]

A uniform the shader does not declare (or declares and never uses, which
the compiler removes) is an error, as is a value of the wrong kind or too
many values. `shaderpass status NAME` lists every uniform the program has
under `uniforms`, each with its `type`, its `size` if an array, and its
`value` if one is set. Integer vectors (`ivec2`...) cannot be set.

A `sampler2D` other than `scene` takes a GL texture id, as
`shaderObjSetSampler` does:

    set img [shaderImageCreate $pixels $w $h linear]
    shaderpass sampler p lut [shaderImageID $img]

The pass binds it on a texture unit of its own when it draws. The texture
is the script's: deleting the pass does not delete it.

## Order

`-priority` (default 200) places the pass in the chain, lower first.
`gazewin` is at 100, so by default a shaderpass applies to the gaze
window's result: noise over the mask as well as the window. Below 100 it
runs first: the gaze window then masks or blurs the noisy scene. In
`postpass list` a pass appears as `shader/NAME`.

## Writing a shader

A file uses the layout of stim2's `shaders/` directory: `-- Vertex`,
`-- Fragment` and `-- Uniforms` sections. Only the fragment is required,
and text with no section markers is all fragment (so `-fragment` can be a
bare `main`). The default vertex shader draws a full-screen triangle. A
source without its own `#version` gets the platform's (`#version 330`, or
`#version 300 es` with highp precision), the same as a shader module
file. `-- Uniforms` lines give defaults, `name value ...` (values may be
quoted), set as `shaderpass uniform` sets them.

Two differences from a file for `shaderBuild`: a pass file needs no
`-- Vertex` section, and a shader that fails to build returns the
compiler's message as the Tcl error.

    -- Fragment
    uniform sampler2D scene;
    uniform float gain;
    out vec4 frag;
    void main() {
      vec4 s = texelFetch(scene, ivec2(gl_FragCoord.xy), 0);
      frag = vec4((s.rgb - 0.5) * gain + 0.5, s.a);    // contrast about mid grey
    }

    -- Uniforms
    gain "0.5"

The pass sets these when the shader declares them:

| uniform | type | |
|---|---|---|
| `scene` | `sampler2D` | the scene as the pass found it (unit 0); `texelFetch(scene, ivec2(gl_FragCoord.xy), 0)` is the pixel as drawn |
| `resolution` | `vec2` | framebuffer size, pixels |
| `halfdeg` | `vec2` | half the screen in degrees (`HalfScreenDegreeX/Y`) |
| `time` | `float` | the stim clock in seconds, wrapped every hour |
| `background` | `vec4` | stim2's background colour |

A pixel's position in degrees is
`(gl_FragCoord.xy / resolution * 2.0 - 1.0) * halfdeg`. A script cannot
override these five: the pass sets them after the script's uniforms.

## Static displays and animation

A display that is not animating draws no frames. While a pass is on,
stim2 repaints it (the passes again over the saved scene; a real frame,
see [postpass.md](postpass.md)) when:

- a uniform of the pass changes: one repaint;
- the pass is **animated**: one repaint every refresh.

A pass is animated if its shader declares `time` (`-animate auto`, the
default). `-animate 0` stops that for a shader that reads `time` but
should change only with the scene; `-animate 1` forces it.

`time` is the stim clock, not the wall clock. In a mirror or a replay
render it is the stepped clock, so a frame rendered at the rig's frame
time draws the noise field the rig's frame had. It wraps every hour to
keep a float's precision under a millisecond: a shader that needs a
continuous phase across that wrap should not rely on `time` alone.

## Pixel values, not light

Like `gazewin`, a pass works on the values in the framebuffer, and stim2
does no gamma handling. On a linearized display (a Display++, or a
monitor driven through a calibrated gamma table) `noise.glsl`'s `amount`
is a luminance deviation and its mean is the scene's. On an uncorrected
display it is neither. Noise is also clipped at 0 and 1, so near black or
white its distribution is one-sided.

## Tests

    stim2 --headless -w 320 -h 180 --port-base 5610 -f /abs/path/tests/test_shaderpass.tcl

Stepped, into a frame sink: an inline swizzle, both shipped shaders, a
uniform change shown by `frameRepaint`, typed uniforms (an int, a mat3, a
vec2 array), a texture sampler, a shader found by name, shaders that fail
to build, noise
repeating at the same stim time and differing at another, `gazewin` and a
shaderpass chained in either order on a frame and on a repaint, and
`postpass reset`.

Checked by hand on a live (not stepped) headless stim2, not yet in a test:
an animated pass repaints a static display at the refresh rate, `-animate
0` stops it, and one uniform change gives one repaint.

Not yet exercised: the GLES and Windows builds, a rig, and a replay or
mirror of a session that used a shaderpass.
