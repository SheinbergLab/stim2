# examples/shader/shader_demo.tcl
# GLSL shader demonstration
# Demonstrates: procedural shaders, automatic uniforms, real-time parameters
#
# This provides examples of different procedural shaders:
#   - Concentric Sine (animated rings with Gaussian envelope)
#   - Polar Wedge (retinotopic mapping stimulus)
#   - Nebula (animated procedural nebula)
#
# Shader uniforms are automatically exposed based on their names:
#   - "time" and "resolution" are auto-updated by the system
#   - Other uniforms become adjustable via shaderObjSetUniform
#
# Note: Shader selection requires re-running setup (program change),
# but uniform parameters are adjustable in real-time!

# ============================================================
# SHADER PATH SETUP
# ============================================================
# Ensure shader path is set - check common locations
# This only runs once when the file is sourced

if {![info exists ::shader_demo_path_set]} {
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
    set ::shader_demo_path_set 1
}

# ============================================================
# STIM CODE
# ============================================================

# Concentric sine shader - animated rings with optional Gaussian envelope
proc concentricsine_setup {} {
    glistInit 1
    resetObjList
    shaderDeleteAll
    
    set shader [shaderBuild concentricsine]
    set sobj [shaderObj $shader]
    objName $sobj shader_obj
    scaleObj $sobj 8.0 8.0
    
    glistAddObject $sobj 0
    glistSetCurGroup 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Polar wedge - retinotopic mapping stimulus with log-polar checkerboard
proc polar_wedge_setup {} {
    glistInit 1
    resetObjList
    shaderDeleteAll
    
    set shader [shaderBuild polar_wedge]
    set sobj [shaderObj $shader]
    objName $sobj shader_obj
    scaleObj $sobj 8.0 8.0
    
    glistAddObject $sobj 0
    glistSetCurGroup 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Nebula - animated procedural nebula with stars
proc nebula_setup {} {
    glistInit 1
    resetObjList
    shaderDeleteAll
    
    set shader [shaderBuild nebula]
    set sobj [shaderObj $shader]
    objName $sobj shader_obj
    scaleObj $sobj 8.0 8.0
    
    glistAddObject $sobj 0
    glistSetCurGroup 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
    
    # Nebula has no user-adjustable uniforms (all procedural)
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# ---- Uniform setter procs ----
proc set_uniform_float {name uniform val} {
    shaderObjSetUniform $name $uniform $val
    redraw
}

proc set_uniform_bool {name uniform val} {
    shaderObjSetUniform $name $uniform [expr {$val ? 1 : 0}]
    redraw
}

proc set_cs_color {name r g b a} {
    shaderObjSetUniform $name ColorR $r
    shaderObjSetUniform $name ColorG $g
    shaderObjSetUniform $name ColorB $b
    shaderObjSetUniform $name ColorA $a
    redraw
}

# ---- Transform adjusters (shared by all variants) ----
workspace::adjuster shader_obj_scale -template scale -target shader_obj \
    -defaults {scale 8.0}
workspace::adjuster shader_obj_rotation -template rotation -target shader_obj

# ---- Concentric Sine uniforms ----
proc set_cs_NCycles {name val} { set_uniform_float $name NCycles $val }
workspace::adjuster cs_NCycles {
    value {float 0.5 20.0 0.5 4.0 "Cycles"}
} -target shader_obj -proc set_cs_NCycles -label "Cycles"

proc set_cs_CyclesPerSec {name val} { set_uniform_float $name CyclesPerSec $val }
workspace::adjuster cs_CyclesPerSec {
    value {float 0.0 10.0 0.1 4.0 "Hz"}
} -target shader_obj -proc set_cs_CyclesPerSec -label "Speed"

proc set_cs_Contrast {name val} { set_uniform_float $name Contrast $val }
workspace::adjuster cs_Contrast {
    value {float 0.0 1.0 0.01 1.0 "Contrast"}
} -target shader_obj -proc set_cs_Contrast -label "Contrast"

proc set_cs_Sigma {name val} { set_uniform_float $name Sigma $val }
workspace::adjuster cs_Sigma {
    value {float 0.01 0.5 0.01 0.2 "σ"}
} -target shader_obj -proc set_cs_Sigma -label "Envelope Sigma"

proc set_cs_Envelope {name val} { set_uniform_bool $name Envelope $val }
workspace::adjuster cs_Envelope {
    value {bool 1 "Enable"}
} -target shader_obj -proc set_cs_Envelope -label "Gaussian Envelope"

proc set_cs_Circular {name val} { set_uniform_bool $name Circular $val }
workspace::adjuster cs_Circular {
    value {bool 1 "Circular"}
} -target shader_obj -proc set_cs_Circular -label "Pattern"

workspace::adjuster cs_Color {
    r {float 0.0 1.0 0.01 0.3 "R"}
    g {float 0.0 1.0 0.01 0.8 "G"}
    b {float 0.0 1.0 0.01 0.5 "B"}
    a {float 0.0 1.0 0.01 1.0 "A"}
} -target shader_obj -proc set_cs_color -label "Color" -colorpicker

# ---- Polar Wedge uniforms ----
proc set_pw_NRings {name val} { set_uniform_float $name NRings $val }
workspace::adjuster pw_NRings {
    value {float 1.0 30.0 1.0 15.0 "Rings"}
} -target shader_obj -proc set_pw_NRings -label "Rings"

proc set_pw_NSpokes {name val} { set_uniform_float $name NSpokes $val }
workspace::adjuster pw_NSpokes {
    value {float 4.0 48.0 2.0 24.0 "Spokes"}
} -target shader_obj -proc set_pw_NSpokes -label "Spokes"

proc set_pw_WedgeSize {name val} { set_uniform_float $name WedgeSize $val }
workspace::adjuster pw_WedgeSize {
    value {float 0.05 0.5 0.01 0.25 "Size"}
} -target shader_obj -proc set_pw_WedgeSize -label "Wedge Size"

proc set_pw_CycleTime {name val} { set_uniform_float $name CycleTime $val }
workspace::adjuster pw_CycleTime {
    value {float 4.0 64.0 1.0 32.0 "Seconds"}
} -target shader_obj -proc set_pw_CycleTime -label "Cycle Time"

proc set_pw_HoleSize {name val} { set_uniform_float $name HoleSize $val }
workspace::adjuster pw_HoleSize {
    value {float 0.0 0.3 0.01 0.0 "Size"}
} -target shader_obj -proc set_pw_HoleSize -label "Hole Size"

proc set_pw_StimSize {name val} { set_uniform_float $name StimSize $val }
workspace::adjuster pw_StimSize {
    value {float 1.0 20.0 0.5 12.0 "Degrees"}
} -target shader_obj -proc set_pw_StimSize -label "Stim Size"

# ---- Setup definitions ----
workspace::setup concentricsine_setup {} \
    -adjusters {shader_obj_scale shader_obj_rotation cs_NCycles cs_CyclesPerSec cs_Contrast cs_Sigma cs_Envelope cs_Circular cs_Color} \
    -label "Concentric Sine"

workspace::variant polar_wedge {} -proc polar_wedge_setup \
    -adjusters {shader_obj_scale shader_obj_rotation pw_NRings pw_NSpokes pw_WedgeSize pw_CycleTime pw_HoleSize pw_StimSize} \
    -label "Polar Wedge (Retinotopy)"

workspace::variant nebula {} -proc nebula_setup \
    -adjusters {shader_obj_scale shader_obj_rotation} \
    -label "Nebula"
