# examples/basic/rectangle.tcl

proc rectangle { { width 4 } { height 4 } } {
    glistInit 1
    resetObjList

    set s [polygon]
    objName $s rect
    scaleObj rect $width $height
    
    glistAddObject rect 0

    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

proc set_size { name w h } {
    scaleObj $name $w $h
    redraw
}

proc set_rotation { name angle } {
    rotateObj $name $angle 0 0 1
    redraw
}

proc set_color { name r g b } {
    polycolor $name $r $g $b
    redraw
}


#######################################################################
##                         Workspace Setup                           ##
#######################################################################

workspace::reset

workspace::setup rectangle {
} -adjusters {rect_size rect_rotation rect_color}

workspace::adjuster rect_size {
    width  {float 0.2 10.0 0.1 4.0 "Width" deg}
    height {float 0.2 10.0 0.1 4.0 "Height" deg}
} -target rect -proc set_size

workspace::adjuster rect_rotation {
    angle  {float 0 360 1 0 "Angle" deg}
} -target rect -proc set_rotation

workspace::adjuster rect_color {
    r      {float 0 1 0.05 1.0 "Red"}
    g      {float 0 1 0.05 1.0 "Green"}
    b      {float 0 1 0.05 1.0 "Blue"}
} -target rect -proc set_color
