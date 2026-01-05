# examples/animation/anim_compound.tcl
# Compound animations using metagroups
# Demonstrates: combining animations on objects and their parent metagroups
#
# Key concept: metagroups let you compose animations
#   - Animate the child: transforms in local space
#   - Animate the metagroup: transforms everything together
#   - Animate both: compound motion
#
# Examples:
#   - Planet: spins on axis while orbiting
#   - Pendulum: bob spins while arm swings
#   - Pulsing spinner: rotates while scaling

# ============================================================
# SETUP PROCS
# ============================================================

# Planet: spins while orbiting
proc setup_planet { {orbit_speed 30.0} {spin_speed 120.0} {radius 3.0} } {
    glistInit 1
    resetObjList
    
    # The planet (child object)
    set planet [polygon]
    objName $planet planet
    polycolor $planet 0.2 0.5 1.0
    scaleObj $planet 0.8 0.8
    translateObj $planet $radius 0  ;# offset from center
    
    # Planet spins on its own axis
    animateRotation planet -speed $spin_speed
    
    # Orbit group (parent)
    set orbit [metagroup]
    metagroupAdd $orbit $planet
    objName $orbit orbit
    
    # Orbit rotates, carrying planet with it
    animateRotation orbit -speed $orbit_speed
    
    # Add a small "sun" at center for reference
    set sun [polygon]
    objName $sun sun
    polycolor $sun 1.0 0.8 0.2
    scaleObj $sun 0.5 0.5
    
    glistAddObject $sun 0
    glistAddObject $orbit 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Pendulum: arm swings while bob spins
proc setup_pendulum { {swing_amplitude 45.0} {swing_freq 0.5} {bob_spin 90.0} {length 3.0} } {
    glistInit 1
    resetObjList
    
    # The bob (child object)
    set bob [polygon]
    objName $bob bob
    polycolor $bob 0.8 0.3 0.3
    scaleObj $bob 0.6 0.6
    translateObj $bob 0 [expr {-$length}]  ;# hang below pivot
    
    # Bob spins
    animateRotation bob -speed $bob_spin
    
    # Arm group (parent) - swings around pivot
    set arm [metagroup]
    metagroupAdd $arm $bob
    objName $arm arm
    
    # Arm oscillates (swings)
    animateRotation arm -oscillate $swing_amplitude -freq $swing_freq
    
    # Pivot point marker
    set pivot [polygon]
    objName $pivot pivot
    polycolor $pivot 0.5 0.5 0.5
    scaleObj $pivot 0.2 0.2
    
    glistAddObject $pivot 0
    glistAddObject $arm 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Pulsing spinner: rotates while pulsing in size
proc setup_pulsing_spinner { {spin_speed 60.0} {pulse_freq 1.0} {pulse_min 0.7} {pulse_max 1.3} } {
    glistInit 1
    resetObjList
    
    # The shape (child object)
    set shape [polygon]
    objName $shape shape
    polycolor $shape 0.3 0.8 0.5
    scaleObj $shape 1.5 1.5
    
    # Shape spins
    animateRotation shape -speed $spin_speed
    
    # Pulse group (parent)
    set pulse_group [metagroup]
    metagroupAdd $pulse_group $shape
    objName $pulse_group pulse_group
    
    # Group pulses, scaling the spinning shape
    animateScale pulse_group -pulse $pulse_freq -min $pulse_min -max $pulse_max
    
    glistAddObject $pulse_group 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Orbiting spinner: spins while moving in circle
proc setup_orbiting_spinner { {orbit_speed 45.0} {orbit_radius 3.0} {spin_speed 120.0} } {
    glistInit 1
    resetObjList
    
    # The shape (child object)
    set shape [polygon]
    objName $shape shape
    polycolor $shape 1.0 0.5 0.2
    scaleObj $shape 0.8 0.8
    
    # Shape spins on its axis
    animateRotation shape -speed $spin_speed
    
    # Orbit group (parent)
    set orbit [metagroup]
    metagroupAdd $orbit $shape
    objName $orbit orbit
    
    # Group moves in circular orbit
    animatePosition orbit -orbit $orbit_speed -radius $orbit_radius
    
    # Center marker
    set center [polygon]
    objName $center center
    polycolor $center 0.3 0.3 0.3
    scaleObj $center 0.15 0.15
    
    glistAddObject $center 0
    glistAddObject $orbit 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# ============================================================
# ADJUSTERS
# ============================================================

# Planet adjusters
proc set_planet_orbit { speed } {
    animateRotation orbit -speed $speed
}
proc get_planet_orbit {{t {}}} {
    set state [animateRotation orbit]
    dict create speed [dict get $state speed]
}

proc set_planet_spin { speed } {
    animateRotation planet -speed $speed
}
proc get_planet_spin {{t {}}} {
    set state [animateRotation planet]
    dict create speed [dict get $state speed]
}

# Pendulum adjusters
proc set_pendulum_swing { amplitude freq } {
    animateRotation arm -oscillate $amplitude -freq $freq
}
proc get_pendulum_swing {{t {}}} {
    set state [animateRotation arm]
    dict create amplitude [dict get $state amplitude] freq [dict get $state freq]
}

proc set_bob_spin { speed } {
    animateRotation bob -speed $speed
}
proc get_bob_spin {{t {}}} {
    set state [animateRotation bob]
    dict create speed [dict get $state speed]
}

# Pulsing spinner adjusters
proc set_spinner_spin { speed } {
    animateRotation shape -speed $speed
}
proc get_spinner_spin {{t {}}} {
    set state [animateRotation shape]
    dict create speed [dict get $state speed]
}

proc set_spinner_pulse { freq min max } {
    animateScale pulse_group -pulse $freq -min $min -max $max
}
proc get_spinner_pulse {{t {}}} {
    set state [animateScale pulse_group]
    dict create freq [dict get $state freq] min [dict get $state min] max [dict get $state max]
}

# Orbiting spinner adjusters  
proc set_orbit_motion { orbit radius } {
    animatePosition orbit -orbit $orbit -radius $radius
}
proc get_orbit_motion {{t {}}} {
    set state [animatePosition orbit]
    dict create orbit [dict get $state orbit] radius [dict get $state radius]
}

proc set_shape_spin { speed } {
    animateRotation shape -speed $speed
}
proc get_shape_spin {{t {}}} {
    set state [animateRotation shape]
    dict create speed [dict get $state speed]
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

# Planet demo
workspace::setup setup_planet {} -adjusters {planet_orbit planet_spin} -label "Planet"

workspace::adjuster planet_orbit {
    speed {float 10.0 90.0 5.0 30.0 "Orbit Speed" deg/s}
} -target {} -proc set_planet_orbit -getter get_planet_orbit -label "Orbit"

workspace::adjuster planet_spin {
    speed {float 30.0 360.0 15.0 120.0 "Spin Speed" deg/s}
} -target {} -proc set_planet_spin -getter get_planet_spin -label "Spin"

# Pendulum demo
workspace::variant pendulum {} \
    -proc setup_pendulum -adjusters {pendulum_swing bob_spin_adj} -label "Pendulum"

workspace::adjuster pendulum_swing {
    amplitude {float 15.0 75.0 5.0 45.0 "Amplitude" deg}
    freq {float 0.2 2.0 0.1 0.5 "Frequency" Hz}
} -target {} -proc set_pendulum_swing -getter get_pendulum_swing -label "Swing"

workspace::adjuster bob_spin_adj {
    speed {float 0.0 180.0 15.0 90.0 "Speed" deg/s}
} -target {} -proc set_bob_spin -getter get_bob_spin -label "Bob Spin"

# Pulsing spinner demo
workspace::variant pulsing_spinner {} \
    -proc setup_pulsing_spinner -adjusters {spinner_spin spinner_pulse} -label "Pulsing Spinner"

workspace::adjuster spinner_spin {
    speed {float 15.0 180.0 15.0 60.0 "Speed" deg/s}
} -target {} -proc set_spinner_spin -getter get_spinner_spin -label "Spin"

workspace::adjuster spinner_pulse {
    freq {float 0.2 3.0 0.1 1.0 "Frequency" Hz}
    min {float 0.3 1.0 0.1 0.7 "Min Scale"}
    max {float 1.0 2.0 0.1 1.3 "Max Scale"}
} -target {} -proc set_spinner_pulse -getter get_spinner_pulse -label "Pulse"

# Orbiting spinner demo
workspace::variant orbiting_spinner {} \
    -proc setup_orbiting_spinner -adjusters {orbit_motion shape_spin_adj} -label "Orbiting Spinner"

workspace::adjuster orbit_motion {
    orbit {float 15.0 90.0 5.0 45.0 "Orbit Speed" deg/s}
    radius {float 1.0 5.0 0.5 3.0 "Radius" deg}
} -target {} -proc set_orbit_motion -getter get_orbit_motion -label "Orbit"

workspace::adjuster shape_spin_adj {
    speed {float 30.0 360.0 15.0 120.0 "Speed" deg/s}
} -target {} -proc set_shape_spin -getter get_shape_spin -label "Spin"
