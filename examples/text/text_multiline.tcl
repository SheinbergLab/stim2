# examples/text/text_multiline.tcl
# Multiline text and layout demonstration
#
# Demonstrates:
#   - Native multiline text with \n
#   - Word wrapping with -wrap option  
#   - Line spacing with -spacing option
#   - Vertical alignment with textValign
#   - Relative positioning with textAlign
#   - Layout queries with textBounds/textInfo
#
# The text module now supports multiline text natively.

# ============================================================
# SETUP PROCS
# ============================================================

# Main setup: stacked layout with textAlign
proc setup_stacked {} {
    glistInit 1
    resetObjList
    
    # Load fonts
    textFont sans NotoSans-Regular.ttf
    textFont sansBold NotoSans-Bold.ttf
    textFont mono NotoSansMono-Regular.ttf
    
    # Title - anchor point for layout
    set title [text "Welcome to Stim2" -font sansBold -size 1.0]
    objName $title title_text
    textColor $title 1.0 1.0 1.0 1.0
    textJustify $title center
    translateObj $title 0 3
    glistAddObject $title 0
    
    # Subtitle - aligned relative to title
    set subtitle [text "Modern Stimulus Display" -font sans -size 0.5]
    objName $subtitle subtitle_text
    textColor $subtitle 0.7 0.8 1.0 1.0
    textJustify $subtitle center
    textAlign $subtitle top $title bottom 0.3
    glistAddObject $subtitle 0
    
    # Instructions - multiline in a single text object
    set instr [text "Press SPACE to start\nPress Q to quit\nPress H for help" \
        -font sans -size 0.35 -spacing 1.4]
    objName $instr instr_text
    textColor $instr 0.6 0.6 0.6 1.0
    textJustify $instr center
    textAlign $instr top $subtitle bottom 0.5
    glistAddObject $instr 0
    
    # Data display (monospace) - aligned below instructions
    set data [text "FPS: 60.0 | Frame: 0" -font mono -size 0.3]
    objName $data data_text
    textColor $data 0.4 1.0 0.4 1.0
    textJustify $data center
    textAlign $data top $instr bottom 0.5
    glistAddObject $data 0
    
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# Word wrap demo
proc setup_wordwrap { {width 7.0} } {
    glistInit 1
    resetObjList
    
    textFont sans NotoSans-Regular.ttf
    textFont sansBold NotoSans-Bold.ttf
    
    set title [text "Word Wrap Demo" -font sansBold -size 0.6]
    objName $title title_text
    textJustify $title center
    translateObj $title 0 3.5
    glistAddObject $title 0
    
    set para [text "This paragraph demonstrates automatic word wrapping. The text flows to fit within the specified width, making it easy to display blocks of text without manually inserting line breaks." \
        -font sans -size 0.35 -wrap $width -spacing 1.3]
    objName $para para_text
    textJustify $para left
    textValign $para top
    textAlign $para top $title bottom 0.5
    textAlign $para centerx $title centerx   ;# Center horizontally with title
    glistAddObject $para 0
    
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# Two-column layout
proc setup_two_column {} {
    glistInit 1
    resetObjList
    
    textFont sans NotoSans-Regular.ttf
    textFont sansBold NotoSans-Bold.ttf
    
    # Left column
    set leftHead [text "Option A" -font sansBold -size 0.5]
    objName $leftHead left_head
    textJustify $leftHead center
    translateObj $leftHead -4 2
    glistAddObject $leftHead 0
    
    set leftBody [text "Fast processing\nLower accuracy\nBest for speed" \
        -font sans -size 0.3 -spacing 1.3]
    objName $leftBody left_body
    textJustify $leftBody center
    textAlign $leftBody top $leftHead bottom 0.3
    textAlign $leftBody centerx $leftHead centerx
    glistAddObject $leftBody 0
    
    # Right column - tops aligned with left
    set rightHead [text "Option B" -font sansBold -size 0.5]
    objName $rightHead right_head
    textJustify $rightHead center
    translateObj $rightHead 4 0
    textAlign $rightHead top $leftHead top
    glistAddObject $rightHead 0
    
    set rightBody [text "Slow processing\nHigher accuracy\nBest for precision" \
        -font sans -size 0.3 -spacing 1.3]
    objName $rightBody right_body
    textJustify $rightBody center
    textAlign $rightBody top $rightHead bottom 0.3
    textAlign $rightBody centerx $rightHead centerx
    glistAddObject $rightBody 0
    
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# Simple multiline test
proc setup_simple {} {
    glistInit 1
    resetObjList
    
    textFont sans NotoSans-Regular.ttf
    
    set t [text "Welcome to Stim2\n\nThis text automatically wraps to the specified width and supports multiple lines.\n\nPress any key to continue..." \
        -font sans -size 0.4 -wrap 8.0 -spacing 1.3]
    objName $t main_text
    textColor $t 1 1 1 1
    textJustify $t center
    textValign $t center
    
    glistAddObject $t 0
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ============================================================
# ADJUSTERS
# ============================================================

# Helper to re-align stacked elements
proc realign_stacked {} {
    textAlign subtitle_text top title_text bottom 0.3
    textAlign instr_text top subtitle_text bottom 0.5
    textAlign data_text top instr_text bottom 0.5
}

proc set_title {str} {
    textString title_text $str
    realign_stacked
    redraw
}

proc get_title {{target {}}} {
    dict create str [textString title_text]
}

proc set_instructions {str} {
    textString instr_text $str
    realign_stacked
    redraw
}

proc get_instructions {{target {}}} {
    dict create str [textString instr_text]
}

proc set_spacing {spacing} {
    textSpacing instr_text $spacing
    realign_stacked
    redraw
}

proc get_spacing {{target {}}} {
    dict create spacing [textSpacing instr_text]
}

proc set_wrap_width {width} {
    textWrap para_text $width
    redraw
}

proc get_wrap_width {{target {}}} {
    dict create width [textWrap para_text]
}

proc set_simple_text {str} {
    textString main_text $str
    redraw
}

proc get_simple_text {{target {}}} {
    dict create str [textString main_text]
}

proc set_simple_wrap {width} {
    textWrap main_text $width
    redraw
}

proc get_simple_wrap {{target {}}} {
    dict create width [textWrap main_text]
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

# Main setup: stacked layout
workspace::setup setup_stacked {} \
    -adjusters {stacked_title stacked_instr stacked_spacing} \
    -label "Stacked Layout"

workspace::adjuster stacked_title {
    str {string "Welcome to Stim2" "Title"}
} -target {} -proc set_title -getter get_title -label "Title"

workspace::adjuster stacked_instr {
    str {string "Press SPACE to start\nPress Q to quit\nPress H for help" "Instructions"}
} -target {} -proc set_instructions -getter get_instructions -label "Instructions"

workspace::adjuster stacked_spacing {
    spacing {float 1.0 2.5 0.1 1.4 "Spacing"}
} -target {} -proc set_spacing -getter get_spacing -label "Line Spacing"

# Word wrap variant
workspace::variant wordwrap {} \
    -proc setup_wordwrap -adjusters {wrap_adj} -label "Word Wrap"

workspace::adjuster wrap_adj {
    width {float 3.0 12.0 0.5 7.0 "Wrap Width" deg}
} -target {} -proc set_wrap_width -getter get_wrap_width -label "Wrap Width"

# Two-column variant
workspace::variant two_column {} \
    -proc setup_two_column -adjusters {} -label "Two Column"

# Simple multiline variant
workspace::variant simple {} \
    -proc setup_simple -adjusters {simple_text simple_wrap} -label "Simple Multiline"

workspace::adjuster simple_text {
    str {string "Welcome to Stim2\n\nMultiple lines\nwith word wrap." "Text"}
} -target {} -proc set_simple_text -getter get_simple_text -label "Text"

workspace::adjuster simple_wrap {
    width {float 0.0 12.0 0.5 8.0 "Wrap Width" deg}
} -target {} -proc set_simple_wrap -getter get_simple_wrap -label "Wrap Width"
