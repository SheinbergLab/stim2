# examples/shader/shader_inline_demo.tcl
# Inline GLSL shader demonstration
# Demonstrates: shaderBuildInline for simple shaders defined in Tcl
#
# This shows how to define a shader entirely within the Tcl script
# without needing an external .glsl file. Good for simple effects
# or when you want the shader code visible alongside the Tcl code.

# ============================================================
# SHADER CODE
# ============================================================

# Standard vertex shader (same as used by most shaders)
set ::checkerboard_vertex {
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
}

# Simple checkerboard fragment shader
set ::checkerboard_fragment {
uniform float CheckSize;
uniform float ColorR1;
uniform float ColorG1;
uniform float ColorB1;
uniform float ColorR2;
uniform float ColorG2;
uniform float ColorB2;

in vec2 texcoord;
out vec4 fragcolor;

void main(void)
{
    // Convert texcoord (0-1) to checker grid coordinates
    float x = floor(texcoord.s * CheckSize);
    float y = floor(texcoord.t * CheckSize);
    
    // Checkerboard pattern: alternate based on sum of coordinates
    float checker = mod(x + y, 2.0);
    
    // Mix between the two colors
    vec3 color1 = vec3(ColorR1, ColorG1, ColorB1);
    vec3 color2 = vec3(ColorR2, ColorG2, ColorB2);
    vec3 color = mix(color1, color2, checker);
    
    fragcolor = vec4(color, 1.0);
}
}

# ============================================================
# STIM CODE
# ============================================================

proc checkerboard_setup {} {
    glistInit 1
    resetObjList
    shaderDeleteAll
    
    # Build shader inline - vertex_source fragment_source ?uniforms?
    # Returns a generated shader name like "shader0"
    set shader [shaderBuildInline $::checkerboard_vertex $::checkerboard_fragment {
        CheckSize 8.0
        ColorR1 1.0
        ColorG1 1.0
        ColorB1 1.0
        ColorR2 0.0
        ColorG2 0.0
        ColorB2 0.0
    }]
    set sobj [shaderObj $shader]
    objName $sobj shader_obj
    scaleObj $sobj 8.0 8.0
    
    glistAddObject $sobj 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# ---- Uniform setter procs ----
proc set_cb_CheckSize {name val} {
    shaderObjSetUniform $name CheckSize $val
    redraw
}

proc set_cb_Color1 {name r g b} {
    shaderObjSetUniform $name ColorR1 $r
    shaderObjSetUniform $name ColorG1 $g
    shaderObjSetUniform $name ColorB1 $b
    redraw
}

proc set_cb_Color2 {name r g b} {
    shaderObjSetUniform $name ColorR2 $r
    shaderObjSetUniform $name ColorG2 $g
    shaderObjSetUniform $name ColorB2 $b
    redraw
}

# Transform adjusters
workspace::adjuster shader_obj_scale -template scale -target shader_obj \
    -defaults {scale 8.0}
workspace::adjuster shader_obj_rotation -template rotation -target shader_obj

# Checkerboard uniforms
workspace::adjuster cb_CheckSize {
    value {float 1.0 32.0 1.0 8.0 "Count"}
} -target shader_obj -proc set_cb_CheckSize -label "Squares"

workspace::adjuster cb_Color1 {
    r {float 0.0 1.0 0.01 1.0 "R"}
    g {float 0.0 1.0 0.01 1.0 "G"}
    b {float 0.0 1.0 0.01 1.0 "B"}
} -target shader_obj -proc set_cb_Color1 -label "Color 1" -colorpicker

workspace::adjuster cb_Color2 {
    r {float 0.0 1.0 0.01 0.0 "R"}
    g {float 0.0 1.0 0.01 0.0 "G"}
    b {float 0.0 1.0 0.01 0.0 "B"}
} -target shader_obj -proc set_cb_Color2 -label "Color 2" -colorpicker

workspace::setup checkerboard_setup {} \
    -adjusters {shader_obj_scale shader_obj_rotation cb_CheckSize cb_Color1 cb_Color2} \
    -label "Checkerboard (Inline Shader)"
