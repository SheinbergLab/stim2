# examples/text/text_basic.tcl
# Basic text rendering demonstration
# Demonstrates: text creation, fonts, sizes, colors, justification
#
# Uses metagroup to separate text object from display transforms:
#   - demo_text (text object): the rendered text
#   - demo_group (metagroup): position transforms
#
# The text module uses fontstash + stb_truetype for efficient
# GPU-accelerated text rendering with any TTF/OTF font.

# ============================================================
# STIM CODE - Copy this section to your project
# ============================================================

proc text_basic_setup {} {
    glistInit 1
    resetObjList
    
    # Load fonts
    textFont sans NotoSans-Regular.ttf
    textFont sansBold NotoSans-Bold.ttf
    textFont mono NotoSansMono-Regular.ttf
    
    # Create main text object
    set t [text "Hello, World!" -font sans -size 0.8]
    objName $t demo_text
    textColor $t 1.0 1.0 1.0 1.0
    textJustify $t center
    
    # Wrap in metagroup for positioning
    set mg [metagroup]
    metagroupAdd $mg $t
    objName $mg demo_group
    
    glistAddObject $mg 0
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ---- Adjuster helper procs ----

proc text_set_string {name str} {
    textString $name $str
    redraw
}

proc text_get_string {{name {}}} {
    dict create str [textString demo_text]
}

proc text_set_size {name size} {
    textSize $name $size
    redraw
}

proc text_get_size {{name {}}} {
    dict create size [textSize demo_text]
}

proc text_set_color {name r g b a} {
    textColor $name $r $g $b $a
    redraw
}

proc text_get_color {{name {}}} {
    # Return current values - for demo purposes use defaults
    dict create r 1.0 g 1.0 b 1.0 a 1.0
}

proc text_set_justify {name justify} {
    textJustify $name $justify
    redraw
}

proc text_get_justify {{name {}}} {
    dict create justify [textJustify demo_text]
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup text_basic_setup {} \
    -adjusters {text_string text_size text_justify text_color text_position} \
    -label "Text Basic"

# Text content
workspace::adjuster text_string {
    str {string "Hello, World!" "Text"}
} -target demo_text -proc text_set_string -getter text_get_string \
  -label "Text String"

# Font size
workspace::adjuster text_size {
    size {float 0.1 3.0 0.1 0.8 "Size" deg}
} -target demo_text -proc text_set_size -getter text_get_size \
  -label "Font Size"

# Justification
workspace::adjuster text_justify {
    justify {choice {left center right} center "Align"}
} -target demo_text -proc text_set_justify -getter text_get_justify \
  -label "Justification"

# Color
workspace::adjuster text_color {
    r {float 0.0 1.0 0.05 1.0 "Red"}
    g {float 0.0 1.0 0.05 1.0 "Green"}
    b {float 0.0 1.0 0.05 1.0 "Blue"}
    a {float 0.0 1.0 0.05 1.0 "Alpha"}
} -target demo_text -proc text_set_color -getter text_get_color \
  -label "Color" -colorpicker

# Position (on metagroup)
workspace::adjuster text_position -template position -target demo_group \
    -label "Position"
