# examples/basic/points.tcl
workspace::reset

proc make_points { n size r g b } {
    set s [polygon]
    polyverts $s [dl_zrand $n] [dl_zrand $n] [dl_zrand $n]
    polytype $s points
    polypointsize $s [expr {double($size)}]
    polycolor $s $r $g $b
    return $s
}

proc points { n scale pointsize r g b } {
    glistInit 1
    resetObjList

    set s [make_points $n $pointsize $r $g $b]
    scaleObj $s $scale $scale
    
    glistAddObject $s 0

    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export points {
    n         {int 1 10000 10 500 "N"}
    scale     {float 0.5 10 0.1 5.0 "Scale"}
    pointsize {float 1.0 15 0.1 10.0 "Pointsize" px}
    r         {float 0 1 0.05 0.5 "Red"}
    g         {float 0 1 0.05 0.5 "Green"}
    b         {float 0 1 0.05 1.0 "Blue"}
}

