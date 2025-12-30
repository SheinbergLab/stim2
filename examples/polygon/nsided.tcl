# examples/polygons/nsided.tcl
workspace::reset

proc make_nsided {n size r g b} {
    set s [polygon]
    set step [expr {2.0 * 3.14159265 / $n}]
    dl_local x0 [dl_cos [dl_fromto 0 [expr {2 * 3.14159265}] $step]]
    dl_local y0 [dl_sin [dl_fromto 0 [expr {2 * 3.14159265}] $step]]
    dl_local x [dl_combine 0.0 $x0 [dl_first $x0]]
    dl_local y [dl_combine 0.0 $y0 [dl_first $y0]]
    polyverts $s $x $y
    polytype $s triangle_fan
    polycolor $s $r $g $b
    scaleObj $s $size $size
    return $s
}

proc show {} {
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

proc hide {} {
    glistSetVisible 0
    redraw
}

proc nsided {n size r g b} {
    resetObjList
    glistInit 1
    set shape [make_nsided $n $size $r $g $b]
    glistAddObject $shape 0    ;# was $s
    show
}

workspace::export nsided {
    n    {int 3 20 1 8 "Sides"}
    size {float 0.1 5 0.1 2.0 "Size" deg}
    r    {float 0 1 0.05 1.0 "Red"}
    g    {float 0 1 0.05 0.0 "Green"}
    b    {float 0 1 0.05 1.0 "Blue"}
}

workspace::export hide {}
