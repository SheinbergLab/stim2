# examples/image/image_effects.tcl
# Demonstrate image effects: inversion and binary thresholding
# Threshold converts image to black/white based on luminance

workspace::reset

proc image_effects { invert threshold_on threshold_value } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    set imgdir ./assets
    set imgname backpack.png
    set s [image [file join $imgdir $imgname]]
    
    # Apply effects
    imageInvert $s $invert
    imageThreshold $s $threshold_on $threshold_value
    
    scaleObj $s 5.0 5.0
    
    glistAddObject $s 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export image_effects {
    invert          {bool 0 "Invert Colors"}
    threshold_on    {bool 0 "Threshold Enable"}
    threshold_value {float 0.0 1.0 0.01 0.5 "Threshold Level"}
}
