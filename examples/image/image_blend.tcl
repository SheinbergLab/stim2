# examples/image/image_blend.tcl
# Multi-image blending and opacity demonstration
# Demonstrates: alpha blending, layered images, positioning
#
# This replaces: image_opacity.tcl
#
# Shows two overlapping images with independent opacity and position control

# ============================================================
# STIM CODE
# ============================================================

proc blend_setup {bg_file fg_file} {
    glistInit 1
    resetObjList
    imageTextureReset
    
    # Background image
    set bg [imageAsset $bg_file]
    objName $bg bg_img
    scaleObj $bg 4.0 4.0
    translateObj $bg -1.0 0 0
    
    # Foreground image (on top, adjustable opacity)
    set fg [imageAsset $fg_file]
    objName $fg fg_img
    scaleObj $fg 4.0 4.0
    translateObj $fg 1.0 0 0
    imageOpacity $fg 0.7
    
    # Add in order: background first, then foreground
    glistAddObject $bg 0
    glistAddObject $fg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ---- Adjuster helper procs ----

proc blend_set_fg_opacity {name opacity} {
    imageOpacity $name $opacity
    redraw
}

proc blend_get_fg_opacity {name} {
    dict create opacity [imageOpacity $name]
}

proc blend_set_positions {bg_x fg_x} {
    translateObj bg_img $bg_x 0 0
    translateObj fg_img $fg_x 0 0
    redraw
}

proc blend_get_positions {} {
    set bg_pos [translateObj bg_img]
    set fg_pos [translateObj fg_img]
    dict create \
        bg_x [lindex $bg_pos 0] \
        fg_x [lindex $fg_pos 0]
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Main setup
workspace::setup blend_setup {
    bg_file {choice {backpack.png movie_ticket.png} backpack.png "Background"}
    fg_file {choice {backpack.png movie_ticket.png} movie_ticket.png "Foreground"}
} -adjusters {bg_scale fg_scale fg_opacity positions} -label "Image Blending"

# Scale adjusters for each image
workspace::adjuster bg_scale -template size2d -target bg_img \
    -defaults {width 4.0 height 4.0} -label "Background Size"

workspace::adjuster fg_scale -template size2d -target fg_img \
    -defaults {width 4.0 height 4.0} -label "Foreground Size"

# Foreground opacity
workspace::adjuster fg_opacity {
    opacity {float 0.0 1.0 0.01 0.7 "Opacity"}
} -target fg_img -proc blend_set_fg_opacity -getter blend_get_fg_opacity \
  -label "Foreground Opacity"

# Position control for both images
workspace::adjuster positions {
    bg_x {float -5.0 5.0 0.1 -1.0 "Background X"}
    fg_x {float -5.0 5.0 0.1 1.0 "Foreground X"}
} -target {} -proc blend_set_positions -getter blend_get_positions \
  -label "Positions"
