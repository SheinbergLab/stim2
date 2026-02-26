# examples/shader/fractal_demo.tcl
# Fractal stimulus generation demonstration
# Demonstrates: escape-time fractals, IFS patterns, parametric complexity
#
# Fractal types available:
#   - Julia Set: infinite variety from 2-parameter seed, great for novel stimuli
#   - Mandelbrot: classic escape-time fractal with zoom exploration
#   - Multi-Fractal: Julia/Mandelbrot/Burning Ship/Tricorn/Newton selectable
#   - IFS / fBm Noise: Sierpinski patterns + controllable 1/f noise
#
# For stimulus generation, the key parameters are:
#   - seed (c_param): each value produces a unique, non-repeating pattern
#   - max_iter: controls fine detail / spatial frequency content
#   - zoom/center: controls scale and region
#   - envelope_sigma: Gaussian windowing for controlled presentation
#
# The "randomize" buttons generate novel stimuli on each press,
# suitable for building trial-unique stimulus sets.

# ============================================================
# SHADER PATH SETUP
# ============================================================

if {![info exists ::fractal_demo_path_set]} {
    foreach dir {
        ./shaders/
        ../shaders/
        /usr/local/share/stim2/shaders/
    } {
        if {[file exists $dir] && [file isdirectory $dir]} {
            shaderAddPath $dir
            break
        }
    }
    set ::fractal_demo_path_set 1
}

# ============================================================
# UTILITY PROCS
# ============================================================

# Generate a random float in [lo, hi]
proc frand {lo hi} {
    expr {$lo + ($hi - $lo) * rand()}
}

# ============================================================
# STIM CODE
# ============================================================

# Julia set - the workhorse for novel stimulus generation
proc julia_setup {} {
    glistInit 1
    resetObjList
    shaderDeleteAll
    
    set shader [shaderBuild julia]
    set sobj [shaderObj $shader]
    objName $sobj shader_obj
    scaleObj $sobj 8.0 8.0
    
    glistAddObject $sobj 0
    glistSetCurGroup 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Mandelbrot set
proc mandelbrot_setup {} {
    glistInit 1
    resetObjList
    shaderDeleteAll
    
    set shader [shaderBuild mandelbrot]
    set sobj [shaderObj $shader]
    objName $sobj shader_obj
    scaleObj $sobj 8.0 8.0
    
    glistAddObject $sobj 0
    glistSetCurGroup 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Multi-fractal (selectable type)
proc fractal_setup {} {
    glistInit 1
    resetObjList
    shaderDeleteAll
    
    set shader [shaderBuild fractal]
    set sobj [shaderObj $shader]
    objName $sobj shader_obj
    scaleObj $sobj 8.0 8.0
    
    glistAddObject $sobj 0
    glistSetCurGroup 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# IFS / fractal noise
proc ifs_setup {} {
    glistInit 1
    resetObjList
    shaderDeleteAll
    
    set shader [shaderBuild ifs]
    set sobj [shaderObj $shader]
    objName $sobj shader_obj
    scaleObj $sobj 8.0 8.0
    
    glistAddObject $sobj 0
    glistSetCurGroup 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# ============================================================
# RANDOMIZE PROCS
# ============================================================

# Generate a random Julia seed known to produce interesting patterns
# Samples from regions near the Mandelbrot boundary where Julia sets
# are most complex and visually interesting
proc julia_randomize {name} {
    # Pick from several productive regions of the c-plane
    set region [expr {int(rand() * 5)}]
    switch $region {
        0 {
            # Near main cardioid boundary
            set angle [frand 0.0 6.283]
            set r [frand 0.24 0.26]
            set cx [expr {$r * cos($angle) - 0.25}]
            set cy [expr {$r * sin($angle)}]
        }
        1 {
            # Near period-2 bulb
            set cx [frand -1.05 -0.95]
            set cy [frand -0.1 0.1]
        }
        2 {
            # Seahorse valley
            set cx [frand -0.78 -0.74]
            set cy [frand 0.05 0.15]
        }
        3 {
            # Elephant valley
            set cx [frand 0.28 0.32]
            set cy [frand 0.02 0.06]
        }
        4 {
            # Near antenna tip
            set cx [frand -1.78 -1.74]
            set cy [frand -0.02 0.02]
        }
    }
    
    shaderObjSetUniform $name c_param "$cx $cy"
    set ::julia_cx $cx
    set ::julia_cy $cy
    redraw
}

# Randomize the multi-fractal type and seed
proc fractal_randomize {name} {
    set ftype [expr {int(rand() * 5)}]
    shaderObjSetUniform $name fractal_type $ftype
    
    if {$ftype == 0} {
        # Julia mode: randomize seed
        set cx [frand -1.5 0.5]
        set cy [frand -1.0 1.0]
        shaderObjSetUniform $name seed "$cx $cy"
    } elseif {$ftype == 4} {
        # Newton: center and zoom variation
        shaderObjSetUniform $name zoom [frand 0.5 3.0]
    } else {
        # Mandelbrot/Ship/Tricorn: random interesting region
        shaderObjSetUniform $name center "[frand -1.5 0.5] [frand -1.0 1.0]"
        shaderObjSetUniform $name zoom [frand 1.0 50.0]
    }
    redraw
}

# Randomize fBm parameters for controlled complexity variation
proc ifs_randomize_fbm {name} {
    shaderObjSetUniform $name ifs_type 3
    shaderObjSetUniform $name octaves [expr {int([frand 1 9])}]
    shaderObjSetUniform $name lacunarity [frand 1.5 3.0]
    shaderObjSetUniform $name gain [frand 0.3 0.7]
    shaderObjSetUniform $name noise_scale [frand 3.0 20.0]
    shaderObjSetUniform $name contrast [frand 0.8 2.5]
    redraw
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# ---- Shared uniform setters ----
proc set_uniform_float {name uniform val} {
    shaderObjSetUniform $name $uniform $val
    redraw
}

proc set_uniform_int {name uniform val} {
    shaderObjSetUniform $name $uniform [expr {int($val)}]
    redraw
}

proc set_uniform_bool {name uniform val} {
    shaderObjSetUniform $name $uniform [expr {$val ? 1 : 0}]
    redraw
}

proc set_uniform_vec2 {name uniform x y} {
    shaderObjSetUniform $name $uniform "$x $y"
    redraw
}

# ---- Transform adjusters (shared by all variants) ----
workspace::adjuster shader_obj_scale -template scale -target shader_obj \
    -defaults {scale 8.0}
workspace::adjuster shader_obj_rotation -template rotation -target shader_obj

# ============================================================
# JULIA SET ADJUSTERS
# ============================================================

# Track vec2 components in Tcl vars (avoids getter on uninitialized uniforms)
set ::julia_cx -0.7
set ::julia_cy 0.27015

proc set_julia_cx {name val} {
    set ::julia_cx $val
    shaderObjSetUniform $name c_param "$::julia_cx $::julia_cy"
    redraw
}
workspace::adjuster julia_cx {
    value {float -2.0 2.0 0.001 -0.7 "Re(c)"}
} -target shader_obj -proc set_julia_cx -label "Seed Real"

proc set_julia_cy {name val} {
    set ::julia_cy $val
    shaderObjSetUniform $name c_param "$::julia_cx $::julia_cy"
    redraw
}
workspace::adjuster julia_cy {
    value {float -2.0 2.0 0.001 0.27015 "Im(c)"}
} -target shader_obj -proc set_julia_cy -label "Seed Imaginary"

proc set_julia_max_iter {name val} { set_uniform_int $name max_iter $val }
workspace::adjuster julia_max_iter {
    value {float 10.0 500.0 10.0 200.0 "Iterations"}
} -target shader_obj -proc set_julia_max_iter -label "Max Iterations"

proc set_julia_zoom {name val} { set_uniform_float $name zoom $val }
workspace::adjuster julia_zoom {
    value {float 0.1 100.0 0.1 1.0 "Zoom"}
} -target shader_obj -proc set_julia_zoom -label "Zoom"

proc set_julia_colormap {name val} { set_uniform_int $name colormap $val }
workspace::adjuster julia_colormap {
    value {float 0.0 3.0 1.0 0.0 "Map"}
} -target shader_obj -proc set_julia_colormap -label "Color Map (0-3)"

proc set_julia_brightness {name val} { set_uniform_float $name brightness $val }
workspace::adjuster julia_brightness {
    value {float 0.1 3.0 0.05 1.0 "Brightness"}
} -target shader_obj -proc set_julia_brightness -label "Brightness"

proc set_julia_animate {name val} { set_uniform_bool $name animate $val }
workspace::adjuster julia_animate {
    value {bool 0 "Animate"}
} -target shader_obj -proc set_julia_animate -label "Animate Seed"

workspace::adjuster julia_randomize_btn {
    value {button "Randomize Seed"}
} -target shader_obj -proc julia_randomize -label "Novel Stimulus"

# ============================================================
# MANDELBROT ADJUSTERS
# ============================================================

proc set_mb_max_iter {name val} { set_uniform_int $name max_iter $val }
workspace::adjuster mb_max_iter {
    value {float 10.0 500.0 10.0 200.0 "Iterations"}
} -target shader_obj -proc set_mb_max_iter -label "Max Iterations"

proc set_mb_zoom {name val} { set_uniform_float $name zoom $val }
workspace::adjuster mb_zoom {
    value {float 0.1 1000.0 0.1 0.8 "Zoom"}
} -target shader_obj -proc set_mb_zoom -label "Zoom"

set ::mb_cx -0.5
set ::mb_cy 0.0

proc set_mb_center_x {name val} {
    set ::mb_cx $val
    shaderObjSetUniform $name center "$::mb_cx $::mb_cy"
    redraw
}
workspace::adjuster mb_center_x {
    value {float -2.5 1.0 0.01 -0.5 "X"}
} -target shader_obj -proc set_mb_center_x -label "Center X"

proc set_mb_center_y {name val} {
    set ::mb_cy $val
    shaderObjSetUniform $name center "$::mb_cx $::mb_cy"
    redraw
}
workspace::adjuster mb_center_y {
    value {float -1.5 1.5 0.01 0.0 "Y"}
} -target shader_obj -proc set_mb_center_y -label "Center Y"

proc set_mb_colormap {name val} { set_uniform_int $name colormap $val }
workspace::adjuster mb_colormap {
    value {float 0.0 4.0 1.0 0.0 "Map"}
} -target shader_obj -proc set_mb_colormap -label "Color Map (0-4)"

proc set_mb_brightness {name val} { set_uniform_float $name brightness $val }
workspace::adjuster mb_brightness {
    value {float 0.1 3.0 0.05 1.2 "Brightness"}
} -target shader_obj -proc set_mb_brightness -label "Brightness"

# ============================================================
# MULTI-FRACTAL ADJUSTERS
# ============================================================

proc set_fr_type {name val} { set_uniform_int $name fractal_type $val }
workspace::adjuster fr_type {
    value {float 0.0 4.0 1.0 0.0 "Type"}
} -target shader_obj -proc set_fr_type -label "Type (0=Julia 1=Mandel 2=Ship 3=Tricorn 4=Newton)"

set ::fr_sx -0.7
set ::fr_sy 0.27015

proc set_fr_seed_x {name val} {
    set ::fr_sx $val
    shaderObjSetUniform $name seed "$::fr_sx $::fr_sy"
    redraw
}
workspace::adjuster fr_seed_x {
    value {float -2.0 2.0 0.001 -0.7 "Re"}
} -target shader_obj -proc set_fr_seed_x -label "Seed Real"

proc set_fr_seed_y {name val} {
    set ::fr_sy $val
    shaderObjSetUniform $name seed "$::fr_sx $::fr_sy"
    redraw
}
workspace::adjuster fr_seed_y {
    value {float -2.0 2.0 0.001 0.27015 "Im"}
} -target shader_obj -proc set_fr_seed_y -label "Seed Imaginary"

proc set_fr_max_iter {name val} { set_uniform_int $name max_iter $val }
workspace::adjuster fr_max_iter {
    value {float 10.0 500.0 10.0 200.0 "Iterations"}
} -target shader_obj -proc set_fr_max_iter -label "Max Iterations"

proc set_fr_zoom {name val} { set_uniform_float $name zoom $val }
workspace::adjuster fr_zoom {
    value {float 0.1 100.0 0.1 1.0 "Zoom"}
} -target shader_obj -proc set_fr_zoom -label "Zoom"

proc set_fr_power {name val} { set_uniform_float $name power $val }
workspace::adjuster fr_power {
    value {float 2.0 6.0 1.0 2.0 "N"}
} -target shader_obj -proc set_fr_power -label "Power (z^n + c)"

proc set_fr_colormap {name val} { set_uniform_int $name colormap $val }
workspace::adjuster fr_colormap {
    value {float 0.0 3.0 1.0 0.0 "Map"}
} -target shader_obj -proc set_fr_colormap -label "Color Map"

proc set_fr_brightness {name val} { set_uniform_float $name brightness $val }
workspace::adjuster fr_brightness {
    value {float 0.1 3.0 0.05 1.0 "Brightness"}
} -target shader_obj -proc set_fr_brightness -label "Brightness"

proc set_fr_envelope {name val} { set_uniform_float $name envelope_sigma $val }
workspace::adjuster fr_envelope {
    value {float 0.0 0.5 0.01 0.0 "σ (0=off)"}
} -target shader_obj -proc set_fr_envelope -label "Gaussian Envelope"

workspace::adjuster fr_randomize_btn {
    value {button "Randomize"}
} -target shader_obj -proc fractal_randomize -label "Novel Stimulus"

# ============================================================
# IFS / FBM NOISE ADJUSTERS
# ============================================================

proc set_ifs_type {name val} { set_uniform_int $name ifs_type $val }
workspace::adjuster ifs_type {
    value {float 0.0 3.0 1.0 3.0 "Type"}
} -target shader_obj -proc set_ifs_type -label "Type (0=Sierpinski 1=Carpet 2=Cantor 3=fBm)"

proc set_ifs_subdivisions {name val} { set_uniform_int $name subdivisions $val }
workspace::adjuster ifs_subdivisions {
    value {float 1.0 10.0 1.0 6.0 "Depth"}
} -target shader_obj -proc set_ifs_subdivisions -label "Subdivisions"

proc set_ifs_octaves {name val} { set_uniform_int $name octaves $val }
workspace::adjuster ifs_octaves {
    value {float 1.0 8.0 1.0 5.0 "Octaves"}
} -target shader_obj -proc set_ifs_octaves -label "fBm Octaves (complexity)"

proc set_ifs_lacunarity {name val} { set_uniform_float $name lacunarity $val }
workspace::adjuster ifs_lacunarity {
    value {float 1.0 4.0 0.1 2.0 "Lacunarity"}
} -target shader_obj -proc set_ifs_lacunarity -label "Lacunarity"

proc set_ifs_gain {name val} { set_uniform_float $name gain $val }
workspace::adjuster ifs_gain {
    value {float 0.1 0.9 0.01 0.5 "Gain"}
} -target shader_obj -proc set_ifs_gain -label "Gain (persistence)"

proc set_ifs_noise_scale {name val} { set_uniform_float $name noise_scale $val }
workspace::adjuster ifs_noise_scale {
    value {float 1.0 30.0 0.5 8.0 "Scale"}
} -target shader_obj -proc set_ifs_noise_scale -label "Spatial Scale"

proc set_ifs_contrast {name val} { set_uniform_float $name contrast $val }
workspace::adjuster ifs_contrast {
    value {float 0.1 4.0 0.05 1.5 "Contrast"}
} -target shader_obj -proc set_ifs_contrast -label "Contrast"

proc set_ifs_colormap {name val} { set_uniform_int $name colormap $val }
workspace::adjuster ifs_colormap {
    value {float 0.0 2.0 1.0 0.0 "Map"}
} -target shader_obj -proc set_ifs_colormap -label "Color Map (0=gray 1=hot 2=cool)"

proc set_ifs_brightness {name val} { set_uniform_float $name brightness $val }
workspace::adjuster ifs_brightness {
    value {float 0.1 3.0 0.05 1.0 "Brightness"}
} -target shader_obj -proc set_ifs_brightness -label "Brightness"

proc set_ifs_envelope {name val} { set_uniform_float $name envelope_sigma $val }
workspace::adjuster ifs_envelope {
    value {float 0.0 0.5 0.01 0.0 "σ (0=off)"}
} -target shader_obj -proc set_ifs_envelope -label "Gaussian Envelope"

workspace::adjuster ifs_randomize_btn {
    value {button "Randomize fBm"}
} -target shader_obj -proc ifs_randomize_fbm -label "Novel Noise"

# ============================================================
# SETUP / VARIANT DEFINITIONS
# ============================================================

workspace::setup julia_setup {} \
    -adjusters {shader_obj_scale shader_obj_rotation julia_cx julia_cy julia_max_iter julia_zoom julia_colormap julia_brightness julia_animate julia_randomize_btn} \
    -label "Julia Set"

workspace::variant mandelbrot {} -proc mandelbrot_setup \
    -adjusters {shader_obj_scale shader_obj_rotation mb_max_iter mb_zoom mb_center_x mb_center_y mb_colormap mb_brightness} \
    -label "Mandelbrot"

workspace::variant fractal {} -proc fractal_setup \
    -adjusters {shader_obj_scale shader_obj_rotation fr_type fr_seed_x fr_seed_y fr_max_iter fr_zoom fr_power fr_colormap fr_brightness fr_envelope fr_randomize_btn} \
    -label "Multi-Fractal"

workspace::variant ifs {} -proc ifs_setup \
    -adjusters {shader_obj_scale shader_obj_rotation ifs_type ifs_subdivisions ifs_octaves ifs_lacunarity ifs_gain ifs_noise_scale ifs_contrast ifs_colormap ifs_brightness ifs_envelope ifs_randomize_btn} \
    -label "IFS / Fractal Noise"
