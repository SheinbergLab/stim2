# examples/animation/anim_custom.tcl
# Custom animations using animateCustom with procs
# Demonstrates: proc-based custom animations with introspectable parameters
#
# Custom animation procs receive: t dt frame obj ?params...?
#   t     - seconds since animation started
#   dt    - delta time since last frame
#   frame - frame count since animation started
#   obj   - object name
#   ...   - additional parameters defined in -params dict
#
# Utility functions available:
#   oscillate $t $freq $min $max  - sinusoidal oscillation
#   hsv2rgb $h $s $v              - returns {r g b} list

# ============================================================
# ANIMATION PROCS
# ============================================================

# Color cycling - hue rotates over time
proc anim_color_cycle {t dt frame obj freq} {
    set hue [expr {fmod($t * $freq, 1.0)}]
    lassign [hsv2rgb $hue 1.0 1.0] r g b
    polycolor $obj $r $g $b
}

# Breathing effect - smooth scale oscillation
proc anim_breathing {t dt frame obj freq min max} {
    set scale [oscillate $t $freq $min $max]
    scaleObj $obj $scale $scale
}

# Lissajous pattern - two-frequency position animation
proc anim_lissajous {t dt frame obj freq_x freq_y amplitude phase} {
    set x [expr {$amplitude * sin($t * $freq_x * 2 * 3.14159)}]
    set y [expr {$amplitude * sin($t * $freq_y * 2 * 3.14159 + $phase)}]
    translateObj $obj $x $y 0
}

# Frame-based rotation - precise per-frame control
proc anim_frame_rotate {t dt frame obj deg_per_frame} {
    set angle [expr {$frame * $deg_per_frame}]
    rotateObj $obj $angle 0 0 1
}

# Combined effects - multiple animations in one proc
proc anim_combined {t dt frame obj rot_speed color_freq scale_freq} {
    # Rotation
    set angle [expr {$t * $rot_speed}]
    rotateObj $obj $angle 0 0 1
    
    # Color cycling
    set hue [expr {fmod($t * $color_freq, 1.0)}]
    lassign [hsv2rgb $hue 0.8 1.0] r g b
    polycolor $obj $r $g $b
    
    # Breathing scale
    set scale [oscillate $t $scale_freq 1.5 2.5]
    scaleObj $obj $scale $scale
}

# ============================================================
# SETUP PROCS
# ============================================================

proc setup_color_cycle { {freq 0.5} } {
    glistInit 1
    resetObjList
    
    set p [polygon]
    objName $p shape
    scaleObj $p 3.0 3.0
    
    animateCustom shape -proc anim_color_cycle -params [list freq $freq]
    
    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

proc setup_breathing { {freq 0.5} {min 1.0} {max 3.0} } {
    glistInit 1
    resetObjList
    
    set p [polygon]
    objName $p shape
    polycolor $p 0.3 0.7 0.9
    
    animateCustom shape -proc anim_breathing -params [list freq $freq min $min max $max]
    
    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

proc setup_lissajous { {freq_x 2.0} {freq_y 3.0} {amplitude 3.0} {phase 1.57} } {
    glistInit 1
    resetObjList
    
    set p [polygon]
    objName $p shape
    polycolor $p 1.0 0.4 0.6
    scaleObj $p 0.5 0.5
    
    animateCustom shape -proc anim_lissajous \
        -params [list freq_x $freq_x freq_y $freq_y amplitude $amplitude phase $phase]
    
    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

proc setup_frame_based { {deg_per_frame 1.0} } {
    glistInit 1
    resetObjList
    
    set p [polygon]
    objName $p shape
    polycolor $p 0.9 0.6 0.2
    scaleObj $p 2.0 2.0
    
    animateCustom shape -proc anim_frame_rotate -params [list deg_per_frame $deg_per_frame]
    
    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

proc setup_combined {} {
    glistInit 1
    resetObjList
    
    set p [polygon]
    objName $p shape
    scaleObj $p 2.0 2.0
    
    animateCustom shape -proc anim_combined \
        -params {rot_speed 30.0 color_freq 0.3 scale_freq 0.5}
    
    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# ============================================================
# ADJUSTERS
# ============================================================

proc set_color_freq { freq } {
    animateCustom shape -params [list freq $freq]
}

proc get_color_freq {{target {}}} {
    set state [animateCustom shape]
    if {[dict exists $state params]} {
        return [dict get $state params]
    }
    return {freq 0.5}
}

proc set_breathing { freq min max } {
    animateCustom shape -params [list freq $freq min $min max $max]
}

proc get_breathing {{target {}}} {
    set state [animateCustom shape]
    if {[dict exists $state params]} {
        return [dict get $state params]
    }
    return {freq 0.5 min 1.0 max 3.0}
}

proc set_lissajous { freq_x freq_y amplitude phase } {
    animateCustom shape -params [list freq_x $freq_x freq_y $freq_y amplitude $amplitude phase $phase]
}

proc get_lissajous {{target {}}} {
    set state [animateCustom shape]
    if {[dict exists $state params]} {
        return [dict get $state params]
    }
    return {freq_x 2.0 freq_y 3.0 amplitude 3.0 phase 1.57}
}

proc set_frame_rate { deg_per_frame } {
    animateCustom shape -params [list deg_per_frame $deg_per_frame]
}

proc get_frame_rate {{target {}}} {
    set state [animateCustom shape]
    if {[dict exists $state params]} {
        return [dict get $state params]
    }
    return {deg_per_frame 1.0}
}

proc set_combined { rot_speed color_freq scale_freq } {
    animateCustom shape -params [list rot_speed $rot_speed color_freq $color_freq scale_freq $scale_freq]
}

proc get_combined {{target {}}} {
    set state [animateCustom shape]
    if {[dict exists $state params]} {
        return [dict get $state params]
    }
    return {rot_speed 30.0 color_freq 0.3 scale_freq 0.5}
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

# Color cycling
workspace::setup setup_color_cycle {} -adjusters {color_freq_adj} -label "Color Cycle"

workspace::adjuster color_freq_adj {
    freq {float 0.1 2.0 0.1 0.5 "Frequency" Hz}
} -target {} -proc set_color_freq -getter get_color_freq -label "Color Cycle"

# Breathing
workspace::variant breathing {} \
    -proc setup_breathing -adjusters {breathing_adj} -label "Breathing"

workspace::adjuster breathing_adj {
    freq {float 0.1 2.0 0.1 0.5 "Frequency" Hz}
    min {float 0.5 2.0 0.1 1.0 "Min Scale"}
    max {float 1.5 4.0 0.1 3.0 "Max Scale"}
} -target {} -proc set_breathing -getter get_breathing -label "Breathing"

# Lissajous
workspace::variant lissajous {} \
    -proc setup_lissajous -adjusters {lissajous_adj} -label "Lissajous"

workspace::adjuster lissajous_adj {
    freq_x {float 0.5 5.0 0.1 2.0 "X Freq" Hz}
    freq_y {float 0.5 5.0 0.1 3.0 "Y Freq" Hz}
    amplitude {float 1.0 5.0 0.1 3.0 "Amplitude" deg}
    phase {float 0.0 6.28 0.1 1.57 "Phase" rad}
} -target {} -proc set_lissajous -getter get_lissajous -label "Lissajous"

# Frame-based
workspace::variant frame_based {} \
    -proc setup_frame_based -adjusters {frame_rate_adj} -label "Frame-Based"

workspace::adjuster frame_rate_adj {
    deg_per_frame {float 0.1 5.0 0.1 1.0 "Deg/Frame"}
} -target {} -proc set_frame_rate -getter get_frame_rate -label "Frame Rate"

# Combined effects
workspace::variant combined {} \
    -proc setup_combined -adjusters {combined_adj} -label "Combined Effects"

workspace::adjuster combined_adj {
    rot_speed {float 0.0 90.0 5.0 30.0 "Rotation" deg/s}
    color_freq {float 0.1 1.0 0.05 0.3 "Color Freq" Hz}
    scale_freq {float 0.1 2.0 0.1 0.5 "Scale Freq" Hz}
} -target {} -proc set_combined -getter get_combined -label "Combined"
