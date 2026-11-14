# Shader files

Files for the shader module (`shaderBuild NAME`, then `shaderObj`).

## Where a shader is looked for

In two places, in this order:

1. **Your directories**: a list, searched in order. Empty to begin with.
2. **stim2's own shaders** (this directory, as installed): the system
   directory. It is not in the list, so nothing done to the list loses
   the shipped shaders.

A directory of yours is searched first, so a file there overrides a
shipped shader of the same name.

| command | |
|---|---|
| `shaderAddPath DIR` | append to your list (a directory already in it is not added again) |
| `shaderPrependPath DIR` | put first (one already in it moves to the front) |
| `shaderRemovePath DIR` | take one out; returns 1 if it was there |
| `shaderClearPaths` | empty your list |
| `shaderSetPath DIR` | your list becomes this one directory; returns the directory that was searched first |
| `shaderGetPaths ?-user?` | every directory searched, in order; `-user`: your list alone |
| `shaderSystemPath ?DIR?` | the system directory; with `DIR`, change it |
| `shaderGetResolvedPath` | where the last shader was found |

Your list belongs to the script that set it: when the next stim script
loads (`stim::prelude`), the list goes back to what it was when the first
script loaded. So a directory added at startup, from a file in
`~/.config/stim2/`, stays for every system, and one a system adds does not
reach the next.

`shaderpass -file NAME` looks along the same path (see
`docs/shaderpass.md`). The mesh module keeps a search list of its own
(`meshShaderSetPath`).

## The file

A file is sections, each starting at a line `-- Name`:

    -- Vertex        (optional)
    -- Fragment
    -- Uniforms      (optional: default values, `name value ...`)

**`-- Vertex` may be left out.** The file is then built with the standard
vertex shader, which is what most files here spell out:

    in vec3 vertex_position;
    in vec2 vertex_texcoord;
    out vec2 texcoord;
    uniform mat4 projMat;
    uniform mat4 modelviewMat;
    void main(void)
    {
      texcoord = vertex_texcoord;
      gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);
    }

So a fragment shader gets `in vec2 texcoord`, 0 to 1 across the object's
quad, and the object is placed by `translateObj` / `scaleObj` as usual.
Write a `-- Vertex` section only for something else. `shaderBuildInline`
takes an empty vertex source the same way.

A file needs the `-- Fragment` marker: one with no section markers at all
(`polar_annulus.glsl`) is not read.

`passes/` holds shaders for `shaderpass`, which run over the whole
finished scene rather than on an object: see `docs/shaderpass.md`. Their
default vertex shader is a different one (a full-screen triangle, no
`texcoord`).
