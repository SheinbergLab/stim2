# examples/image/image_levels.tcl
# Demonstrate image brightness, contrast, and gamma controls
# Useful for adjusting image appearance in real-time

workspace::reset

proc image_levels { brightness contrast gamma } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    set imgdir ./assets
    set imgname backpack.png
    set s [image [file join $imgdir $imgname]]
    
    # Apply level adjustments
    imageBrightness $s $brightness
    imageContrast $s $contrast
    imageGamma $s $gamma
    
    scaleObj $s 5.0 5.0
    
    glistAddObject $s 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export image_levels {
    brightness {float -1.0 1.0 0.01 0.0 "Brightness"}
    contrast   {float 0.0 3.0 0.01 1.0 "Contrast"}
    gamma      {float 0.1 3.0 0.01 1.0 "Gamma"}
}
