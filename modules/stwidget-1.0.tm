# stwidget-1.0.tm
# Touch-based UI widget toolkit for stim2
#
# Provides dropdown, button, keyboard, and textinput widgets with polygon 
# graphics and touch event handling.
#
# Usage:
#   package require stwidget
#   stwidget::init
#   stwidget::dropdown::create mydd {"Option A" "Option B"} -x 0 -y 0 ...
#   stwidget::button::create mybtn "Click Me" -x 0 -y -2 ...
#   stwidget::textinput::create myinput -x 0 -y 1 -placeholder "Enter text"
#   stwidget::subscribe  ;# connect to dserv touch events
#
# Scale factors (default: widget_scale=1.5, keyboard_scale=2.0):
#   stwidget::set_scale 1.5 2.0    ;# set both scales (call before creating widgets)
#   stwidget::get_scale            ;# returns {widget_scale keyboard_scale}
#   Or set directly: set stwidget::widget_scale 1.5
#                    set stwidget::keyboard_scale 2.0
#
# After resetObjList, call stwidget::reset to clear widget state.
#
# Testing without touch hardware:
#   stwidget::route_touch $deg_x $deg_y $event_type
#   # event_type: 0=press, 1=move, 2=release

package provide stwidget 1.0

# ============================================================
# WIDGET BASE
# ============================================================

namespace eval stwidget {
    variable screen
    array set screen {
        width_px    1920
        height_px   1080
        ppd_x       50.0
        ppd_y       50.0
        half_deg_x  19.2
        half_deg_y  10.8
        scale_x     1.0
        scale_y     1.0
    }
    
    # Widget scale factors - adjust these to resize all widgets
    variable widget_scale 1.5      ;# General widget scale (buttons, dropdowns, etc.)
    variable keyboard_scale 2.0    ;# Keyboard-specific scale (often needs to be larger)
    
    variable handlers {}
    variable focus_widget ""
    variable focus_namespace ""
    variable initialized 0
}

proc stwidget::init {} {
    variable initialized
    if {$initialized} return
    
    load_screen_params
    catch { textFont mono NotoSansMono-Regular.ttf }
    
    set initialized 1
}

proc stwidget::set_scale {scale {kb_scale ""}} {
    # Set widget scale factors. If kb_scale not provided, uses scale value.
    # Call BEFORE creating widgets - does not affect already-created widgets.
    variable widget_scale
    variable keyboard_scale
    
    set widget_scale $scale
    if {$kb_scale eq ""} {
        set keyboard_scale $scale
    } else {
        set keyboard_scale $kb_scale
    }
}

proc stwidget::get_scale {} {
    # Returns list: {widget_scale keyboard_scale}
    variable widget_scale
    variable keyboard_scale
    return [list $widget_scale $keyboard_scale]
}

proc stwidget::reset {} {
    # Reset all widget subsystems - call after resetObjList
    catch {button::reset}
    catch {dropdown::reset}
    catch {keyboard::reset}
    catch {textinput::reset}
    
    # Clear focus
    variable focus_widget ""
    variable focus_namespace ""
}

proc stwidget::load_screen_params {} {
    variable screen
    
    set screen(width_px)    [screen_set WinWidth]
    set screen(height_px)   [screen_set WinHeight]
    set screen(scale_x)     [screen_set ScaleX]
    set screen(scale_y)     [screen_set ScaleY]
    set screen(half_deg_x)  [screen_set HalfScreenDegreeX]
    set screen(half_deg_y)  [screen_set HalfScreenDegreeY]
    set screen(ppd_x)       [expr {($screen(width_px) / 2.0) / $screen(half_deg_x)}]
    set screen(ppd_y)       [expr {($screen(height_px) / 2.0) / $screen(half_deg_y)}]
}

proc stwidget::refresh_screen_params {} {
    variable screen
    
    set new_w [screen_set WinWidth]
    set new_h [screen_set WinHeight]
    
    if {$new_w != $screen(width_px) || $new_h != $screen(height_px)} {
        load_screen_params
    }
}

# ---- Coordinate Conversion ----

proc stwidget::px_to_deg {px_x px_y} {
    variable screen
    set px_x [expr {$px_x * $screen(scale_x)}]
    set px_y [expr {$px_y * $screen(scale_y)}]
    set px_from_center_x [expr {$px_x - $screen(width_px) / 2.0}]
    set px_from_center_y [expr {$screen(height_px) / 2.0 - $px_y}]
    return [list \
        [expr {$px_from_center_x / $screen(ppd_x)}] \
        [expr {$px_from_center_y / $screen(ppd_y)}]]
}

proc stwidget::deg_to_px {deg_x deg_y} {
    variable screen
    return [list \
        [expr {int($deg_x * $screen(ppd_x) + $screen(width_px) / 2.0)}] \
        [expr {int($screen(height_px) / 2.0 - $deg_y * $screen(ppd_y))}]]
}

proc stwidget::ppd {} {
    variable screen
    return [expr {($screen(ppd_x) + $screen(ppd_y)) / 2.0}]
}

# ---- Event Routing ----

proc stwidget::register_handler {handler_ns {priority 100}} {
    variable handlers
    unregister_handler $handler_ns
    lappend handlers [list $handler_ns $priority]
    set handlers [lsort -index 1 -integer $handlers]
}

proc stwidget::unregister_handler {handler_ns} {
    variable handlers
    set new {}
    foreach entry $handlers {
        if {[lindex $entry 0] ne $handler_ns} {
            lappend new $entry
        }
    }
    set handlers $new
}

proc stwidget::route_touch {deg_x deg_y event_type} {
    variable handlers
    foreach entry $handlers {
        set ns [lindex $entry 0]
        if {[catch { ${ns}::on_touch $deg_x $deg_y $event_type } result]} {
            continue
        }
        if {$result} { return 1 }
    }
    return 0
}

proc stwidget::on_touch_event {px_x px_y event_type} {
    # Refresh screen params in case window was resized
    refresh_screen_params
    lassign [px_to_deg $px_x $px_y] deg_x deg_y
    return [route_touch $deg_x $deg_y $event_type]
}

proc stwidget::on_dserv_touch {args} {
    if {[llength $args] < 5} return
    lassign $args varname dtype timestamp dlen data
    
    dl_local vals [dl_create short]
    dl_fromString64 $data $vals
    set coords [dl_tcllist $vals]
    
    if {[llength $coords] < 3} return
    lassign $coords px_x px_y event_type
    on_touch_event $px_x $px_y $event_type
}

proc stwidget::subscribe {} {
    global dservhost dsCmds
    
    if {![info exists dservhost]} {
        puts "widget: dservhost not set, touch events disabled"
        return 0
    }
    
    if {[catch {qpcs::dsStimAddMatch $dservhost mtouch/event} err]} {
        puts "widget: subscribe failed: $err"
        return 0
    }
    
    set dsCmds(mtouch/event) stwidget::on_dserv_touch
    return 1
}

proc stwidget::unsubscribe {} {
    global dsCmds
    if {[info exists dsCmds(mtouch/event)] && 
        $dsCmds(mtouch/event) eq "stwidget::on_dserv_touch"} {
        unset dsCmds(mtouch/event)
    }
}

# ---- Focus Management ----

proc stwidget::set_focus {widget_name widget_ns} {
    variable focus_widget
    variable focus_namespace
    
    if {$focus_namespace ne "" && $focus_namespace ne $widget_ns} {
        catch { ${focus_namespace}::on_blur $focus_widget }
    }
    
    set focus_widget $widget_name
    set focus_namespace $widget_ns
    
    if {$widget_ns ne ""} {
        catch { ${widget_ns}::on_focus $widget_name }
    }
}

proc stwidget::clear_focus {} { set_focus "" "" }

proc stwidget::get_focus {} {
    variable focus_widget
    variable focus_namespace
    return [list $focus_widget $focus_namespace]
}

# ---- Hit Testing ----

proc stwidget::hit_rect {deg_x deg_y cx cy w h} {
    set hw [expr {$w / 2.0}]
    set hh [expr {$h / 2.0}]
    return [expr {
        $deg_x >= ($cx - $hw) && $deg_x <= ($cx + $hw) &&
        $deg_y >= ($cy - $hh) && $deg_y <= ($cy + $hh)
    }]
}

proc stwidget::hit_circle {deg_x deg_y cx cy r} {
    set dx [expr {$deg_x - $cx}]
    set dy [expr {$deg_y - $cy}]
    return [expr {sqrt($dx*$dx + $dy*$dy) <= $r}]
}

# ---- Color Utilities ----

proc stwidget::hex_to_rgb {hex} {
    set hex [string trimleft $hex "#"]
    return [list \
        [expr {[scan [string range $hex 0 1] %x] / 255.0}] \
        [expr {[scan [string range $hex 2 3] %x] / 255.0}] \
        [expr {[scan [string range $hex 4 5] %x] / 255.0}]]
}

proc stwidget::rgb_to_hex {r g b} {
    return [format "#%02x%02x%02x" \
        [expr {int($r * 255)}] \
        [expr {int($g * 255)}] \
        [expr {int($b * 255)}]]
}

# ============================================================
# BUTTON WIDGET
# ============================================================

namespace eval stwidget::button {
    variable widgets
    array set widgets {}
    variable initialized 0
}

proc stwidget::button::init {} {
    variable initialized
    if {$initialized} return
    stwidget::init
    stwidget::register_handler stwidget::button 200
    set initialized 1
}

proc stwidget::button::reset {} {
    variable widgets
    array unset widgets
    array set widgets {}
}

proc stwidget::button::create {name label args} {
    variable widgets
    variable initialized
    if {!$initialized} { init }
    
    set s $::stwidget::widget_scale
    array set opts [list \
        x 0.0  y 0.0  width [expr {2.5 * $s}]  height [expr {0.6 * $s}] \
        callback ""  font mono  font_size [expr {0.35 * $s}] \
        enabled 1  bg_color "#4a4a4a"  text_color "#ffffff" \
    ]
    foreach {key val} $args {
        set opts([string trimleft $key -]) $val
    }
    
    set widgets($name,label) $label
    foreach key {x y width height callback font font_size enabled bg_color text_color} {
        set widgets($name,$key) $opts($key)
    }
    set widgets($name,pressed) 0
    set widgets($name,objects) {}
    
    create_visuals $name
    return $name
}

proc stwidget::button::create_visuals {name} {
    variable widgets
    
    set w $widgets($name,width)
    set h $widgets($name,height)
    
    set mg [metagroup]
    objName $mg "${name}"
    lappend widgets($name,objects) $mg
    
    set bg [polygon]
    objName $bg "${name}_bg"
    scaleObj $bg $w $h
    priorityObj $bg 0.0
    metagroupAdd $mg $bg
    lappend widgets($name,objects) $bg
    
    apply_state $name
    
    set txt [text $widgets($name,label) -font $widgets($name,font) -size $widgets($name,font_size)]
    objName $txt "${name}_label"
    textJustify $txt center
    priorityObj $txt 1.0
    metagroupAdd $mg $txt
    lappend widgets($name,objects) $txt
    
    apply_text_color $name
    translateObj $mg $widgets($name,x) $widgets($name,y) 0
    glistAddObject $mg 0
}

proc stwidget::button::apply_state {name} {
    variable widgets
    lassign [stwidget::hex_to_rgb $widgets($name,bg_color)] r g b
    
    if {!$widgets($name,enabled)} {
        set r [expr {$r * 0.4}]
        set g [expr {$g * 0.4}]
        set b [expr {$b * 0.4}]
    } elseif {$widgets($name,pressed)} {
        set r [expr {min(1.0, $r * 1.4)}]
        set g [expr {min(1.0, $g * 1.4)}]
        set b [expr {min(1.0, $b * 1.6)}]
    }
    polycolor "${name}_bg" $r $g $b
}

proc stwidget::button::apply_text_color {name} {
    variable widgets
    if {$widgets($name,enabled)} {
        lassign [stwidget::hex_to_rgb $widgets($name,text_color)] r g b
        textColor "${name}_label" $r $g $b 1.0
    } else {
        textColor "${name}_label" 0.5 0.5 0.5 1.0
    }
}

proc stwidget::button::set_pressed {name pressed} {
    variable widgets
    if {!$widgets($name,enabled) || $widgets($name,pressed) == $pressed} return
    set widgets($name,pressed) $pressed
    apply_state $name
    redraw
}

proc stwidget::button::set_enabled {name enabled} {
    variable widgets
    if {$widgets($name,enabled) == $enabled} return
    set widgets($name,enabled) $enabled
    set widgets($name,pressed) 0
    apply_state $name
    apply_text_color $name
    redraw
}

proc stwidget::button::set_label {name label} {
    variable widgets
    set widgets($name,label) $label
    textString "${name}_label" $label
    redraw
}

proc stwidget::button::on_touch {deg_x deg_y event_type} {
    variable widgets
    
    foreach key [array names widgets "*,enabled"] {
        set name [string range $key 0 end-8]
        if {![stwidget::hit_rect $deg_x $deg_y \
                $widgets($name,x) $widgets($name,y) \
                $widgets($name,width) $widgets($name,height)]} continue
        if {!$widgets($name,enabled)} continue
        
        if {$event_type == 0} {
            set_pressed $name 1
            return 1
        } elseif {$event_type == 2 && $widgets($name,pressed)} {
            set_pressed $name 0
            if {$widgets($name,callback) ne ""} {
                {*}$widgets($name,callback) $name
            }
            return 1
        }
    }
    
    if {$event_type == 2} {
        foreach key [array names widgets "*,pressed"] {
            set name [string range $key 0 end-8]
            if {$widgets($name,pressed)} { set_pressed $name 0 }
        }
    }
    return 0
}

proc stwidget::button::destroy {name} {
    variable widgets
    if {[info exists widgets($name,objects)]} {
        foreach obj $widgets($name,objects) { catch { deleteObj $obj } }
    }
    foreach key [array names widgets "$name,*"] { unset widgets($key) }
}

proc stwidget::button::destroy_all {} {
    variable widgets
    foreach key [array names widgets "*,enabled"] {
        destroy [string range $key 0 end-8]
    }
}

# ============================================================
# DROPDOWN WIDGET
# ============================================================

namespace eval stwidget::dropdown {
    variable widgets
    array set widgets {}
    variable active_dropdown ""
    variable initialized 0
}

proc stwidget::dropdown::init {} {
    variable initialized
    if {$initialized} return
    stwidget::init
    stwidget::register_handler stwidget::dropdown 100
    set initialized 1
}

proc stwidget::dropdown::reset {} {
    variable widgets
    variable active_dropdown
    array unset widgets
    array set widgets {}
    set active_dropdown ""
}

proc stwidget::dropdown::create {name items args} {
    variable widgets
    variable initialized
    if {!$initialized} { init }
    
    set s $::stwidget::widget_scale
    array set opts [list \
        x 0.0  y 0.0  width [expr {4.0 * $s}]  item_height [expr {0.5 * $s}] \
        selected 0  callback ""  font mono  font_size [expr {0.35 * $s}] \
        max_visible 6  bg_color "#3a3a3a"  highlight_color "#4a6da7" \
    ]
    foreach {key val} $args {
        set opts([string trimleft $key -]) $val
    }
    
    if {[llength $items] == 0} { error "dropdown requires items" }
    if {$opts(selected) < 0 || $opts(selected) >= [llength $items]} {
        set opts(selected) 0
    }
    
    set widgets($name,items) $items
    set widgets($name,open) 0
    set widgets($name,hover_index) -1
    foreach key {x y width item_height selected callback font font_size max_visible bg_color highlight_color} {
        set widgets($name,$key) $opts($key)
    }
    set widgets($name,objects) {}
    
    create_header $name
    return $name
}

proc stwidget::dropdown::make_arrow {} {
    set p [polygon]
    dl_local x [dl_flist -0.5 0.5 0.0]
    dl_local y [dl_flist 0.3 0.3 -0.3]
    polyverts $p $x $y
    polytype $p triangles
    return $p
}

proc stwidget::dropdown::create_header {name} {
    variable widgets
    
    set w $widgets($name,width)
    set h $widgets($name,item_height)
    
    set mg [metagroup]
    objName $mg "dd_${name}"
    lappend widgets($name,objects) $mg
    
    set hdr [polygon]
    objName $hdr "dd_${name}_hdr"
    scaleObj $hdr $w $h
    lassign [stwidget::hex_to_rgb $widgets($name,bg_color)] r g b
    polycolor $hdr $r $g $b
    priorityObj $hdr 0.0
    metagroupAdd $mg $hdr
    lappend widgets($name,objects) $hdr
    
    set txt [text [lindex $widgets($name,items) $widgets($name,selected)] \
        -font $widgets($name,font) -size $widgets($name,font_size)]
    objName $txt "dd_${name}_text"
    textJustify $txt left
    textColor $txt 0.9 0.9 0.9 1.0
    translateObj $txt [expr {-$w/2.0 + 0.15 * $::stwidget::widget_scale}] 0 0
    priorityObj $txt 1.0
    metagroupAdd $mg $txt
    lappend widgets($name,objects) $txt
    
    set arrow [make_arrow]
    objName $arrow "dd_${name}_arrow"
    set arrow_size [expr {$h * 0.5}]
    scaleObj $arrow $arrow_size $arrow_size
    translateObj $arrow [expr {$w/2.0 - $h*0.4}] 0 0
    polycolor $arrow 0.7 0.7 0.7
    priorityObj $arrow 1.0
    metagroupAdd $mg $arrow
    lappend widgets($name,objects) $arrow
    
    translateObj $mg $widgets($name,x) $widgets($name,y) 0
    glistAddObject $mg 0
    
    create_popup $name
}

proc stwidget::dropdown::create_popup {name} {
    variable widgets
    
    set w $widgets($name,width)
    set h $widgets($name,item_height)
    set items $widgets($name,items)
    set num [llength $items]
    set vis [expr {min($num, $widgets($name,max_visible))}]
    
    set popup_h [expr {$vis * $h}]
    
    set popup [metagroup]
    objName $popup "dd_${name}_popup"
    set popup_y [expr {$widgets($name,y) - $h/2.0 - $popup_h/2.0}]
    translateObj $popup $widgets($name,x) $popup_y 0
    setVisible $popup 0
    lappend widgets($name,objects) $popup
    
    set frame [polygon]
    objName $frame "dd_${name}_frame"
    scaleObj $frame $w $popup_h
    priorityObj $frame 0.0
    metagroupAdd $popup $frame
    lappend widgets($name,objects) $frame
    lassign [stwidget::hex_to_rgb $widgets($name,bg_color)] r g b
    polycolor "dd_${name}_frame" $r $g $b
    
    set start_y [expr {($vis - 1) * $h / 2.0}]
    for {set i 0} {$i < $num} {incr i} {
        set iy [expr {$start_y - $i * $h}]
        
        set ibg [polygon]
        objName $ibg "dd_${name}_ibg_$i"
        scaleObj $ibg $w $h
        translateObj $ibg 0 $iy 0
        setVisible $ibg 0
        priorityObj $ibg 1.0
        lassign [stwidget::hex_to_rgb $widgets($name,highlight_color)] hr hg hb
        polycolor "dd_${name}_ibg_$i" $hr $hg $hb
        metagroupAdd $popup $ibg
        lappend widgets($name,objects) $ibg
        
        set itxt [text [lindex $items $i] -font $widgets($name,font) -size $widgets($name,font_size)]
        objName $itxt "dd_${name}_item_$i"
        textJustify $itxt left
        textColor $itxt 0.85 0.85 0.85 1.0
        translateObj $itxt [expr {-$w/2.0 + 0.15 * $::stwidget::widget_scale}] $iy 0
        priorityObj $itxt 2.0
        metagroupAdd $popup $itxt
        lappend widgets($name,objects) $itxt
    }
    
    glistAddObject $popup 0
}

proc stwidget::dropdown::apply_header_color {name} {
    variable widgets
    lassign [stwidget::hex_to_rgb $widgets($name,bg_color)] r g b
    if {$widgets($name,open)} {
        set r [expr {min(1.0, $r * 1.3)}]
        set g [expr {min(1.0, $g * 1.3)}]
        set b [expr {min(1.0, $b * 1.3)}]
    }
    polycolor "dd_${name}_hdr" $r $g $b
}

proc stwidget::dropdown::open {name} {
    variable widgets
    variable active_dropdown
    
    if {$active_dropdown ne "" && $active_dropdown ne $name} {
        close $active_dropdown
    }
    
    set widgets($name,open) 1
    set active_dropdown $name
    apply_header_color $name
    rotateObj "dd_${name}_arrow" 180 0 0 1
    priorityObj "dd_${name}_popup" 100.0
    setVisible "dd_${name}_popup" 1
    redraw
}

proc stwidget::dropdown::close {name} {
    variable widgets
    variable active_dropdown
    
    if {![info exists widgets($name,open)]} return
    
    set widgets($name,open) 0
    if {$active_dropdown eq $name} { set active_dropdown "" }
    
    apply_header_color $name
    rotateObj "dd_${name}_arrow" 0 0 0 1
    priorityObj "dd_${name}_popup" 0.0
    setVisible "dd_${name}_popup" 0
    clear_hover $name
    redraw
}

proc stwidget::dropdown::toggle {name} {
    variable widgets
    if {$widgets($name,open)} { close $name } else { open $name }
}

proc stwidget::dropdown::select {name index} {
    variable widgets
    set items $widgets($name,items)
    if {$index < 0 || $index >= [llength $items]} return
    
    set widgets($name,selected) $index
    textString "dd_${name}_text" [lindex $items $index]
    close $name
    
    if {$widgets($name,callback) ne ""} {
        {*}$widgets($name,callback) $name $index [lindex $items $index]
    }
}

proc stwidget::dropdown::set_hover {name index} {
    variable widgets
    set old $widgets($name,hover_index)
    if {$old == $index} return
    
    if {$old >= 0} { setVisible "dd_${name}_ibg_$old" 0 }
    set widgets($name,hover_index) $index
    if {$index >= 0} { setVisible "dd_${name}_ibg_$index" 1 }
    redraw
}

proc stwidget::dropdown::clear_hover {name} {
    variable widgets
    set old $widgets($name,hover_index)
    if {$old >= 0} { setVisible "dd_${name}_ibg_$old" 0 }
    set widgets($name,hover_index) -1
}

proc stwidget::dropdown::get_selected {name} {
    variable widgets
    return $widgets($name,selected)
}

proc stwidget::dropdown::get_selected_text {name} {
    variable widgets
    return [lindex $widgets($name,items) $widgets($name,selected)]
}

proc stwidget::dropdown::set_selected {name index} {
    variable widgets
    set items $widgets($name,items)
    if {$index < 0 || $index >= [llength $items]} return
    set widgets($name,selected) $index
    textString "dd_${name}_text" [lindex $items $index]
    redraw
}

proc stwidget::dropdown::on_touch {deg_x deg_y event_type} {
    variable widgets
    variable active_dropdown
    
    if {$event_type == 0} {
        foreach key [array names widgets "*,open"] {
            set name [string range $key 0 end-5]
            set x $widgets($name,x)
            set y $widgets($name,y)
            set w $widgets($name,width)
            set h $widgets($name,item_height)
            
            if {[stwidget::hit_rect $deg_x $deg_y $x $y $w $h]} {
                toggle $name
                return 1
            }
            
            if {$widgets($name,open)} {
                set items $widgets($name,items)
                set num [llength $items]
                set popup_top [expr {$y - $h}]
                
                if {$deg_x >= ($x - $w/2.0) && $deg_x <= ($x + $w/2.0)} {
                    for {set i 0} {$i < $num} {incr i} {
                        set iy [expr {$popup_top - $i * $h}]
                        if {$deg_y >= ($iy - $h/2.0) && $deg_y <= ($iy + $h/2.0)} {
                            select $name $i
                            return 1
                        }
                    }
                }
            }
        }
        
        if {$active_dropdown ne ""} {
            close $active_dropdown
            return 1
        }
    } elseif {$event_type == 1 && $active_dropdown ne ""} {
        set name $active_dropdown
        set x $widgets($name,x)
        set y $widgets($name,y)
        set w $widgets($name,width)
        set h $widgets($name,item_height)
        set items $widgets($name,items)
        set num [llength $items]
        set popup_top [expr {$y - $h}]
        
        set hover -1
        if {$deg_x >= ($x - $w/2.0) && $deg_x <= ($x + $w/2.0)} {
            for {set i 0} {$i < $num} {incr i} {
                set iy [expr {$popup_top - $i * $h}]
                if {$deg_y >= ($iy - $h/2.0) && $deg_y <= ($iy + $h/2.0)} {
                    set hover $i
                    break
                }
            }
        }
        set_hover $name $hover
        return 1
    }
    
    return 0
}

proc stwidget::dropdown::destroy {name} {
    variable widgets
    variable active_dropdown
    
    if {$active_dropdown eq $name} { set active_dropdown "" }
    if {[info exists widgets($name,objects)]} {
        foreach obj $widgets($name,objects) { catch { deleteObj $obj } }
    }
    foreach key [array names widgets "$name,*"] { unset widgets($key) }
}

proc stwidget::dropdown::destroy_all {} {
    variable widgets
    variable active_dropdown
    foreach key [array names widgets "*,open"] {
        destroy [string range $key 0 end-5]
    }
    set active_dropdown ""
}

# ============================================================
# KEYBOARD WIDGET
# ============================================================

namespace eval stwidget::keyboard {
    variable widgets
    array set widgets {}
    variable active_keyboard ""
    variable initialized 0
    
    variable layouts
    array set layouts {
        alpha_upper,0    "QWERTYUIOP"
        alpha_upper,1    "ASDFGHJKL"
        alpha_upper,2    "ZXCVBNM"
        alpha_upper,rows 3
        alpha_upper,offsets {0 0.5 1.5}
        
        alpha_lower,0    "qwertyuiop"
        alpha_lower,1    "asdfghjkl"
        alpha_lower,2    "zxcvbnm"
        alpha_lower,rows 3
        alpha_lower,offsets {0 0.5 1.5}
        
        numeric,0        "1234567890"
        numeric,1        "-/:;()$&@"
        numeric,2        ".,?!'"
        numeric,rows     3
        numeric,offsets  {0 0 2.5}
        
        symbol,0         "[]{}#%^*+="
        symbol,1         "_|~<>\\"
        symbol,2         ".,?!'"
        symbol,rows      3
        symbol,offsets   {0 0.5 2.5}
    }
}

proc stwidget::keyboard::init {} {
    variable initialized
    if {$initialized} return
    stwidget::init
    stwidget::register_handler stwidget::keyboard 50
    set initialized 1
}

proc stwidget::keyboard::reset {} {
    variable widgets
    variable active_keyboard
    array unset widgets
    array set widgets {}
    set active_keyboard ""
}

proc stwidget::keyboard::create {name args} {
    variable widgets
    variable initialized
    
    if {!$initialized} { init }
    
    set s $::stwidget::keyboard_scale
    array set opts [list \
        x 0.0  y -3.5 \
        key_size [expr {0.45 * $s}] \
        key_spacing 1.15 \
        callback "" \
        secure 0 \
        bg_color "#2a2a2a" \
        key_color "#3a3a3a" \
    ]
    foreach {key val} $args {
        set opts([string trimleft $key -]) $val
    }
    
    foreach key {x y key_size key_spacing callback secure bg_color key_color} {
        set widgets($name,$key) $opts($key)
    }
    
    set widgets($name,text) ""
    set widgets($name,mode) "alpha_lower"
    set widgets($name,shift) 0
    set widgets($name,visible) 0
    set widgets($name,objects) {}
    set widgets($name,key_objects) {}
    
    create_visuals $name
    return $name
}

proc stwidget::keyboard::create_visuals {name} {
    variable widgets
    variable layouts
    
    set ks $widgets($name,key_size)
    set sp $widgets($name,key_spacing)
    set key_w [expr {$ks * $sp}]
    set key_h [expr {$ks * $sp}]
    
    set mg [metagroup]
    objName $mg "kb_${name}"
    set widgets($name,metagroup) $mg
    lappend widgets($name,objects) $mg
    
    set kb_bg [polygon]
    objName $kb_bg "kb_${name}_bg"
    scaleObj $kb_bg [expr {12 * $key_w}] [expr {6 * $key_h}]
    translateObj $kb_bg 0 [expr {1.5 * $key_h}] 0
    lassign [stwidget::hex_to_rgb $widgets($name,bg_color)] r g b
    polycolor $kb_bg $r $g $b
    priorityObj $kb_bg 0.0
    metagroupAdd $mg $kb_bg
    lappend widgets($name,objects) $kb_bg
    
    set field_bg [polygon]
    objName $field_bg "kb_${name}_field_bg"
    scaleObj $field_bg [expr {10 * $key_w}] [expr {$ks * 1.4}]
    translateObj $field_bg 0 [expr {4.2 * $key_h}] 0
    polycolor $field_bg 0.15 0.15 0.15
    priorityObj $field_bg 1.0
    metagroupAdd $mg $field_bg
    lappend widgets($name,objects) $field_bg
    
    set display [text "" -font mono -size [expr {$ks * 1.0}]]
    objName $display "kb_${name}_display"
    textJustify $display center
    textColor $display 1.0 1.0 1.0 1.0
    translateObj $display 0 [expr {4.2 * $key_h}] 0
    priorityObj $display 2.0
    metagroupAdd $mg $display
    lappend widgets($name,objects) $display
    
    create_keys $name
    
    set bottom_y [expr {-$key_h}]
    lassign [stwidget::hex_to_rgb $widgets($name,key_color)] kr kg kb
    
    # Shift key
    set shift_bg [polygon]
    objName $shift_bg "kb_${name}_shift_bg"
    scaleObj $shift_bg [expr {$key_w * 1.4}] $key_h
    translateObj $shift_bg [expr {-5.2 * $key_w}] 0 0
    polycolor $shift_bg $kr $kg $kb
    priorityObj $shift_bg 1.0
    metagroupAdd $mg $shift_bg
    lappend widgets($name,objects) $shift_bg
    
    set shift [text "Shift" -font mono -size [expr {$ks * 0.55}]]
    objName $shift "kb_${name}_shift"
    textJustify $shift center
    textColor $shift 0.7 0.7 0.7 1.0
    translateObj $shift [expr {-5.2 * $key_w}] 0 0
    priorityObj $shift 2.0
    metagroupAdd $mg $shift
    lappend widgets($name,objects) $shift
    
    # Mode key
    set mode_bg [polygon]
    objName $mode_bg "kb_${name}_mode_bg"
    scaleObj $mode_bg [expr {$key_w * 1.4}] $key_h
    translateObj $mode_bg [expr {-5.2 * $key_w}] $bottom_y 0
    polycolor $mode_bg $kr $kg $kb
    priorityObj $mode_bg 1.0
    metagroupAdd $mg $mode_bg
    lappend widgets($name,objects) $mode_bg
    
    set mode_btn [text "123" -font mono -size [expr {$ks * 0.6}]]
    objName $mode_btn "kb_${name}_mode"
    textJustify $mode_btn center
    textColor $mode_btn 0.7 0.7 0.9 1.0
    translateObj $mode_btn [expr {-5.2 * $key_w}] $bottom_y 0
    priorityObj $mode_btn 2.0
    metagroupAdd $mg $mode_btn
    lappend widgets($name,objects) $mode_btn
    
    # Space bar
    set space_bg [polygon]
    objName $space_bg "kb_${name}_space_bg"
    scaleObj $space_bg [expr {$key_w * 5}] $key_h
    translateObj $space_bg 0 $bottom_y 0
    polycolor $space_bg $kr $kg $kb
    priorityObj $space_bg 1.0
    metagroupAdd $mg $space_bg
    lappend widgets($name,objects) $space_bg
    
    set space [text "space" -font mono -size [expr {$ks * 0.55}]]
    objName $space "kb_${name}_space"
    textJustify $space center
    textColor $space 0.8 0.8 0.8 1.0
    translateObj $space 0 $bottom_y 0
    priorityObj $space 2.0
    metagroupAdd $mg $space
    lappend widgets($name,objects) $space
    
    # Backspace
    set bksp_bg [polygon]
    objName $bksp_bg "kb_${name}_backspace_bg"
    scaleObj $bksp_bg [expr {$key_w * 1.4}] $key_h
    translateObj $bksp_bg [expr {5.2 * $key_w}] 0 0
    polycolor $bksp_bg $kr $kg $kb
    priorityObj $bksp_bg 1.0
    metagroupAdd $mg $bksp_bg
    lappend widgets($name,objects) $bksp_bg
    
    set bksp [text "Del" -font mono -size [expr {$ks * 0.6}]]
    objName $bksp "kb_${name}_backspace"
    textJustify $bksp center
    textColor $bksp 1.0 0.6 0.6 1.0
    translateObj $bksp [expr {5.2 * $key_w}] 0 0
    priorityObj $bksp 2.0
    metagroupAdd $mg $bksp
    lappend widgets($name,objects) $bksp
    
    # Done key
    set done_bg [polygon]
    objName $done_bg "kb_${name}_done_bg"
    scaleObj $done_bg [expr {$key_w * 1.4}] $key_h
    translateObj $done_bg [expr {5.2 * $key_w}] $bottom_y 0
    polycolor $done_bg 0.2 0.5 0.2
    priorityObj $done_bg 1.0
    metagroupAdd $mg $done_bg
    lappend widgets($name,objects) $done_bg
    
    set done [text "Done" -font mono -size [expr {$ks * 0.55}]]
    objName $done "kb_${name}_done"
    textJustify $done center
    textColor $done 0.6 1.0 0.6 1.0
    translateObj $done [expr {5.2 * $key_w}] $bottom_y 0
    priorityObj $done 2.0
    metagroupAdd $mg $done
    lappend widgets($name,objects) $done
    
    translateObj $mg $widgets($name,x) $widgets($name,y) 0
    setVisible $mg 0
    glistAddObject $mg 0
}

proc stwidget::keyboard::create_keys {name} {
    variable widgets
    variable layouts
    
    set mode $widgets($name,mode)
    set ks $widgets($name,key_size)
    set sp $widgets($name,key_spacing)
    set key_w [expr {$ks * $sp}]
    set key_h [expr {$ks * $sp}]
    set mg $widgets($name,metagroup)
    
    lassign [stwidget::hex_to_rgb $widgets($name,key_color)] kr kg kb
    
    set num_rows $layouts($mode,rows)
    set offsets $layouts($mode,offsets)
    
    for {set row 0} {$row < $num_rows} {incr row} {
        set chars $layouts($mode,$row)
        set row_offset [lindex $offsets $row]
        set num_keys [string length $chars]
        
        set row_width [expr {$num_keys * $key_w}]
        set start_x [expr {-$row_width/2.0 + $key_w/2.0 + $row_offset * $key_w}]
        set y [expr {(2 - $row) * $key_h}]
        
        for {set col 0} {$col < $num_keys} {incr col} {
            set char [string index $chars $col]
            set x [expr {$start_x + $col * $key_w}]
            
            set key_bg [polygon]
            objName $key_bg "kb_${name}_keybg_${row}_${col}"
            scaleObj $key_bg [expr {$key_w * 0.9}] [expr {$key_h * 0.9}]
            translateObj $key_bg $x $y 0
            polycolor $key_bg $kr $kg $kb
            priorityObj $key_bg 1.0
            metagroupAdd $mg $key_bg
            lappend widgets($name,key_objects) $key_bg
            
            set t [text $char -font mono -size $ks]
            objName $t "kb_${name}_key_${row}_${col}"
            textJustify $t center
            textColor $t 0.85 0.85 0.85 1.0
            translateObj $t $x $y 0
            priorityObj $t 2.0
            metagroupAdd $mg $t
            lappend widgets($name,key_objects) $t
        }
    }
}

proc stwidget::keyboard::rebuild_keys {name} {
    variable widgets
    
    set mg $widgets($name,metagroup)
    
    if {[info exists widgets($name,key_objects)]} {
        foreach obj $widgets($name,key_objects) {
            catch { metagroupRemove $mg $obj }
            catch { deleteObj $obj }
        }
    }
    set widgets($name,key_objects) {}
    
    create_keys $name
}

proc stwidget::keyboard::show {name {target_callback ""}} {
    variable widgets
    variable active_keyboard
    
    if {$active_keyboard ne "" && $active_keyboard ne $name} {
        hide $active_keyboard
    }
    
    set active_keyboard $name
    set widgets($name,visible) 1
    
    if {$target_callback ne ""} {
        set widgets($name,callback) $target_callback
    }
    
    priorityObj "kb_${name}" 200.0
    setVisible "kb_${name}" 1
    redraw
}

proc stwidget::keyboard::hide {name} {
    variable widgets
    variable active_keyboard
    
    if {![info exists widgets($name,visible)]} return
    
    set widgets($name,visible) 0
    if {$active_keyboard eq $name} {
        set active_keyboard ""
    }
    
    priorityObj "kb_${name}" 0.0
    setVisible "kb_${name}" 0
    redraw
}

proc stwidget::keyboard::update_display {name} {
    variable widgets
    
    set text $widgets($name,text)
    if {$widgets($name,secure)} {
        set display_text [string repeat "*" [string length $text]]
    } else {
        set display_text $text
    }
    
    textString "kb_${name}_display" $display_text
}

proc stwidget::keyboard::on_key {name key} {
    variable widgets
    
    switch $key {
        SPACE {
            append widgets($name,text) " "
        }
        BACKSPACE {
            if {[string length $widgets($name,text)] > 0} {
                set widgets($name,text) [string range $widgets($name,text) 0 end-1]
            }
        }
        SHIFT {
            toggle_shift $name
            return
        }
        MODE {
            cycle_mode $name
            return
        }
        DONE {
            set text $widgets($name,text)
            if {$widgets($name,callback) ne ""} {
                {*}$widgets($name,callback) $text
            }
            hide $name
            return
        }
        default {
            append widgets($name,text) $key
            if {$widgets($name,shift) && $widgets($name,mode) eq "alpha_upper"} {
                toggle_shift $name
                return
            }
        }
    }
    
    update_display $name
    redraw
}

proc stwidget::keyboard::toggle_shift {name} {
    variable widgets
    
    set widgets($name,shift) [expr {!$widgets($name,shift)}]
    
    if {$widgets($name,mode) eq "alpha_lower"} {
        set widgets($name,mode) "alpha_upper"
    } elseif {$widgets($name,mode) eq "alpha_upper"} {
        set widgets($name,mode) "alpha_lower"
    }
    
    if {$widgets($name,shift)} {
        textColor "kb_${name}_shift" 1.0 1.0 1.0 1.0
    } else {
        textColor "kb_${name}_shift" 0.7 0.7 0.7 1.0
    }
    
    rebuild_keys $name
    update_display $name
    redraw
}

proc stwidget::keyboard::cycle_mode {name} {
    variable widgets
    
    switch $widgets($name,mode) {
        alpha_lower - alpha_upper {
            set widgets($name,mode) "numeric"
            set widgets($name,shift) 0
            textString "kb_${name}_mode" "#+=";
            textColor "kb_${name}_shift" 0.7 0.7 0.7 1.0
        }
        numeric {
            set widgets($name,mode) "symbol"
            textString "kb_${name}_mode" "ABC"
        }
        symbol {
            set widgets($name,mode) "alpha_lower"
            set widgets($name,shift) 0
            textString "kb_${name}_mode" "123"
            textColor "kb_${name}_shift" 0.7 0.7 0.7 1.0
        }
    }
    
    rebuild_keys $name
    redraw
}

proc stwidget::keyboard::get_text {name} {
    variable widgets
    return $widgets($name,text)
}

proc stwidget::keyboard::set_text {name text} {
    variable widgets
    set widgets($name,text) $text
    update_display $name
    redraw
}

proc stwidget::keyboard::clear {name} {
    set_text $name ""
}

proc stwidget::keyboard::set_secure {name secure} {
    variable widgets
    set widgets($name,secure) $secure
    update_display $name
    redraw
}

proc stwidget::keyboard::hit_test {name deg_x deg_y} {
    variable widgets
    variable layouts
    
    if {!$widgets($name,visible)} { return "" }
    
    lassign [translateObj "kb_${name}"] kb_x kb_y kb_z
    
    set local_x [expr {$deg_x - $kb_x}]
    set local_y [expr {$deg_y - $kb_y}]
    
    set ks $widgets($name,key_size)
    set sp $widgets($name,key_spacing)
    set key_w [expr {$ks * $sp}]
    set key_h [expr {$ks * $sp}]
    set half_key_w [expr {$key_w / 2.0}]
    set half_key_h [expr {$key_h / 2.0}]
    
    set mode $widgets($name,mode)
    set num_rows $layouts($mode,rows)
    set offsets $layouts($mode,offsets)
    
    set bottom_y [expr {-$key_h}]
    
    if {[stwidget::hit_rect $local_x $local_y [expr {-5.2 * $key_w}] 0 [expr {$key_w * 1.4}] $key_h]} {
        return "SHIFT"
    }
    if {[stwidget::hit_rect $local_x $local_y [expr {5.2 * $key_w}] 0 [expr {$key_w * 1.4}] $key_h]} {
        return "BACKSPACE"
    }
    if {[stwidget::hit_rect $local_x $local_y [expr {-5.2 * $key_w}] $bottom_y [expr {$key_w * 1.4}] $key_h]} {
        return "MODE"
    }
    if {[stwidget::hit_rect $local_x $local_y 0 $bottom_y [expr {$key_w * 5}] $key_h]} {
        return "SPACE"
    }
    if {[stwidget::hit_rect $local_x $local_y [expr {5.2 * $key_w}] $bottom_y [expr {$key_w * 1.4}] $key_h]} {
        return "DONE"
    }
    
    for {set row 0} {$row < $num_rows} {incr row} {
        set chars $layouts($mode,$row)
        set row_offset [lindex $offsets $row]
        set num_keys [string length $chars]
        
        set row_width [expr {$num_keys * $key_w}]
        set start_x [expr {-$row_width/2.0 + $key_w/2.0 + $row_offset * $key_w}]
        set y [expr {(2 - $row) * $key_h}]
        
        if {abs($local_y - $y) <= $half_key_h} {
            for {set col 0} {$col < $num_keys} {incr col} {
                set x [expr {$start_x + $col * $key_w}]
                if {abs($local_x - $x) <= $half_key_w} {
                    return [string index $chars $col]
                }
            }
        }
    }
    
    return ""
}

proc stwidget::keyboard::on_touch {deg_x deg_y event_type} {
    variable widgets
    variable active_keyboard
    
    if {$active_keyboard eq ""} { return 0 }
    
    set name $active_keyboard
    if {![info exists widgets($name,visible)] || !$widgets($name,visible)} { return 0 }
    
    if {$event_type != 0} { return 0 }
    
    set key [hit_test $name $deg_x $deg_y]
    
    if {$key ne ""} {
        on_key $name $key
        return 1
    }
    
    return 0
}

proc stwidget::keyboard::destroy {name} {
    variable widgets
    variable active_keyboard
    
    if {$active_keyboard eq $name} { set active_keyboard "" }
    
    if {[info exists widgets($name,key_objects)]} {
        foreach obj $widgets($name,key_objects) { catch { deleteObj $obj } }
    }
    if {[info exists widgets($name,objects)]} {
        foreach obj $widgets($name,objects) { catch { deleteObj $obj } }
    }
    foreach key [array names widgets "$name,*"] { unset widgets($key) }
}

proc stwidget::keyboard::destroy_all {} {
    variable widgets
    variable active_keyboard
    foreach key [array names widgets "*,visible"] {
        destroy [string range $key 0 end-8]
    }
    set active_keyboard ""
}

# ============================================================
# TEXT INPUT WIDGET
# ============================================================

namespace eval stwidget::textinput {
    variable widgets
    array set widgets {}
    variable active_input ""
    variable shared_keyboard_created 0
    variable initialized 0
}

proc stwidget::textinput::init {} {
    variable initialized
    if {$initialized} return
    stwidget::init
    stwidget::register_handler stwidget::textinput 150
    set initialized 1
}

proc stwidget::textinput::reset {} {
    variable widgets
    variable active_input
    variable shared_keyboard_created
    
    array unset widgets
    array set widgets {}
    set active_input ""
    set shared_keyboard_created 0
}

proc stwidget::textinput::create {name args} {
    variable widgets
    variable shared_keyboard_created
    variable initialized
    
    if {!$initialized} { init }
    
    set s $::stwidget::widget_scale
    array set opts [list \
        x 0.0  y 0.0  width [expr {5.0 * $s}]  height [expr {0.6 * $s}] \
        placeholder "Enter text" \
        secure 0 \
        font mono  font_size [expr {0.35 * $s}] \
        callback "" \
        bg_color "#3a3a3a" \
        text_color "#ffffff" \
    ]
    foreach {key val} $args {
        set opts([string trimleft $key -]) $val
    }
    
    foreach key {x y width height placeholder secure font font_size callback bg_color text_color} {
        set widgets($name,$key) $opts($key)
    }
    
    set widgets($name,text) ""
    set widgets($name,focused) 0
    set widgets($name,objects) {}
    
    create_visuals $name
    
    # Create shared keyboard if it doesn't exist
    if {!$shared_keyboard_created || [catch {gobjName kb__shared}]} {
        catch {stwidget::keyboard::destroy _shared}
        stwidget::keyboard::create _shared -y -3.0
        set shared_keyboard_created 1
    }
    
    return $name
}

proc stwidget::textinput::create_visuals {name} {
    variable widgets
    
    set w $widgets($name,width)
    set h $widgets($name,height)
    
    set mg [metagroup]
    objName $mg "ti_${name}"
    lappend widgets($name,objects) $mg
    
    set bg [polygon]
    objName $bg "ti_${name}_bg"
    scaleObj $bg $w $h
    lassign [stwidget::hex_to_rgb $widgets($name,bg_color)] r g b
    polycolor $bg $r $g $b
    priorityObj $bg 0.0
    metagroupAdd $mg $bg
    lappend widgets($name,objects) $bg
    
    set txt [text $widgets($name,placeholder) -font $widgets($name,font) -size $widgets($name,font_size)]
    objName $txt "ti_${name}_text"
    textJustify $txt left
    textColor $txt 0.5 0.5 0.5 1.0
    translateObj $txt [expr {-$w/2.0 + 0.15 * $::stwidget::widget_scale}] 0 0
    priorityObj $txt 1.0
    metagroupAdd $mg $txt
    lappend widgets($name,objects) $txt
    
    translateObj $mg $widgets($name,x) $widgets($name,y) 0
    glistAddObject $mg 0
}

proc stwidget::textinput::focus {name} {
    variable widgets
    variable active_input
    
    if {$active_input ne "" && $active_input ne $name} {
        unfocus $active_input
    }
    
    set active_input $name
    set widgets($name,focused) 1
    
    lassign [stwidget::hex_to_rgb $widgets($name,bg_color)] r g b
    polycolor "ti_${name}_bg" [expr {min(1.0, $r * 1.4)}] [expr {min(1.0, $g * 1.4)}] [expr {min(1.0, $b * 1.4)}]
    
    stwidget::keyboard::set_secure _shared $widgets($name,secure)
    stwidget::keyboard::set_text _shared $widgets($name,text)
    stwidget::keyboard::show _shared [list stwidget::textinput::on_keyboard_done $name]
    
    redraw
}

proc stwidget::textinput::unfocus {name} {
    variable widgets
    variable active_input
    
    if {![info exists widgets($name,focused)]} return
    
    set widgets($name,focused) 0
    if {$active_input eq $name} {
        set active_input ""
    }
    
    lassign [stwidget::hex_to_rgb $widgets($name,bg_color)] r g b
    polycolor "ti_${name}_bg" $r $g $b
    
    redraw
}

proc stwidget::textinput::on_keyboard_done {name text} {
    variable widgets
    
    set widgets($name,text) $text
    unfocus $name
    update_display $name
    
    if {$widgets($name,callback) ne ""} {
        {*}$widgets($name,callback) $name $text
    }
    
    redraw
}

proc stwidget::textinput::update_display {name} {
    variable widgets
    
    set text $widgets($name,text)
    
    if {$text eq ""} {
        textString "ti_${name}_text" $widgets($name,placeholder)
        textColor "ti_${name}_text" 0.5 0.5 0.5 1.0
    } else {
        if {$widgets($name,secure)} {
            textString "ti_${name}_text" [string repeat "*" [string length $text]]
        } else {
            textString "ti_${name}_text" $text
        }
        lassign [stwidget::hex_to_rgb $widgets($name,text_color)] r g b
        textColor "ti_${name}_text" $r $g $b 1.0
    }
}

proc stwidget::textinput::get_text {name} {
    variable widgets
    return $widgets($name,text)
}

proc stwidget::textinput::set_text {name text} {
    variable widgets
    set widgets($name,text) $text
    update_display $name
    redraw
}

proc stwidget::textinput::clear {name} {
    set_text $name ""
}

proc stwidget::textinput::on_touch {deg_x deg_y event_type} {
    variable widgets
    variable active_input
    
    if {$event_type != 0} { return 0 }
    
    foreach key [array names widgets "*,placeholder"] {
        set name [string range $key 0 end-12]
        set x $widgets($name,x)
        set y $widgets($name,y)
        set w $widgets($name,width)
        set h $widgets($name,height)
        
        if {[stwidget::hit_rect $deg_x $deg_y $x $y $w $h]} {
            focus $name
            return 1
        }
    }
    
    return 0
}

proc stwidget::textinput::destroy {name} {
    variable widgets
    variable active_input
    
    if {$active_input eq $name} {
        set active_input ""
        stwidget::keyboard::hide _shared
    }
    
    if {[info exists widgets($name,objects)]} {
        foreach obj $widgets($name,objects) { catch { deleteObj $obj } }
    }
    foreach key [array names widgets "$name,*"] { unset widgets($key) }
}

proc stwidget::textinput::destroy_all {} {
    variable widgets
    variable active_input
    variable shared_keyboard_created
    
    foreach key [array names widgets "*,placeholder"] {
        destroy [string range $key 0 end-12]
    }
    set active_input ""
    
    if {$shared_keyboard_created} {
        stwidget::keyboard::destroy _shared
        set shared_keyboard_created 0
    }
}
