# examples/basic/rectangle.tcl
workspace::reset

proc rectangle {width height r g b} {
    glistInit 1
    resetObjList

    set s [polygon]
    scaleObj $s $width $height
    polycolor $s $r $g $b
    
    glistAddObject $s 0

    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

workspace::export rectangle {
    width  {float 0.1 10 0.1 4.0 "Width" deg}
    height {float 0.1 10 0.1 4.0 "Height" deg}
    r      {float 0 1 0.05 0.5 "Red"}
    g      {float 0 1 0.05 0.5 "Green"}
    b      {float 0 1 0.05 1.0 "Blue"}
}
