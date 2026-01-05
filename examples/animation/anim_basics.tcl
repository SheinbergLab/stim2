# examples/animation/anim_basics.tcl
# Basic animation demonstrations
# Demonstrates: animateRotation, animateScale, animatePosition
#
# Shows the three universal animation types that work on any GR_OBJ:
#   - Rotation (continuous or oscillating)
#   - Scale (pulsing)
#   - Position (velocity or orbit)

# ============================================================
# SETUP PROCS
# ============================================================

# Simple continuous rotation
proc setup_rotation { {speed 45.0} } {
    glistInit 1
    resetObjList
    
    set p [polygon]
    objName $p shape
    polycolor $p 0.2 0.6 1.0
    scaleObj $p 2.0 2.0
    
    animateRotation shape -speed $speed
    
    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Oscillating rotation (wobble)
proc setup_oscillate { {amplitude 30.0} {freq 1.0} } {
    glistInit 1
    resetObjList
    
    set p [polygon]
    objName $p shape
    polycolor $p 1.0 0.4 0.2
    scaleObj $p 2.0 2.0
    
    animateRotation shape -oscillate $amplitude -freq $freq
    
    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Pulsing scale
proc setup_pulse { {freq 1.0} {min 1.0} {max 2.0} } {
    glistInit 1
    resetObjList
    
    set p [polygon]
    objName $p shape
    polycolor $p 0.2 0.8 0.4
    scaleObj $p 2.0 2.0
    
    animateScale shape -pulse $freq -min $min -max $max
    
    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Linear velocity
proc setup_velocity { {vx 1.0} {vy 0.5} } {
    glistInit 1
    resetObjList
    
    set p [polygon]
    objName $p shape
    polycolor $p 0.8 0.2 0.8
    scaleObj $p 1.0 1.0
    
    animatePosition shape -velocity [list $vx $vy]
    
    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# ============================================================
# ADJUSTERS
# ============================================================

proc set_rotation_speed { speed } {
    animateRotation shape -speed $speed
}

proc get_rotation_speed {{target {}}} {
    animateRotation shape
}

proc set_oscillate_params { amplitude freq } {
    animateRotation shape -oscillate $amplitude -freq $freq
}

proc get_oscillate_params {{target {}}} {
    animateRotation shape
}

proc set_pulse_params { freq min max } {
    animateScale shape -pulse $freq -min $min -max $max
}

proc get_pulse_params {{target {}}} {
    animateScale shape
}

proc set_velocity_params { vx vy } {
    animatePosition shape -velocity [list $vx $vy]
}

proc get_velocity_params {{target {}}} {
    animatePosition shape
}

proc set_shape_color { name r g b } {
    polycolor $name $r $g $b
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

# Continuous rotation
workspace::setup setup_rotation {} \
    -adjusters {rotation_adj shape_color} -label "Rotation"

workspace::adjuster rotation_adj {
    speed {float -360.0 360.0 5.0 45.0 "Speed" deg/s}
} -target {} -proc set_rotation_speed -getter get_rotation_speed -label "Rotation Speed"

# Oscillating rotation
workspace::variant oscillate {} \
    -proc setup_oscillate -adjusters {oscillate_adj shape_color} -label "Oscillate"

workspace::adjuster oscillate_adj {
    amplitude {float 5.0 90.0 5.0 30.0 "Amplitude" deg}
    freq {float 0.1 5.0 0.1 1.0 "Frequency" Hz}
} -target {} -proc set_oscillate_params -getter get_oscillate_params -label "Oscillation"

# Pulsing scale
workspace::variant pulse {} \
    -proc setup_pulse -adjusters {pulse_adj shape_color} -label "Pulse"

workspace::adjuster pulse_adj {
    freq {float 0.1 5.0 0.1 1.0 "Frequency" Hz}
    min {float 0.5 2.0 0.1 1.0 "Min Scale"}
    max {float 1.0 4.0 0.1 2.0 "Max Scale"}
} -target {} -proc set_pulse_params -getter get_pulse_params -label "Pulse"

# Linear velocity
workspace::variant velocity {} \
    -proc setup_velocity -adjusters {velocity_adj shape_color} -label "Velocity"

workspace::adjuster velocity_adj {
    vx {float -5.0 5.0 0.1 1.0 "X Velocity" deg/s}
    vy {float -5.0 5.0 0.1 0.5 "Y Velocity" deg/s}
} -target {} -proc set_velocity_params -getter get_velocity_params -label "Velocity"

# Shared color adjuster
workspace::adjuster shape_color -template color -target shape -colorpicker
