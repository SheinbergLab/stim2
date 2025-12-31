# examples/anim/anim_path.tcl
# Animate an object along a circular path
# Demonstrates addPreScript and setObjProp for per-object state

workspace::reset

# Animation update proc - called each frame when object is visible
proc path_update { obj } {
    set props [setObjProp $obj]
    dict with props {
        set t [expr {($::StimTime - $t0) * 0.001}]
        set a [expr {$t * $speed}]
        translateObj $obj [expr {$radius * cos($a)}] [expr {$radius * sin($a)}] 0
    }
}

proc anim_path { radius speed size } {
    glistInit 1
    resetObjList
    
    # Create a simple filled circle
    set obj [polygon]
    polycirc $obj 1
    scaleObj $obj $size $size
    
    # Store animation parameters as object properties
    setObjProp $obj radius $radius
    setObjProp $obj speed $speed
    setObjProp $obj t0 $::StimTime
    
    # Attach update script
    addPreScript $obj "path_update $obj"
    
    # Add to display group with dynamic update
    glistAddObject $obj 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export anim_path {
    radius {float 1.0 6.0 0.5 3.0 "Path Radius"}
    speed  {float 0.5 5.0 0.5 2.0 "Speed (rad/s)"}
    size   {float 0.2 2.0 0.1 0.5 "Circle Size"}
}
