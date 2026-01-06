# examples/text/text_animate.tcl
# Animated text demonstration using the animate module
#
# Demonstrates:
#   - Text opacity pulsing via animateCustom
#   - Color cycling with hsv2rgb
#   - Dynamic text updates during animation
#   - Combining text with transform animations
#
# The animate module handles timing; text-specific properties
# (color, string) use animateCustom callbacks.

# ============================================================
# ANIMATION STATE
# ============================================================

namespace eval text_anim {
    variable color_cycle 0
    variable counter 0
    variable show_counter 0
}

# ============================================================
# ANIMATION PROCS
# ============================================================

# Text effects animation callback
proc anim_text_effects {t dt frame obj pulse_speed pulse_min pulse_max} {
    # Opacity pulsing
    if {$pulse_speed > 0} {
        set opacity [oscillate $t $pulse_speed $pulse_min $pulse_max]
        textColor anim_text 1.0 1.0 1.0 $opacity
    }
    
    # Color cycling
    if {$text_anim::color_cycle} {
        set hue [expr {fmod($t * 0.3, 1.0)}]
        lassign [hsv2rgb $hue 0.8 1.0] r g b
        # Preserve current alpha from pulse
        set opacity [oscillate $t $pulse_speed $pulse_min $pulse_max]
        textColor anim_text $r $g $b $opacity
    }
    
    # Counter display
    if {$text_anim::show_counter} {
        incr text_anim::counter
        textString counter_text "Frame: $text_anim::counter"
    }
}

# ============================================================
# SETUP
# ============================================================

proc text_animate_setup {} {
    glistInit 1
    resetObjList
    
    # Reset state
    set text_anim::counter 0
    
    # Load font
    textFont sans NotoSans-Regular.ttf
    textFont mono NotoSansMono-Regular.ttf
    
    # Main animated text
    set t [text "Animated Text" -font sans -size 0.8]
    objName $t anim_text
    textColor $t 1.0 1.0 1.0 1.0
    textJustify $t center
    
    # Wrap in metagroup for transform animations
    set mg [metagroup]
    metagroupAdd $mg $t
    objName $mg anim_group
    translateObj $mg 0 1.5
    
    glistAddObject $mg 0
    glistSetDynamic 0 1
    
    # Counter text (static position)
    set c [text "Frame: 0" -font mono -size 0.3]
    objName $c counter_text
    textColor $c 0.5 1.0 0.5 1.0
    textJustify $c center
    translateObj $c 0 -2
    glistAddObject $c 0
    
    glistSetCurGroup 0
    glistSetVisible 1
    
    # Start custom animation for text effects
    animateCustom anim_group -proc anim_text_effects \
        -params {pulse_speed 1.0 pulse_min 0.3 pulse_max 1.0}
    
    redraw
}

# ============================================================
# ADJUSTER PROCS
# ============================================================

proc textanim_set_string {str} {
    textString anim_text $str
    redraw
}

proc textanim_get_string {{target {}}} {
    dict create str [textString anim_text]
}

proc textanim_set_pulse {pulse_speed pulse_min pulse_max} {
    animateCustom anim_group -params [list \
        pulse_speed $pulse_speed \
        pulse_min $pulse_min \
        pulse_max $pulse_max]
}

proc textanim_get_pulse {{target {}}} {
    set state [animateCustom anim_group]
    if {[dict exists $state params]} {
        set params [dict get $state params]
        return [dict create \
            pulse_speed [dict get $params pulse_speed] \
            pulse_min [dict get $params pulse_min] \
            pulse_max [dict get $params pulse_max]]
    }
    return {pulse_speed 1.0 pulse_min 0.3 pulse_max 1.0}
}

proc textanim_set_color_cycle {enabled} {
    set text_anim::color_cycle $enabled
}

proc textanim_get_color_cycle {{target {}}} {
    dict create enabled $text_anim::color_cycle
}

proc textanim_set_counter {enabled} {
    set text_anim::show_counter $enabled
    if {!$enabled} {
        set text_anim::counter 0
        textString counter_text "Frame: 0"
    }
}

proc textanim_get_counter {{target {}}} {
    dict create enabled $text_anim::show_counter
}

proc textanim_set_rotation {speed} {
    if {$speed != 0} {
        animateRotation anim_group -speed $speed
    } else {
        animateRotation anim_group -stop 1
        rotateObj anim_group 0 0 0 1
    }
    return
}

proc textanim_get_rotation {{target {}}} {
    set state [animateRotation anim_group]
    if {[dict size $state] == 0} {
        return {speed 0.0}
    }
    dict create speed [dict get $state speed]
}

proc textanim_set_size {size} {
    textSize anim_text $size
    redraw
}

proc textanim_get_size {{target {}}} {
    dict create size [textSize anim_text]
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup text_animate_setup {} \
    -adjusters {text_content text_fontsize pulse_settings color_cycle_toggle counter_toggle rotation_speed} \
    -label "Text Animation"

# Text content
workspace::adjuster text_content {
    str {string "Animated Text" "Text"}
} -target {} -proc textanim_set_string -getter textanim_get_string \
  -label "Text String"

# Font size
workspace::adjuster text_fontsize {
    size {float 0.2 2.0 0.1 0.8 "Size" deg}
} -target {} -proc textanim_set_size -getter textanim_get_size \
  -label "Font Size"

# Pulse settings
workspace::adjuster pulse_settings {
    pulse_speed {float 0.0 5.0 0.1 1.0 "Frequency" Hz}
    pulse_min {float 0.0 1.0 0.05 0.3 "Min Alpha"}
    pulse_max {float 0.0 1.0 0.05 1.0 "Max Alpha"}
} -target {} -proc textanim_set_pulse -getter textanim_get_pulse \
  -label "Opacity Pulse"

# Color cycling toggle
workspace::adjuster color_cycle_toggle {
    enabled {bool 0 "Enable"}
} -target {} -proc textanim_set_color_cycle -getter textanim_get_color_cycle \
  -label "Color Cycle"

# Counter toggle
workspace::adjuster counter_toggle {
    enabled {bool 0 "Enable"}
} -target {} -proc textanim_set_counter -getter textanim_get_counter \
  -label "Frame Counter"

# Rotation
workspace::adjuster rotation_speed {
    speed {float -180.0 180.0 5.0 0.0 "Speed" deg/s}
} -target {} -proc textanim_set_rotation -getter textanim_get_rotation \
  -label "Rotation"
