# examples/anim/anim_path.tcl
# Animate an object along a circular path
# Demonstrates addPreScript and setObjProp for per-object state

# ============================================================
# REUSABLE CODE
# ============================================================

# Animation update proc - called each frame when object is visible
proc path_update { obj } {
    set props [setObjProp $obj]
    dict with props {
        set t [expr {($::StimTime - $t0) * 0.001}]
        set a [expr {$t * $speed}]
        translateObj $obj [expr {$radius * cos($a)}] [expr {$radius * sin($a)}] 0
    }
}

proc setup_anim_path { radius speed size } {
    glistInit 1
    resetObjList
    
    # Create a simple filled circle
    set obj [polygon]
    objName $obj "dot"
    polycirc dot 1
    scaleObj dot $size $size
    
    # Store animation parameters as object properties
    setObjProp dot radius $radius
    setObjProp dot speed $speed
    setObjProp dot t0 $::StimTime
    
    # Attach update script
    addPreScript dot "path_update dot"
    
    # Add to display group with dynamic update
    glistAddObject $obj 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# Adjusters - modify properties without rebuilding
proc set_radius { name value } {
    setObjProp $name radius $value
}

proc set_speed { name value } {
    setObjProp $name speed $value
}

proc set_size { name value } {
    scaleObj $name $value $value
    redraw
}

proc set_color { name r g b } {
    polycolor $name $r $g $b
    redraw
}

# ============================================================
# DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup setup_anim_path {
    radius {float 1.0 6.0 0.5 3.0 "Path Radius" deg}
    speed  {float 0.5 5.0 0.5 2.0 "Speed" rad/s}
    size   {float 0.2 2.0 0.1 0.5 "Circle Size" deg}
} -adjusters {dot_radius dot_speed dot_size dot_color}

workspace::adjuster dot_radius {
    value {float 1.0 6.0 0.1 3.0 "Path Radius" deg}
} -target dot -proc set_radius

workspace::adjuster dot_speed {
    value {float 0.1 10.0 0.1 2.0 "Speed" rad/s}
} -target dot -proc set_speed

workspace::adjuster dot_size {
    value {float 0.1 2.0 0.05 0.5 "Size" deg}
} -target dot -proc set_size

workspace::adjuster dot_color {
    r {float 0 1 0.05 1.0 "Red"}
    g {float 0 1 0.05 1.0 "Green"}
    b {float 0 1 0.05 1.0 "Blue"}
} -target dot -proc set_color
