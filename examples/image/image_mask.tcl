# examples/image/image_mask.tcl
# Gaze-contingent masking demonstration
# Demonstrates: circular window, inverse circular, rectangular masks
#
# This consolidates: image_mask_circular, image_mask_rect, image_gaze_sim
#
# Mask modes:
#   0 = off
#   1 = circular window (show inside) - use for gaze-contingent display
#   2 = rectangular window
#   3 = inverse circular (show outside)
#
# All mask coordinates are in texture space (0-1)
# For gaze-contingent use, update center_x/center_y from eye tracker

# ============================================================
# STIM CODE
# ============================================================

proc mask_setup {filename} {
    glistInit 1
    resetObjList
    imageTextureReset
    
    set img [imageAsset $filename]
    objName $img img
    scaleObj $img 6.0 6.0
    
    # Start with mask off
    imageMask $img 0 0.5 0.5 0.2 0.05
    
    glistAddObject $img 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ---- Adjuster helper procs ----

# Circular mask (modes 1 and 3)
proc mask_set_circular {name mode center_x center_y radius feather} {
    # Map choice to int
    set mode_map {off 0 window 1 inverse 3}
    set mode_int [dict get $mode_map $mode]
    
    if {$mode_int == 0} {
        imageMask $name 0 0.5 0.5 0.2 0.05
    } else {
        imageMask $name $mode_int $center_x $center_y $radius $feather
    }
    redraw
}

proc mask_get_circular {name} {
    set vals [imageMask $name]
    set mode_int [lindex $vals 0]
    
    # Map int back to choice
    set mode_rmap {0 off 1 window 3 inverse}
    if {[dict exists $mode_rmap $mode_int]} {
        set mode [dict get $mode_rmap $mode_int]
    } else {
        set mode off
    }
    
    dict create \
        mode $mode \
        center_x [lindex $vals 1] \
        center_y [lindex $vals 2] \
        radius [lindex $vals 3] \
        feather [lindex $vals 6]
}

# Rectangular mask (mode 2)
proc mask_set_rect {name mode center_x center_y width height feather} {
    if {$mode eq "off"} {
        imageMask $name 0 0.5 0.5 0.4 0.3 0.05
    } else {
        imageMask $name 2 $center_x $center_y $width $height $feather
    }
    redraw
}

proc mask_get_rect {name} {
    set vals [imageMask $name]
    set mode_int [lindex $vals 0]
    
    if {$mode_int == 2} {
        set mode on
    } else {
        set mode off
    }
    
    dict create \
        mode $mode \
        center_x [lindex $vals 1] \
        center_y [lindex $vals 2] \
        width [lindex $vals 4] \
        height [lindex $vals 5] \
        feather [lindex $vals 6]
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Circular mask (includes gaze-contingent use case)
workspace::setup mask_setup {
    filename {choice {backpack.png movie_ticket.png} backpack.png "Image"}
} -adjusters {img_scale mask_circular} -label "Circular Mask"

# Rectangular mask variant
workspace::variant mask_rect {
    filename {choice {backpack.png movie_ticket.png} backpack.png "Image"}
} -proc mask_setup -adjusters {img_scale mask_rect} -label "Rectangular Mask"

# Transform adjuster
workspace::adjuster img_scale -template size2d -target img \
    -defaults {width 6.0 height 6.0}

# Circular mask adjuster (mode=window for gaze-contingent display)
workspace::adjuster mask_circular {
    mode     {choice {off window inverse} off "Mask Mode"}
    center_x {float 0.0 1.0 0.01 0.5 "Center X"}
    center_y {float 0.0 1.0 0.01 0.5 "Center Y"}
    radius   {float 0.0 0.5 0.01 0.2 "Radius"}
    feather  {float 0.0 0.2 0.01 0.05 "Feather"}
} -target img -proc mask_set_circular -getter mask_get_circular -label "Circular Mask"

# Rectangular mask adjuster
workspace::adjuster mask_rect {
    mode     {choice {off on} off "Mask Mode"}
    center_x {float 0.0 1.0 0.01 0.5 "Center X"}
    center_y {float 0.0 1.0 0.01 0.5 "Center Y"}
    width    {float 0.0 1.0 0.01 0.4 "Width"}
    height   {float 0.0 1.0 0.01 0.3 "Height"}
    feather  {float 0.0 0.2 0.01 0.05 "Feather"}
} -target img -proc mask_set_rect -getter mask_get_rect -label "Rectangular Mask"
