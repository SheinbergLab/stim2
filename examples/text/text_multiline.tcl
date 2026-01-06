# examples/text/text_multiline.tcl
# Multiple text objects demonstration
# Demonstrates: multiple fonts, sizes, colors, vertical layout
#
# Shows how to create a typical text layout with:
#   - Title (large, bold)
#   - Subtitle (medium)
#   - Body text (normal)
#   - Monospace text (for data/code)

# ============================================================
# STIM CODE - Copy this section to your project
# ============================================================

proc text_multiline_setup {} {
    glistInit 1
    resetObjList
    
    # Load fonts
    textFont sans NotoSans-Regular.ttf
    textFont sansBold NotoSans-Bold.ttf
    textFont mono NotoSansMono-Regular.ttf
    
    # Title
    set title [text "Welcome to Stim2" -font sansBold -size 1.0]
    objName $title title_text
    textColor $title 1.0 1.0 1.0 1.0
    textJustify $title center
    translateObj $title 0 3
    glistAddObject $title 0
    
    # Subtitle
    set subtitle [text "Modern Stimulus Display" -font sans -size 0.5]
    objName $subtitle subtitle_text
    textColor $subtitle 0.7 0.8 1.0 1.0
    textJustify $subtitle center
    translateObj $subtitle 0 1.8
    glistAddObject $subtitle 0
    
    # Body text
    set body [text "Press any key to continue..." -font sans -size 0.35]
    objName $body body_text
    textColor $body 0.6 0.6 0.6 1.0
    textJustify $body center
    translateObj $body 0 0.5
    glistAddObject $body 0
    
    # Data display (monospace)
    set data [text "FPS: 60.0 | Frame: 0" -font mono -size 0.3]
    objName $data data_text
    textColor $data 0.4 1.0 0.4 1.0
    textJustify $data center
    translateObj $data 0 -1.0
    glistAddObject $data 0
    
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ---- Adjuster helper procs ----

proc multiline_set_title {str} {
    textString title_text $str
    redraw
}

proc multiline_get_title {{target {}}} {
    dict create str [textString title_text]
}

proc multiline_set_subtitle {str} {
    textString subtitle_text $str
    redraw
}

proc multiline_get_subtitle {{target {}}} {
    dict create str [textString subtitle_text]
}

proc multiline_set_body {str} {
    textString body_text $str
    redraw
}

proc multiline_get_body {{target {}}} {
    dict create str [textString body_text]
}

proc multiline_set_data {str} {
    textString data_text $str
    redraw
}

proc multiline_get_data {{target {}}} {
    dict create str [textString data_text]
}

proc multiline_set_title_color {r g b} {
    textColor title_text $r $g $b 1.0
    redraw
}

proc multiline_get_title_color {{target {}}} {
    dict create r 1.0 g 1.0 b 1.0
}

proc multiline_set_subtitle_color {r g b} {
    textColor subtitle_text $r $g $b 1.0
    redraw
}

proc multiline_get_subtitle_color {{target {}}} {
    dict create r 0.7 g 0.8 b 1.0
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup text_multiline_setup {} \
    -adjusters {title_content subtitle_content body_content data_content title_color subtitle_color} \
    -label "Text Multi-line"

# Text content adjusters
workspace::adjuster title_content {
    str {string "Welcome to Stim2" "Title"}
} -target {} -proc multiline_set_title -getter multiline_get_title \
  -label "Title Text"

workspace::adjuster subtitle_content {
    str {string "Modern Stimulus Display" "Subtitle"}
} -target {} -proc multiline_set_subtitle -getter multiline_get_subtitle \
  -label "Subtitle Text"

workspace::adjuster body_content {
    str {string "Press any key to continue..." "Body"}
} -target {} -proc multiline_set_body -getter multiline_get_body \
  -label "Body Text"

workspace::adjuster data_content {
    str {string "FPS: 60.0 | Frame: 0" "Data"}
} -target {} -proc multiline_set_data -getter multiline_get_data \
  -label "Data Text"

# Color adjusters
workspace::adjuster title_color {
    r {float 0.0 1.0 0.05 1.0 "Red"}
    g {float 0.0 1.0 0.05 1.0 "Green"}
    b {float 0.0 1.0 0.05 1.0 "Blue"}
} -target {} -proc multiline_set_title_color -getter multiline_get_title_color \
  -label "Title Color" -colorpicker

workspace::adjuster subtitle_color {
    r {float 0.0 1.0 0.05 0.7 "Red"}
    g {float 0.0 1.0 0.05 0.8 "Green"}
    b {float 0.0 1.0 0.05 1.0 "Blue"}
} -target {} -proc multiline_set_subtitle_color -getter multiline_get_subtitle_color \
  -label "Subtitle Color" -colorpicker
