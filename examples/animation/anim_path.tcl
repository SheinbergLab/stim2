# examples/animation/anim_path.tcl
# Animate an object along a circular path
# Demonstrates: animatePosition -orbit for circular motion
#
# Before: required addPreScript, setObjProp, manual time/trig calculation
# Now: single animatePosition call handles it all

# ============================================================
# SETUP
# ============================================================

proc setup_anim_path { {radius 3.0} {speed 90.0} {size 0.5} } {
    glistInit 1
    resetObjList
    
    # Create a simple filled circle
    set obj [polygon]
    objName $obj dot
    polycirc dot 1
    scaleObj dot $size $size
    
    # Animate along circular path - that's it!
    animatePosition dot -orbit $speed -radius $radius
    
    # Add to display group with dynamic update
    glistAddObject $obj 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# ADJUSTERS
# ============================================================

proc set_orbit { name orbit radius } {
    animatePosition $name -orbit $orbit -radius $radius
}

proc get_orbit { name } {
    set state [animatePosition $name]
    if {[dict size $state] == 0} {
        return {orbit 90.0 radius 3.0}
    }
    dict create orbit [dict get $state orbit] radius [dict get $state radius]
}

proc set_size { name value } {
    scaleObj $name $value $value
}

proc get_size { name } {
    set s [scaleObj $name]
    dict create value [lindex $s 0]
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup setup_anim_path {} \
    -adjusters {dot_orbit dot_size dot_color} -label "Circular Path"

workspace::adjuster dot_orbit {
    orbit  {float 10.0 360.0 5.0 90.0 "Speed" deg/s}
    radius {float 1.0 6.0 0.1 3.0 "Radius" deg}
} -target dot -proc set_orbit -getter get_orbit -label "Orbit"

workspace::adjuster dot_size {
    value {float 0.1 2.0 0.05 0.5 "Size" deg}
} -target dot -proc set_size -getter get_size -label "Size"

workspace::adjuster dot_color -template color -target dot -colorpicker
