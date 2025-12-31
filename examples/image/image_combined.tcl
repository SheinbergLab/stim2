# examples/image/image_combined.tcl
# Comprehensive demo combining multiple image processing features
# Shows how effects can be stacked together

workspace::reset

proc image_combined { scale grayscale brightness contrast invert opacity } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    set imgdir ./assets
    set imgname backpack.png
    set s [image [file join $imgdir $imgname]]
    
    # Apply all adjustments
    imageGrayscale $s $grayscale
    imageBrightness $s $brightness
    imageContrast $s $contrast
    imageInvert $s $invert
    imageOpacity $s $opacity
    
    scaleObj $s $scale $scale
    
    glistAddObject $s 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export image_combined {
    scale      {float 1.0 8.0 0.1 5.0 "Scale"}
    grayscale  {bool 0 "Grayscale"}
    brightness {float -1.0 1.0 0.01 0.0 "Brightness"}
    contrast   {float 0.0 3.0 0.01 1.0 "Contrast"}
    invert     {bool 0 "Invert"}
    opacity    {float 0.0 1.0 0.01 1.0 "Opacity"}
}
