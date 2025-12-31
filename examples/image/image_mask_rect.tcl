# examples/image/image_mask_rect.tcl
# Demonstrate rectangular gaze-contingent masking
# Mode 2: rectangular window
# Mask coordinates are in texture space (0-1)

workspace::reset

proc image_mask_rect { center_x center_y width height feather } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    set imgdir ./assets
    set imgname backpack.png
    set s [image [file join $imgdir $imgname]]
    
    # Apply rectangular mask (mode 2)
    imageMask $s 2 $center_x $center_y $width $height $feather
    
    scaleObj $s 6.0 6.0
    
    glistAddObject $s 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export image_mask_rect {
    center_x {float 0.0 1.0 0.01 0.5 "Center X"}
    center_y {float 0.0 1.0 0.01 0.5 "Center Y"}
    width    {float 0.0 1.0 0.01 0.4 "Width"}
    height   {float 0.0 1.0 0.01 0.3 "Height"}
    feather  {float 0.0 0.2 0.01 0.05 "Feather (soft edge)"}
}
