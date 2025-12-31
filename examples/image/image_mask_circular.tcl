# examples/image/image_mask_circular.tcl
# Demonstrate circular gaze-contingent masking
# Mode 1: circular window (show inside), Mode 3: inverse (show outside)
# Mask coordinates are in texture space (0-1)

workspace::reset

proc image_mask_circular { mode center_x center_y radius feather } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    # Map choice strings to mode integers
    set mode_map {off 0 window 1 inverse 3}
    set mode_int [dict get $mode_map $mode]
    
    set imgdir ./assets
    set imgname backpack.png
    set s [image [file join $imgdir $imgname]]
    
    # Apply circular mask
    # Mode: 0=off, 1=circular window, 3=inverse circular
    imageMask $s $mode_int $center_x $center_y $radius $feather
    
    scaleObj $s 6.0 6.0
    
    glistAddObject $s 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export image_mask_circular {
    mode     {choice {off window inverse} off "Mask Mode"}
    center_x {float 0.0 1.0 0.01 0.5 "Center X"}
    center_y {float 0.0 1.0 0.01 0.5 "Center Y"}
    radius   {float 0.0 0.5 0.01 0.2 "Radius"}
    feather  {float 0.0 0.2 0.01 0.05 "Feather (soft edge)"}
}
