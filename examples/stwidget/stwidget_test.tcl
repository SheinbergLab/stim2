# stwidget_test.tcl
# Widget test demonstration - dropdown and button widgets
#
# Demonstrates:
#   - Dropdown menus with selection callbacks
#   - Buttons with press/release handling
#   - Form-style layout with labels
#   - Touch event routing
#
# Usage:
#   source stwidget_test.tcl
#   ;# Use workspace "Widget Demo" or call stwidget_test::setup directly
#
# Testing without touch hardware:
#   stwidget_test_click_deg x y    ;# simulate press at degree coords
#   stwidget_test_release_deg x y  ;# simulate release
#   stwidget_test_move_deg x y     ;# simulate move (for hover)
#   stwidget_test_demo             ;# run automated demo sequence

package require stwidget

namespace eval stwidget_test {
    variable result_text ""
}

# ============================================================
# MAIN SETUP
# ============================================================

proc stwidget_test::setup {} {
    glistInit 1
    resetObjList
    
    stwidget::init
    
    # ---- Title and instructions ----
    set title [text "Subject Information" -font mono -size 0.5]
    objName $title form_title
    textJustify $title center
    textColor $title 1.0 1.0 1.0 1.0
    translateObj $title 0 4.5 0
    glistAddObject $title 0
    
    set instr [text "Select options and press Continue" -font mono -size 0.28]
    objName $instr form_instr
    textJustify $instr center
    textColor $instr 0.7 0.7 0.7 1.0
    translateObj $instr 0 3.8 0
    glistAddObject $instr 0
    
    # ---- Handedness dropdown ----
    set lbl1 [text "Handedness:" -font mono -size 0.32]
    objName $lbl1 lbl_hand
    textJustify $lbl1 right
    textColor $lbl1 0.9 0.9 0.9 1.0
    translateObj $lbl1 -2.8 2.0 0
    glistAddObject $lbl1 0
    
    stwidget::dropdown::create handedness \
        {"Right-handed" "Left-handed" "Ambidextrous"} \
        -x 1.5 -y 2.0 -width 4.5 -item_height 0.55 \
        -callback stwidget_test::on_dropdown
    
    # ---- Age group dropdown ----
    set lbl2 [text "Age Group:" -font mono -size 0.32]
    objName $lbl2 lbl_age
    textJustify $lbl2 right
    textColor $lbl2 0.9 0.9 0.9 1.0
    translateObj $lbl2 -2.8 0.5 0
    glistAddObject $lbl2 0
    
    stwidget::dropdown::create age_group \
        {"18-24" "25-34" "35-44" "45-54" "55-64" "65+"} \
        -x 1.5 -y 0.5 -width 4.5 -item_height 0.55 \
        -callback stwidget_test::on_dropdown
    
    # ---- Buttons ----
    stwidget::button::create btn_continue "Continue" \
        -x 1.5 -y -2.5 -width 3.0 -height 0.7 \
        -bg_color "#2a6a2a" -callback stwidget_test::on_continue
    
    stwidget::button::create btn_cancel "Cancel" \
        -x -1.5 -y -2.5 -width 3.0 -height 0.7 \
        -bg_color "#6a2a2a" -callback stwidget_test::on_cancel
    
    # ---- Result display ----
    set result [text "" -font mono -size 0.32]
    objName $result form_result
    textJustify $result center
    textColor $result 0.5 1.0 0.5 1.0
    translateObj $result 0 -4.0 0
    glistAddObject $result 0
    
    # Connect to touch events (if dserv available)
    stwidget::subscribe
    
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ============================================================
# CALLBACKS
# ============================================================

proc stwidget_test::on_dropdown {name index text} {
    puts "Dropdown $name: selected \[$index\] \"$text\""
}

proc stwidget_test::on_continue {name} {
    set hand [stwidget::dropdown::get_selected_text handedness]
    set age [stwidget::dropdown::get_selected_text age_group]
    textString form_result "Selected: $hand, $age"
    puts "Continue pressed: $hand, $age"
    redraw
}

proc stwidget_test::on_cancel {name} {
    stwidget::dropdown::set_selected handedness 0
    stwidget::dropdown::set_selected age_group 0
    textString form_result "Cancelled"
    puts "Cancel pressed"
    redraw
}

# ============================================================
# TEST UTILITIES
# ============================================================

proc stwidget_test_click_deg {x y} {
    stwidget::route_touch $x $y 0
}

proc stwidget_test_release_deg {x y} {
    stwidget::route_touch $x $y 2
}

proc stwidget_test_move_deg {x y} {
    stwidget::route_touch $x $y 1
}

proc stwidget_test_demo {} {
    puts "\n=== Widget Demo ==="
    puts "1. Opening handedness dropdown..."
    stwidget_test_click_deg 1.5 2.0
    after 500
    
    puts "2. Selecting 'Left-handed'..."
    stwidget_test_click_deg 1.5 1.45
    after 300
    
    puts "3. Opening age group dropdown..."
    stwidget_test_click_deg 1.5 0.5
    after 500
    
    puts "4. Selecting '35-44'..."
    stwidget_test_click_deg 1.5 -0.6
    after 300
    
    puts "5. Clicking Continue..."
    stwidget_test_click_deg 1.5 -2.5
    stwidget_test_release_deg 1.5 -2.5
    
    puts "=== Demo Complete ===\n"
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================

workspace::reset

workspace::setup stwidget_test::setup {} \
    -adjusters {widget_actions} \
    -label "Widget Demo"

workspace::adjuster widget_actions {
    reset_form  {action "Reset Form"}
    open_hand   {action "Open Handedness"}
    open_age    {action "Open Age Group"}
    close_all   {action "Close Dropdowns"}
    disable_ok  {action "Disable Continue"}
    enable_ok   {action "Enable Continue"}
} -target {} -proc stwidget_test_action -label "Test Actions"

proc stwidget_test_action {action} {
    switch $action {
        reset_form {
            stwidget::dropdown::set_selected handedness 0
            stwidget::dropdown::set_selected age_group 0
            textString form_result ""
            redraw
        }
        open_hand {
            stwidget::dropdown::open handedness
        }
        open_age {
            stwidget::dropdown::open age_group
        }
        close_all {
            stwidget::dropdown::close handedness
            stwidget::dropdown::close age_group
        }
        disable_ok {
            stwidget::button::set_enabled btn_continue 0
        }
        enable_ok {
            stwidget::button::set_enabled btn_continue 1
        }
    }
}

puts ""
puts "Widget Demo loaded."
puts "  Use workspace 'Widget Demo' to run"
puts "  Or call: stwidget_test::setup"
puts "  Test touch: stwidget_test_click_deg x y"
puts ""
