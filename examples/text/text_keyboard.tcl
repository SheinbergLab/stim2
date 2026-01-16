# examples/text/text_keyboard.tcl
# Virtual keyboard demonstration
#
# Demonstrates:
#   - Touch-based text input using text objects
#   - Coordinate conversion (pixels to visual degrees)
#   - Hit testing for interactive elements
#   - Integration with dserv touch events (mtouch/event)
#
# The keyboard uses the text module to render keys and subscribes
# to touch events for input. Works with both touch screens and
# mouse input (via mtouch/event simulation).

namespace eval keyboard {
    # Configuration
    variable config
    array set config {
        origin_x     0
        origin_y     0
        key_size     0.5
        key_spacing  1.1
        callback     ""
        visible      0
    }
    
    # Screen info for coordinate conversion (populated from screen_set)
    variable screen
    array set screen {
        width_px     1920
        height_px    1080
        ppd_x        50.0
        ppd_y        50.0
        half_deg_x   19.2
        half_deg_y   10.8
    }
    
    # Layout definitions
    variable layouts
    array set layouts {
        qwerty,0     "QWERTYUIOP"
        qwerty,1     "ASDFGHJKL"
        qwerty,2     "ZXCVBNM"
        qwerty,rows  3
        qwerty,offsets {0 0.5 1.0}
        
        numeric,0    "123"
        numeric,1    "456"
        numeric,2    "789"
        numeric,3    " 0 "
        numeric,rows 4
        numeric,offsets {0 0 0 0}
    }
    
    variable current_layout "qwerty"
    variable typed_text ""
    variable objects {}
    variable display_obj ""
    variable initialized 0
}

# Load screen parameters from stim2's screen_set command
# Uses WinWidth/WinHeight for touch coordinate conversion since
# touch events are in window pixel space, not screen pixel space
proc keyboard::load_screen_params {} {
    variable screen
    
    # Window size (framebuffer size)
    set screen(width_px)   [screen_set WinWidth]
    set screen(height_px)  [screen_set WinHeight]
    
    # Scale factors (for HiDPI/Retina displays)
    # Mouse coordinates are in window coords, WinWidth/Height are framebuffer coords
    set screen(scale_x)    [screen_set ScaleX]
    set screen(scale_y)    [screen_set ScaleY]
    
    # Degrees per half-screen (defines the visual field)
    set screen(half_deg_x) [screen_set HalfScreenDegreeX]
    set screen(half_deg_y) [screen_set HalfScreenDegreeY]
    
    # Calculate pixels-per-degree based on window size
    # (window pixels / 2) / half_screen_degrees = ppd
    set screen(ppd_x) [expr {($screen(width_px) / 2.0) / $screen(half_deg_x)}]
    set screen(ppd_y) [expr {($screen(height_px) / 2.0) / $screen(half_deg_y)}]
}

# Convert pixel coordinates to visual degrees
# Touch/mouse coords: origin top-left, Y increases downward
# Stim coords: origin center, Y increases upward
# Note: mouse coords need scaling on HiDPI displays (scale_x/scale_y)
proc keyboard::px_to_deg {px_x px_y} {
    variable screen
    
    # Scale mouse coordinates to framebuffer coordinates (for HiDPI)
    set px_x [expr {$px_x * $screen(scale_x)}]
    set px_y [expr {$px_y * $screen(scale_y)}]
    
    # Pixels from center
    set px_from_center_x [expr {$px_x - $screen(width_px) / 2.0}]
    set px_from_center_y [expr {$screen(height_px) / 2.0 - $px_y}]  ;# Flip Y
    
    # Convert to degrees
    set deg_x [expr {$px_from_center_x / $screen(ppd_x)}]
    set deg_y [expr {$px_from_center_y / $screen(ppd_y)}]
    
    return [list $deg_x $deg_y]
}

# Convert degrees to pixels (for setting up touch windows if needed)
proc keyboard::deg_to_px {deg_x deg_y} {
    variable screen
    
    set px_x [expr {int($deg_x * $screen(ppd_x) + $screen(width_px) / 2.0)}]
    set px_y [expr {int($screen(height_px) / 2.0 - $deg_y * $screen(ppd_y))}]
    
    return [list $px_x $px_y]
}

# Create the keyboard visuals
proc keyboard::create {args} {
    variable config
    variable layouts
    variable current_layout
    variable objects
    variable display_obj
    variable initialized
    
    # Load screen parameters from stim2
    load_screen_params
    
    # Parse arguments
    foreach {opt val} $args {
        switch -- $opt {
            -x        { set config(origin_x) $val }
            -y        { set config(origin_y) $val }
            -size     { set config(key_size) $val }
            -spacing  { set config(key_spacing) $val }
            -callback { set config(callback) $val }
            -layout   { set current_layout $val }
        }
    }
    
    # Load font if needed
    catch { textFont mono NotoSansMono-Regular.ttf }
    
    # Clear any existing keyboard
    keyboard::destroy_objects
    
    set key_w [expr {$config(key_size) * $config(key_spacing)}]
    set key_h [expr {$config(key_size) * $config(key_spacing)}]
    
    set num_rows $layouts($current_layout,rows)
    set offsets $layouts($current_layout,offsets)
    
    # Create metagroup to hold all keyboard elements
    set mg [metagroup]
    objName $mg keyboard
    
    # Create text display area (position relative to keyboard origin 0,0)
    set display_y [expr {($num_rows + 1) * $key_h}]
    set display_obj [text "" -font mono -size $config(key_size)]
    objName $display_obj kb_display
    textJustify $display_obj center
    textColor $display_obj 1.0 1.0 1.0 1.0
    translateObj $display_obj 0 $display_y 0
    metagroupAdd $mg $display_obj
    lappend objects $display_obj
    
    # Create keys for each row
    for {set row 0} {$row < $num_rows} {incr row} {
        set chars $layouts($current_layout,$row)
        set row_offset [lindex $offsets $row]
        set num_keys [string length $chars]
        
        # Center the row
        set row_width [expr {$num_keys * $key_w}]
        set start_x [expr {-$row_width/2.0 + $key_w/2.0 + $row_offset * $key_w}]
        set y [expr {($num_rows - 1 - $row) * $key_h}]
        
        for {set col 0} {$col < $num_keys} {incr col} {
            set char [string index $chars $col]
            set x [expr {$start_x + $col * $key_w}]
            
            set t [text $char -font mono -size $config(key_size)]
            objName $t "kb_key_${row}_${col}"
            textJustify $t center
            textColor $t 0.8 0.8 0.8 1.0
            translateObj $t $x $y 0
            metagroupAdd $mg $t
            lappend objects $t
        }
    }
    
    # Create special keys
    set bottom_y [expr {-$key_h}]
    
    # Backspace
    set bksp [text "DEL" -font mono -size [expr {$config(key_size) * 0.6}]]
    objName $bksp kb_backspace
    textJustify $bksp center
    textColor $bksp 1.0 0.6 0.6 1.0
    translateObj $bksp [expr {4 * $key_w}] $bottom_y 0
    metagroupAdd $mg $bksp
    lappend objects $bksp
    
    # Space
    set space [text "SPACE" -font mono -size [expr {$config(key_size) * 0.6}]]
    objName $space kb_space
    textJustify $space center
    textColor $space 0.8 0.8 0.8 1.0
    translateObj $space 0 $bottom_y 0
    metagroupAdd $mg $space
    lappend objects $space
    
    # Enter/Done
    set enter [text "OK" -font mono -size [expr {$config(key_size) * 0.6}]]
    objName $enter kb_enter
    textJustify $enter center
    textColor $enter 0.6 1.0 0.6 1.0
    translateObj $enter [expr {-4 * $key_w}] $bottom_y 0
    metagroupAdd $mg $enter
    lappend objects $enter
    
    # Position the metagroup
    translateObj $mg $config(origin_x) $config(origin_y) 0
    
    # Add metagroup to glist (not individual objects)
    glistAddObject $mg 0
    
    set initialized 1
    set config(visible) 1
    
    redraw
}

# Destroy keyboard objects
proc keyboard::destroy_objects {} {
    variable objects
    variable initialized
    
    foreach obj $objects {
        catch { deleteObj $obj }
    }
    set objects {}
    set initialized 0
}

# Full cleanup
proc keyboard::destroy {} {
    keyboard::destroy_objects
    keyboard::unsubscribe
}

# Hit test - returns key character or special key name, empty if miss
proc keyboard::hit_test {deg_x deg_y} {
    variable config
    variable layouts
    variable current_layout
    
    # Get metagroup's current transform
    lassign [translateObj keyboard] mg_x mg_y mg_z
    lassign [scaleObj keyboard] scale_x scale_y scale_z
    
    # Transform touch coordinates into metagroup's local space
    # local = (world - origin) / scale
    set local_x [expr {($deg_x - $mg_x) / $scale_x}]
    set local_y [expr {($deg_y - $mg_y) / $scale_y}]
    
    # Now use original (unscaled) key layout for hit testing
    set key_w [expr {$config(key_size) * $config(key_spacing)}]
    set key_h [expr {$config(key_size) * $config(key_spacing)}]
    set half_key [expr {$key_w / 2.0}]
    
    set num_rows $layouts($current_layout,rows)
    set offsets $layouts($current_layout,offsets)
    
    # Check special keys first (bottom row) - positions relative to metagroup origin (0,0)
    set bottom_y [expr {-$key_h}]
    if {abs($local_y - $bottom_y) < $half_key} {
        # Space bar region (wider)
        if {abs($local_x) < [expr {$key_w * 1.5}]} {
            return "SPACE"
        }
        # Backspace
        if {abs($local_x - (4 * $key_w)) < $key_w} {
            return "BACKSPACE"
        }
        # Enter
        if {abs($local_x - (-4 * $key_w)) < $key_w} {
            return "ENTER"
        }
    }
    
    # Check letter keys
    for {set row 0} {$row < $num_rows} {incr row} {
        set chars $layouts($current_layout,$row)
        set row_offset [lindex $offsets $row]
        set num_keys [string length $chars]
        
        set row_width [expr {$num_keys * $key_w}]
        set start_x [expr {-$row_width/2.0 + $key_w/2.0 + $row_offset * $key_w}]
        set y [expr {($num_rows - 1 - $row) * $key_h}]
        
        # Check if in this row's Y range
        if {abs($local_y - $y) < $half_key} {
            # Check which column
            for {set col 0} {$col < $num_keys} {incr col} {
                set key_x [expr {$start_x + $col * $key_w}]
                if {abs($local_x - $key_x) < $half_key} {
                    return [string index $chars $col]
                }
            }
        }
    }
    
    return ""
}

# Direct touch event handler - can be called for testing with raw pixel coords
# px_x, px_y in pixels, event_type: 0=press, 1=drag, 2=release
proc keyboard::on_touch_event {px_x px_y event_type} {
    variable config
    variable initialized
    
    if {!$initialized || !$config(visible)} return
    
    # Only handle press events (type 0)
    if {$event_type != 0} return
    
    # Convert to degrees
    lassign [px_to_deg $px_x $px_y] deg_x deg_y
    
    # Hit test and process
    set key [hit_test $deg_x $deg_y]
    
    if {$key ne ""} {
        on_key_hit $key
    }
}

# Visual feedback - briefly highlight a key
proc keyboard::flash_key {key} {
    # Find the key object and flash it
    # This is a simple version - could be enhanced with animation
    
    switch -- $key {
        SPACE     { set obj kb_space }
        BACKSPACE { set obj kb_backspace }
        ENTER     { set obj kb_enter }
        default {
            # Find the letter key - would need to track which object
            # For now, skip flashing letters
            return
        }
    }
    
    catch {
        textColor $obj 1.0 1.0 1.0 1.0
        redraw
        after 100 [list keyboard::unflash_key $obj $key]
    }
}

proc keyboard::unflash_key {obj key} {
    catch {
        switch -- $key {
            SPACE     { textColor $obj 0.8 0.8 0.8 1.0 }
            BACKSPACE { textColor $obj 1.0 0.6 0.6 1.0 }
            ENTER     { textColor $obj 0.6 1.0 0.6 1.0 }
        }
        redraw
    }
}

# Subscribe to touch events via dserv
proc keyboard::subscribe {} {
    global dservhost dsCmds
    
    if {![info exists dservhost]} {
        puts "keyboard: dservhost not set, touch events disabled (use keyboard_test_click for testing)"
        return 0
    }
    
    # Subscribe to mtouch/event (catch in case qpcs not available)
    if {[catch {qpcs::dsStimAddMatch $dservhost mtouch/event} err]} {
        puts "keyboard: could not subscribe to touch events: $err"
        return 0
    }
    
    # Set callback
    set dsCmds(mtouch/event) keyboard::on_dserv_touch
    
    return 1
}

proc keyboard::unsubscribe {} {
    global dsCmds
    
    # Remove our callback
    if {[info exists dsCmds(mtouch/event)]} {
        if {$dsCmds(mtouch/event) eq "keyboard::on_dserv_touch"} {
            unset dsCmds(mtouch/event)
        }
    }
}

# Callback from dserv - args is the dsVals entry
# Format: varname type timestamp len {base64_data}
proc keyboard::on_dserv_touch {args} {
    variable config
    variable initialized
    
    if {!$initialized || !$config(visible)} return
    
    # Parse the datapoint
    # args = varname type timestamp len data
    if {[llength $args] < 5} return
    
    lassign $args varname dtype timestamp dlen data
    
    # Decode the base64 data to get x, y, event_type (3 shorts)
    dl_local vals [dl_create short]
    dl_fromString64 $data $vals
    set coords [dl_tcllist $vals]
    
    if {[llength $coords] < 3} return
    
    lassign $coords px_x px_y event_type
    
    # Only handle press events (type 0)
    if {$event_type != 0} return
    
    # Convert to degrees and process
    lassign [px_to_deg $px_x $px_y] deg_x deg_y
    
    # Hit test
    set key [hit_test $deg_x $deg_y]
    
    if {$key eq ""} return
    
    # Handle the key
    on_key_hit $key
}

# Process a key hit
proc keyboard::on_key_hit {key} {
    variable config
    variable typed_text
    
    switch -- $key {
        SPACE {
            append typed_text " "
        }
        BACKSPACE {
            if {[string length $typed_text] > 0} {
                set typed_text [string range $typed_text 0 end-1]
            }
        }
        ENTER {
            # Call the callback with final text
            if {$config(callback) ne ""} {
                {*}$config(callback) $typed_text
            }
            return
        }
        default {
            append typed_text $key
        }
    }
    
    # Update display
    textString kb_display $typed_text
    redraw
    
    # Visual feedback
    flash_key $key
}

# Show/hide
proc keyboard::show {} {
    variable config
    variable objects
    
    foreach obj $objects {
        catch { objSetVisible $obj 1 }
    }
    set config(visible) 1
    redraw
}

proc keyboard::hide {} {
    variable config
    variable objects
    
    foreach obj $objects {
        catch { objSetVisible $obj 0 }
    }
    set config(visible) 0
    redraw
}

# Get current typed text
proc keyboard::get_text {} {
    variable typed_text
    return $typed_text
}

# Clear typed text
proc keyboard::clear {} {
    variable typed_text
    set typed_text ""
    catch { textString kb_display "" }
    redraw
}

# Set initial text
proc keyboard::set_text {str} {
    variable typed_text
    set typed_text $str
    catch { textString kb_display $typed_text }
    redraw
}


# ============================================================
# SETUP PROCS
# ============================================================

proc setup_qwerty { {size 0.4} {y -0.5} } {
    glistInit 1
    resetObjList
    
    keyboard::create \
        -x 0 -y $y \
        -size $size \
        -layout qwerty \
        -callback keyboard_on_done
    
    keyboard::subscribe
    
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

proc setup_numeric { {size 0.5} {y -0.5} } {
    glistInit 1
    resetObjList
    
    keyboard::create \
        -x 0 -y $y \
        -size $size \
        -layout numeric \
        -callback keyboard_on_done
    
    keyboard::subscribe
    
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

proc keyboard_on_done {text} {
    puts "Input complete: '$text'"
    # In a real application, you would do something with the text here
    keyboard::clear
}

# ============================================================
# ADJUSTERS
# ============================================================

# Text adjuster - custom since it's not a standard transform
proc keyboard_set_text {str} {
    keyboard::set_text $str
}

proc keyboard_get_text {{target {}}} {
    dict create str [keyboard::get_text]
}

proc keyboard_clear_text {} {
    keyboard::clear
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

# QWERTY layout (main) - no setup params, use adjusters
workspace::setup setup_qwerty {} \
    -adjusters {kb_scale kb_position kb_text} \
    -label "QWERTY Keyboard"

# Scale and position adjusters target the metagroup "keyboard"
workspace::adjuster kb_scale -template scale -target keyboard
workspace::adjuster kb_position -template position -target keyboard

workspace::adjuster kb_text {
    str {string "" "Text"}
} -target {} -proc keyboard_set_text -getter keyboard_get_text \
  -label "Display Text"

# Numeric keypad variant
workspace::variant numeric {} \
    -proc setup_numeric -adjusters {num_scale num_position} \
    -label "Numeric Keypad"

workspace::adjuster num_scale -template scale -target keyboard
workspace::adjuster num_position -template position -target keyboard


# ============================================================
# STANDALONE TEST FUNCTIONS
# ============================================================

# For testing - simulate touch at pixel coords
proc keyboard_test_click {px_x px_y} {
    keyboard::on_touch_event $px_x $px_y 0
}

# For testing - simulate touch at degree coords  
proc keyboard_test_click_deg {deg_x deg_y} {
    lassign [keyboard::deg_to_px $deg_x $deg_y] px_x px_y
    keyboard::on_touch_event $px_x $px_y 0
}
