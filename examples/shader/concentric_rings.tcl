# examples/shaders/concentric_rings.tcl
# Concentric rings with inline shader definition
#
# Self-contained demo - shader source embedded directly in file

# ============================================================
# INLINE SHADER SOURCE
# ============================================================

set vertex_src {
in vec3 vertex_position;
in vec2 vertex_texcoord;

out vec2 texcoord;

uniform mat4 projMat;
uniform mat4 modelviewMat;

void main(void)
{
  texcoord  = vertex_texcoord;
  gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);
}
}

set fragment_src {
in vec2 texcoord;
out vec4 fragcolor;

uniform float time;

uniform float NCycles;
uniform float CyclesPerSec;
uniform float Phase;
uniform float Contrast;
uniform float Sigma;
uniform int Envelope;
uniform int Circular;
uniform float ColorR;
uniform float ColorG;
uniform float ColorB;
uniform float ColorA;
uniform int InvertR;
uniform int InvertG;
uniform int InvertB;

void main(void)
{
  float r;
  float g;
  float b;
  float alpha = 1.0;
  float dx = texcoord.s-.5;
  float dy = texcoord.t-.5;
  float d = sqrt(dx*dx+dy*dy);
  float scaled_t;
  if (Circular == 1) scaled_t = 2. * d * NCycles;
  else scaled_t = 2. * dy * NCycles;
  float angle = scaled_t*6.283+Phase+(time*CyclesPerSec);
  float modulation = (sin(angle)/2.)*Contrast;
  float intensity = clamp(modulation+0.5,0.,1.);
  if (Envelope != 0) {
    float scale = .3989423/Sigma;
    float t1 = -1.*d*d;
    float t2 = 2.*Sigma*Sigma;
    alpha = exp(t1/t2);
  }
  if (InvertR == 1) r = ColorR-(ColorR*intensity);
  else r = ColorR*intensity;
  if (InvertG == 1) g = ColorG-(ColorG*intensity);
  else g = ColorG*intensity;
  if (InvertB == 1) b = ColorB-(ColorB*intensity);
  else b = ColorB*intensity;
  fragcolor = vec4 (r, g, b, ColorA*alpha);
}
}

set uniform_defaults {
    NCycles 4
    CyclesPerSec 4
    Circular 1
    Contrast 1
    Sigma 0.2
    Envelope 1
    ColorR .3
    ColorG .8
    ColorB .5
    ColorA 1.
}

# ============================================================
# REUSABLE CODE
# ============================================================

proc setup_rings { scale } {
    global vertex_src fragment_src uniform_defaults
    
    glistInit 1
    resetObjList
    shaderDeleteAll
    
    # Build shader from inline source
    set shader [shaderBuildInline $vertex_src $fragment_src $uniform_defaults]
    
    # Create shader object
    set obj [shaderObj $shader]
    objName $obj "rings"
    scaleObj rings $scale $scale
    
    glistAddObject $obj 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# Adjuster procs
proc set_scale { name val } {
    scaleObj $name $val $val
    redraw
}

proc set_ncycles { name val } {
    shaderObjSetUniform $name NCycles $val
    redraw
}

proc set_speed { name val } {
    shaderObjSetUniform $name CyclesPerSec $val
    redraw
}

proc set_phase { name val } {
    shaderObjSetUniform $name Phase $val
    redraw
}

proc set_contrast { name val } {
    shaderObjSetUniform $name Contrast $val
    redraw
}

proc set_sigma { name val } {
    shaderObjSetUniform $name Sigma $val
    redraw
}

proc set_envelope { name val } {
    shaderObjSetUniform $name Envelope [expr {int($val)}]
    redraw
}

proc set_circular { name val } {
    shaderObjSetUniform $name Circular [expr {int($val)}]
    redraw
}

proc set_color { name r g b } {
    shaderObjSetUniform $name ColorR $r
    shaderObjSetUniform $name ColorG $g
    shaderObjSetUniform $name ColorB $b
    redraw
}

proc set_alpha { name val } {
    shaderObjSetUniform $name ColorA $val
    redraw
}

# ============================================================
# DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup setup_rings {
    scale {float 1.0 10.0 0.5 4.0 "Scale" deg}
} -adjusters {
    rings_scale rings_ncycles rings_speed rings_phase 
    rings_contrast rings_sigma rings_envelope rings_circular
    rings_color rings_alpha
}

# Transform
workspace::adjuster rings_scale {
    val {float 0.5 10.0 0.1 4.0 "Scale" deg}
} -target rings -proc set_scale

# Pattern parameters
workspace::adjuster rings_ncycles {
    val {float 0.5 20.0 0.5 4.0 "Cycles"}
} -target rings -proc set_ncycles

workspace::adjuster rings_speed {
    val {float -10.0 10.0 0.1 4.0 "Speed" cyc/s}
} -target rings -proc set_speed

workspace::adjuster rings_phase {
    val {float 0.0 6.28 0.01 0.0 "Phase" rad}
} -target rings -proc set_phase

workspace::adjuster rings_contrast {
    val {float 0.0 1.0 0.01 1.0 "Contrast"}
} -target rings -proc set_contrast

# Envelope
workspace::adjuster rings_sigma {
    val {float 0.05 1.0 0.01 0.2 "Sigma"}
} -target rings -proc set_sigma

workspace::adjuster rings_envelope {
    val {bool 1 "Gaussian Envelope"}
} -target rings -proc set_envelope

workspace::adjuster rings_circular {
    val {bool 1 "Circular (vs Linear)"}
} -target rings -proc set_circular

# Color
workspace::adjuster rings_color {
    r {float 0.0 1.0 0.01 0.3 "Red"}
    g {float 0.0 1.0 0.01 0.8 "Green"}
    b {float 0.0 1.0 0.01 0.5 "Blue"}
} -target rings -proc set_color

workspace::adjuster rings_alpha {
    val {float 0.0 1.0 0.01 1.0 "Alpha"}
} -target rings -proc set_alpha
