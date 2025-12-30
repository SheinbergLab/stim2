# examples/basic/points.tcl
workspace::reset


proc make_triangle {} {
    set s [polygon]
    polyverts $s "-.5 0 .5"  "-.5 .5 -.5"
    polytype $s triangles
    return $s
}

proc triangle { scale rotation_deg } {
    glistInit 1
    resetObjList

    set s [make_triangle]
    scaleObj $s $scale $scale
    rotateObj $s $rotation_deg 0 0 1

    glistAddObject $s 0

    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export triangle {
    scale     {float 0.5 10 0.1 5.0 "Scale"}
    rotation  {float 0 360 1 0 "Rotation" deg}
}

