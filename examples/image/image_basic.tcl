# examples/image/image_basic.tcl
# Demonstrate basic image loading with scale and rotation
# Uses the dedicated image module

workspace::reset

proc single_image { scale rotation_deg } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    set imgdir ./assets
    set imgname backpack.png
    set s [image [file join $imgdir $imgname]]
    
    scaleObj $s $scale $scale
    rotateObj $s $rotation_deg 0 0 1
    
    glistAddObject $s 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export single_image {
    scale     {float 0.5 10 0.1 5.0 "Scale"}
    rotation  {float 0 360 1 0 "Rotation" deg}
}
