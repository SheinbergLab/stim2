# examples/image/image_demo.tcl
# Comprehensive image display demonstration
# Demonstrates: image loading, transforms, levels, color, effects
#
# This consolidates: image_basic, image_color, image_levels, 
#                    image_effects, image_combined
#
# Setup parameters (require reload):
#   - filename: which image to load
#
# Adjusters (real-time):
#   - Transform: scale, rotation
#   - Levels: brightness, contrast, gamma
#   - Color: grayscale, RGB gains
#   - Effects: invert, threshold

# ============================================================
# STIM CODE
# ============================================================

proc image_setup {filename} {
    glistInit 1
    resetObjList
    imageTextureReset
    
    set img [imageAsset $filename]
    objName $img img
    scaleObj $img 5.0 5.0
    
    glistAddObject $img 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ---- Adjuster helper procs ----

# Levels adjustment (brightness, contrast, gamma)
proc image_set_levels {name brightness contrast gamma} {
    imageBrightness $name $brightness
    imageContrast $name $contrast
    imageGamma $name $gamma
    redraw
}

proc image_get_levels {name} {
    dict create \
        brightness [imageBrightness $name] \
        contrast [imageContrast $name] \
        gamma [imageGamma $name]
}

# Color adjustment (grayscale + RGB gains)
proc image_set_color {name grayscale red_gain green_gain blue_gain} {
    imageGrayscale $name $grayscale
    imageColorGains $name $red_gain $green_gain $blue_gain
    redraw
}

proc image_get_color {name} {
    set gains [imageColorGains $name]
    dict create \
        grayscale [imageGrayscale $name] \
        red_gain [lindex $gains 0] \
        green_gain [lindex $gains 1] \
        blue_gain [lindex $gains 2]
}

# Effects adjustment (invert, threshold)
proc image_set_effects {name invert thresh_on thresh_val} {
    imageInvert $name $invert
    imageThreshold $name $thresh_on $thresh_val
    redraw
}

proc image_get_effects {name} {
    set thresh [imageThreshold $name]
    dict create \
        invert [imageInvert $name] \
        thresh_on [lindex $thresh 0] \
        thresh_val [lindex $thresh 1]
}

# Opacity adjustment
proc image_set_opacity {name opacity} {
    imageOpacity $name $opacity
    redraw
}

proc image_get_opacity {name} {
    dict create opacity [imageOpacity $name]
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Main setup - filename requires reload
workspace::setup image_setup {
    filename {choice {backpack.png movie_ticket.png} backpack.png "Image"}
} -adjusters {img_scale img_rotation img_levels img_color img_effects img_opacity} \
  -label "Image Demo"

# Transform adjusters (built-in templates)
workspace::adjuster img_scale -template size2d -target img \
    -defaults {width 5.0 height 5.0}
workspace::adjuster img_rotation -template rotation -target img

# Levels adjuster (custom)
workspace::adjuster img_levels {
    brightness {float -1.0 1.0 0.01 0.0 "Brightness"}
    contrast   {float 0.0 3.0 0.01 1.0 "Contrast"}
    gamma      {float 0.1 3.0 0.01 1.0 "Gamma"}
} -target img -proc image_set_levels -getter image_get_levels -label "Levels"

# Color adjuster (custom)
workspace::adjuster img_color {
    grayscale  {bool 0 "Grayscale"}
    red_gain   {float 0.0 2.0 0.01 1.0 "Red Gain"}
    green_gain {float 0.0 2.0 0.01 1.0 "Green Gain"}
    blue_gain  {float 0.0 2.0 0.01 1.0 "Blue Gain"}
} -target img -proc image_set_color -getter image_get_color -label "Color"

# Effects adjuster (custom)
workspace::adjuster img_effects {
    invert     {bool 0 "Invert"}
    thresh_on  {bool 0 "Threshold Enable"}
    thresh_val {float 0.0 1.0 0.01 0.5 "Threshold Level"}
} -target img -proc image_set_effects -getter image_get_effects -label "Effects"

# Opacity adjuster (custom)
workspace::adjuster img_opacity {
    opacity {float 0.0 1.0 0.01 1.0 "Opacity"}
} -target img -proc image_set_opacity -getter image_get_opacity -label "Opacity"
