# examples/image/image_opacity.tcl
# Demonstrate image opacity control for alpha blending
# Shows two overlapping images with adjustable transparency

workspace::reset

proc image_opacity { opacity_front offset_x } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    set imgdir ./assets
    
    # Background image (fully opaque)
    set bg [image [file join $imgdir backpack.png]]
    scaleObj $bg 4.0 4.0
    translateObj $bg [expr {-$offset_x}] 0 0
    
    # Foreground image (adjustable opacity)
    set fg [image [file join $imgdir movie_ticket.png]]
    scaleObj $fg 4.0 4.0
    translateObj $fg $offset_x 0 0
    imageOpacity $fg $opacity_front
    
    glistAddObject $bg 0
    glistAddObject $fg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export image_opacity {
    opacity_front {float 0.0 1.0 0.01 0.5 "Front Opacity"}
    offset_x      {float 0.0 3.0 0.1 1.0 "Horizontal Offset"}
}
