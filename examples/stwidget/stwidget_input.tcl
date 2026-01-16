# stwidget_input.tcl
# Text input demonstration - textinput and keyboard widgets
#
# Demonstrates:
#   - Text input fields with placeholder text
#   - Secure input (password masking)
#   - On-screen keyboard with letters, numbers, symbols
#   - Form submission pattern
#
# This mimics a typical system setup screen for entering
# WiFi credentials and user account information.

package require stwidget

namespace eval textinput_test {}

# ============================================================
# SETUP
# ============================================================

proc textinput_test::setup {} {
    glistInit 1
    resetObjList
    
    stwidget::reset
    stwidget::init
    
    # Title
    set title [text "System Setup" -font mono -size 0.5]
    objName $title form_title
    textJustify $title center
    textColor $title 1.0 1.0 1.0 1.0
    translateObj $title 0 5.5 0
    glistAddObject $title 0
    
    # ---- WiFi Section ----
    set wifi_label [text "WiFi Configuration" -font mono -size 0.35]
    objName $wifi_label wifi_section
    textJustify $wifi_label center
    textColor $wifi_label 0.7 0.85 1.0 1.0
    translateObj $wifi_label 0 4.5 0
    glistAddObject $wifi_label 0
    
    # SSID label
    set lbl_ssid [text "Network:" -font mono -size 0.3]
    objName $lbl_ssid lbl_ssid
    textJustify $lbl_ssid right
    textColor $lbl_ssid 0.8 0.8 0.8 1.0
    translateObj $lbl_ssid -3.2 3.5 0
    glistAddObject $lbl_ssid 0
    
    # SSID input
    stwidget::textinput::create wifi_ssid \
        -x 1.0 -y 3.5 -width 5.5 -height 0.55 \
        -placeholder "Network name (SSID)" \
        -callback textinput_test::on_field_changed
    
    # Password label
    set lbl_pass [text "Password:" -font mono -size 0.3]
    objName $lbl_pass lbl_pass
    textJustify $lbl_pass right
    textColor $lbl_pass 0.8 0.8 0.8 1.0
    translateObj $lbl_pass -3.2 2.5 0
    glistAddObject $lbl_pass 0
    
    # Password input (secure)
    stwidget::textinput::create wifi_pass \
        -x 1.0 -y 2.5 -width 5.5 -height 0.55 \
        -placeholder "WiFi password" \
        -secure 1 \
        -callback textinput_test::on_field_changed
    
    # ---- User Account Section ----
    set user_label [text "User Account" -font mono -size 0.35]
    objName $user_label user_section
    textJustify $user_label center
    textColor $user_label 0.7 0.85 1.0 1.0
    translateObj $user_label 0 1.3 0
    glistAddObject $user_label 0
    
    # Username label
    set lbl_user [text "Username:" -font mono -size 0.3]
    objName $lbl_user lbl_user
    textJustify $lbl_user right
    textColor $lbl_user 0.8 0.8 0.8 1.0
    translateObj $lbl_user -3.2 0.4 0
    glistAddObject $lbl_user 0
    
    # Username input
    stwidget::textinput::create username \
        -x 1.0 -y 0.4 -width 5.5 -height 0.55 \
        -placeholder "SSH username" \
        -callback textinput_test::on_field_changed
    
    # ---- Buttons ----
    stwidget::button::create btn_save "Save" \
        -x 1.8 -y -0.8 -width 2.5 -height 0.6 \
        -bg_color "#2a6a2a" \
        -callback textinput_test::on_save
    
    stwidget::button::create btn_clear "Clear" \
        -x -1.8 -y -0.8 -width 2.5 -height 0.6 \
        -bg_color "#5a5a5a" \
        -callback textinput_test::on_clear
    
    # Status display
    set status [text "Tap a field to enter text" -font mono -size 0.28]
    objName $status form_status
    textJustify $status center
    textColor $status 0.6 0.6 0.6 1.0
    translateObj $status 0 -1.7 0
    glistAddObject $status 0
    
    stwidget::subscribe
    
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ============================================================
# CALLBACKS
# ============================================================

proc textinput_test::on_field_changed {name text} {
    puts "Field '$name' changed: [string length $text] chars"
    update_status "Entered [string length $text] characters in $name"
}

proc textinput_test::on_save {name} {
    set ssid [stwidget::textinput::get_text wifi_ssid]
    set pass [stwidget::textinput::get_text wifi_pass]
    set user [stwidget::textinput::get_text username]
    
    if {$ssid eq "" || $pass eq "" || $user eq ""} {
        update_status "Please fill in all fields"
        return
    }
    
    puts "=== Configuration Saved ==="
    puts "  SSID: $ssid"
    puts "  Password: [string repeat * [string length $pass]]"
    puts "  Username: $user"
    puts "==========================="
    
    update_status "Configuration saved!"
}

proc textinput_test::on_clear {name} {
    stwidget::textinput::clear wifi_ssid
    stwidget::textinput::clear wifi_pass
    stwidget::textinput::clear username
    update_status "All fields cleared"
    puts "Fields cleared"
}

proc textinput_test::update_status {msg} {
    textString form_status $msg
    redraw
}

# ============================================================
# TEST UTILITIES
# ============================================================

proc textinput_test_click {x y} { stwidget::route_touch $x $y 0 }
proc textinput_test_release {x y} { stwidget::route_touch $x $y 2 }

# ============================================================
# WORKSPACE
# ============================================================

workspace::reset

workspace::setup textinput_test::setup {} \
    -adjusters {input_actions} \
    -label "Text Input Demo"

workspace::adjuster input_actions {
    focus_ssid  {action "Focus SSID"}
    focus_pass  {action "Focus Password"}
    focus_user  {action "Focus Username"}
    clear_all   {action "Clear All"}
    show_values {action "Show Values"}
} -target {} -proc textinput_test_action -label "Actions"

proc textinput_test_action {action} {
    switch $action {
        focus_ssid  { stwidget::textinput::focus wifi_ssid }
        focus_pass  { stwidget::textinput::focus wifi_pass }
        focus_user  { stwidget::textinput::focus username }
        clear_all   { textinput_test::on_clear "" }
        show_values {
            puts "Current values:"
            puts "  SSID: [stwidget::textinput::get_text wifi_ssid]"
            puts "  Pass: [stwidget::textinput::get_text wifi_pass]"
            puts "  User: [stwidget::textinput::get_text username]"
        }
    }
}

puts ""
puts "Text Input Demo loaded."
puts "  Use workspace 'Text Input Demo' to run"
puts "  Tap on input fields to open keyboard"
puts "  Use Shift for uppercase, 123 for numbers/symbols"
puts ""
