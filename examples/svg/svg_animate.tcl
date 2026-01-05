# examples/svg/svg_animate.tcl
# SVG animation demonstration using the animate module
#
# Demonstrates:
#   - animateRotation for continuous rotation
#   - animateCustom with oscillate/hsv2rgb for module-specific properties
#   - Combining multiple animations on related objects
#   - Runtime adjustment of animation parameters
#
# The animate module handles all timing - no manual loops or scripts needed
# for universal properties (rotation, scale, position). Module-specific
# properties (SVG opacity, color) use animateCustom with utility functions.

# ============================================================
# ANIMATION STATE
# ============================================================
# Parameters adjustable at runtime via workspace adjusters
namespace eval svg_anim {
    variable color_cycle 0          ;# 0=off, 1=on
}

# ============================================================
# ANIMATION PROCS
# ============================================================

# SVG opacity and color animation
proc anim_svg_effects {t dt frame obj pulse_speed pulse_min pulse_max} {
    # Opacity pulsing
    if {$pulse_speed > 0} {
        set opacity [oscillate $t $pulse_speed $pulse_min $pulse_max]
        svgOpacity anim_svg $opacity
    }
    
    # Color cycling (controlled by namespace variable for boolean toggle)
    if {$svg_anim::color_cycle} {
        set hue [expr {fmod($t * 0.5, 1.0)}]
        lassign [hsv2rgb $hue 1.0 1.0] r g b
        svgColor anim_svg 2 $r $g $b 1.0
    } else {
        svgColor anim_svg 0 1 1 1 1
    }
}

# ============================================================
# SETUP
# ============================================================
proc svg_animate_setup {} {
    glistInit 1
    resetObjList
    
    # Load the tiger SVG
    set s [svgAsset Ghostscript_Tiger.svg]
    objName $s anim_svg
    
    # Wrap in metagroup for transforms
    set mg [metagroup]
    metagroupAdd $mg $s
    objName $mg anim_group
    scaleObj $mg 6.0 6.0
    
    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    
    # Start rotation animation on the metagroup
    animateRotation anim_group -speed 45.0
    
    # Custom animation for SVG-specific properties
    animateCustom anim_group -proc anim_svg_effects \
        -params {pulse_speed 1.0 pulse_min 0.3 pulse_max 1.0}
    
    redraw
}

# ============================================================
# ADJUSTER PROCS
# ============================================================

# Rotation speed - direct update
proc anim_set_rotation {speed} {
    animateRotation anim_group -speed $speed
}

proc anim_get_rotation {{target {}}} {
    set state [animateRotation anim_group]
    if {[dict size $state] == 0} {
        return {speed 0.0}
    }
    dict create speed [dict get $state speed]
}

# Pulse settings - update custom animation params
proc anim_set_pulse {pulse_speed pulse_min pulse_max} {
    animateCustom anim_group -params [list pulse_speed $pulse_speed pulse_min $pulse_min pulse_max $pulse_max]
}

proc anim_get_pulse {{target {}}} {
    set state [animateCustom anim_group]
    if {[dict exists $state params]} {
        set params [dict get $state params]
        return [dict create \
            pulse_speed [dict get $params pulse_speed] \
            pulse_min [dict get $params pulse_min] \
            pulse_max [dict get $params pulse_max]]
    }
    return {pulse_speed 1.0 pulse_min 0.3 pulse_max 1.0}
}

# Color cycle toggle - still uses namespace variable for boolean
proc anim_set_color_cycle {enabled} {
    set svg_anim::color_cycle $enabled
}

proc anim_get_color_cycle {{target {}}} {
    dict create enabled $svg_anim::color_cycle
}

# Static scale adjustment
proc anim_set_scale {scale} {
    scaleObj anim_group $scale $scale
}

proc anim_get_scale {{target {}}} {
    set s [scaleObj anim_group]
    dict create scale [lindex $s 0]
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup svg_animate_setup {} \
    -adjusters {anim_scale rotation_speed pulse_settings color_cycle_toggle} \
    -label "SVG Animation"

# Scale (static, not animated)
workspace::adjuster anim_scale {
    scale {float 1.0 15.0 0.5 6.0 "Scale"}
} -target {} -proc anim_set_scale -getter anim_get_scale \
  -label "Display Scale"

# Rotation speed
workspace::adjuster rotation_speed {
    speed {float -180.0 180.0 5.0 45.0 "Degrees/sec"}
} -target {} -proc anim_set_rotation -getter anim_get_rotation \
  -label "Rotation Speed"

# Pulse settings
workspace::adjuster pulse_settings {
    pulse_speed {float 0.0 5.0 0.1 1.0 "Cycles/sec"}
    pulse_min {float 0.0 1.0 0.05 0.3 "Min opacity"}
    pulse_max {float 0.0 1.0 0.05 1.0 "Max opacity"}
} -target {} -proc anim_set_pulse -getter anim_get_pulse \
  -label "Opacity Pulse"

# Color cycling toggle
workspace::adjuster color_cycle_toggle {
    enabled {bool 0 "Enable"}
} -target {} -proc anim_set_color_cycle -getter anim_get_color_cycle \
  -label "Color Cycle"
