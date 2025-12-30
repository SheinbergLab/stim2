# examples/polygon/circle.tcl
# Use hte polygon module to create a circle with adjustable parameters
# Demonstrates: polygon creation, circles
# See also: nsided.tcl, points.tcl

workspace::reset

proc make_circle {} {
    set circ [polygon]
    polycirc $circ 1
    return $circ
}

proc circle {radius r g b} {
    glistInit 1
    resetObjList

    set s [make_circle]
    set diameter [expr {2*$radius}]
    scaleObj $s $diameter $diameter
    polycolor $s $r $g $b
    
    glistAddObject $s 0

    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

workspace::export circle {
    radius {float 0.1 10 0.1 2.0 "Radius" deg}
    r      {float 0 1 0.05 0.5 "Red"}
    g      {float 0 1 0.05 0.5 "Green"}
    b      {float 0 1 0.05 1.0 "Blue"}
}
