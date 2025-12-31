# examples/image/image_color.tcl
# Demonstrate grayscale conversion and RGB channel gains
# Useful for color manipulation and channel isolation

workspace::reset

proc image_color { grayscale red_gain green_gain blue_gain } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    set imgdir ./assets
    set imgname backpack.png
    set s [image [file join $imgdir $imgname]]
    
    # Apply color controls
    imageGrayscale $s $grayscale
    imageColorGains $s $red_gain $green_gain $blue_gain
    
    scaleObj $s 5.0 5.0
    
    glistAddObject $s 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export image_color {
    grayscale  {bool 0 "Grayscale"}
    red_gain   {float 0.0 2.0 0.01 1.0 "Red Gain"}
    green_gain {float 0.0 2.0 0.01 1.0 "Green Gain"}
    blue_gain  {float 0.0 2.0 0.01 1.0 "Blue Gain"}
}
