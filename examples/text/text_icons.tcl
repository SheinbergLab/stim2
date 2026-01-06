# examples/text/text_icons.tcl
# Icon fonts demonstration
# Demonstrates: Font Awesome and Material Icons usage
#
# Icon fonts render scalable vector icons as text glyphs:
#   - Font Awesome: Uses Unicode codepoints (\uf015 = home)
#   - Material Icons: Uses ligatures ("home" = home icon)
#
# Both approaches allow icons to be styled like text
# (size, color, transforms) with GPU-accelerated rendering.

# ============================================================
# ICON DEFINITIONS
# ============================================================

# Font Awesome codepoints (subset)
namespace eval ::fa {
    variable home "\uf015"
    variable gear "\uf013"
    variable play "\uf04b"
    variable pause "\uf04c"
    variable stop "\uf04d"
    variable check "\uf00c"
    variable xmark "\uf00d"
    variable warning "\uf071"
    variable info "\uf05a"
    variable refresh "\uf021"
}

# Material Icons ligatures
namespace eval ::mat {
    variable home "home"
    variable settings "settings"
    variable play "play_arrow"
    variable pause "pause"
    variable stop "stop"
    variable check "check"
    variable close "close"
    variable warning "warning"
    variable info "info"
    variable refresh "refresh"
}

# ============================================================
# STIM CODE - Copy this section to your project
# ============================================================

namespace eval icons_demo {
    variable current_set "fontawesome"
    variable icon_size 0.6
    variable icon_names {home gear play pause stop check xmark warning info refresh}
    variable icon_colors {
        {1.0 1.0 1.0}
        {0.7 0.7 0.7}
        {0.2 1.0 0.2}
        {1.0 1.0 0.2}
        {1.0 0.2 0.2}
        {0.2 1.0 0.2}
        {1.0 0.2 0.2}
        {1.0 0.8 0.2}
        {0.2 0.6 1.0}
        {0.5 0.8 1.0}
    }
}

proc text_icons_setup {} {
    glistInit 1
    resetObjList
    
    # Load fonts
    textFont sans NotoSans-Regular.ttf
    
    # Try to load icon fonts (may not be available)
    set icons_demo::fa_available [expr {![catch {textFont icons FontAwesome-Solid.otf}]}]
    set icons_demo::mat_available [expr {![catch {textFont material MaterialIcons-Regular.ttf}]}]
    
    if {!$icons_demo::fa_available && !$icons_demo::mat_available} {
        # Fallback: just show message
        set msg [text "Icon fonts not found" -font sans -size 0.5]
        textColor $msg 1.0 0.5 0.5 1.0
        textJustify $msg center
        glistAddObject $msg 0
        glistSetVisible 1
        glistSetCurGroup 0
        redraw
        return
    }
    
    # Title
    set title [text "Icon Fonts Demo" -font sans -size 0.6]
    objName $title icons_title
    textColor $title 1.0 1.0 1.0 1.0
    textJustify $title center
    translateObj $title 0 4
    glistAddObject $title 0
    
    # Font Awesome row
    if {$icons_demo::fa_available} {
        set label [text "Font Awesome:" -font sans -size 0.3]
        textColor $label 0.6 0.6 0.6 1.0
        textJustify $label left
        translateObj $label -5.5 2
        glistAddObject $label 0
        
        set x -3.5
        set idx 0
        foreach name $icons_demo::icon_names {
            set codepoint [set ::fa::[lindex {home gear play pause stop check xmark warning info refresh} $idx]]
            set icon [text $codepoint -font icons -size $icons_demo::icon_size]
            objName $icon fa_icon_$idx
            lassign [lindex $icons_demo::icon_colors $idx] r g b
            textColor $icon $r $g $b 1.0
            textJustify $icon center
            translateObj $icon $x 2
            glistAddObject $icon 0
            set x [expr {$x + 1.0}]
            incr idx
        }
    }
    
    # Material Icons row
    if {$icons_demo::mat_available} {
        set label [text "Material Icons:" -font sans -size 0.3]
        textColor $label 0.6 0.6 0.6 1.0
        textJustify $label left
        translateObj $label -5.5 0
        glistAddObject $label 0
        
        set x -3.5
        set idx 0
        foreach name {home settings play pause stop check close warning info refresh} {
            set ligature [set ::mat::$name]
            set icon [text $ligature -font material -size $icons_demo::icon_size]
            objName $icon mat_icon_$idx
            lassign [lindex $icons_demo::icon_colors $idx] r g b
            textColor $icon $r $g $b 1.0
            textJustify $icon center
            translateObj $icon $x 0
            glistAddObject $icon 0
            set x [expr {$x + 1.0}]
            incr idx
        }
    }
    
    # Instructions
    set instr [text "Icons scale and color like text" -font sans -size 0.25]
    textColor $instr 0.5 0.5 0.5 1.0
    textJustify $instr center
    translateObj $instr 0 -2
    glistAddObject $instr 0
    
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ---- Adjuster helper procs ----

proc icons_set_size {size} {
    # Update all icons
    for {set i 0} {$i < 10} {incr i} {
        catch { textSize fa_icon_$i $size }
        catch { textSize mat_icon_$i $size }
    }
    set icons_demo::icon_size $size
    redraw
}

proc icons_get_size {{target {}}} {
    dict create size $icons_demo::icon_size
}

proc icons_set_highlight {index} {
    # Reset all to original colors
    set idx 0
    foreach color $icons_demo::icon_colors {
        lassign $color r g b
        catch { textColor fa_icon_$idx $r $g $b 1.0 }
        catch { textColor mat_icon_$idx $r $g $b 1.0 }
        incr idx
    }
    
    # Highlight selected
    if {$index >= 0 && $index < 10} {
        catch { textColor fa_icon_$index 1.0 1.0 1.0 1.0 }
        catch { textColor mat_icon_$index 1.0 1.0 1.0 1.0 }
    }
    redraw
}

proc icons_get_highlight {{target {}}} {
    dict create index -1
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup text_icons_setup {} \
    -adjusters {icon_size icon_highlight} \
    -label "Icon Fonts"

# Icon size
workspace::adjuster icon_size {
    size {float 0.3 1.5 0.1 0.6 "Size" deg}
} -target {} -proc icons_set_size -getter icons_get_size \
  -label "Icon Size"

# Highlight selector
workspace::adjuster icon_highlight {
    index {int -1 9 1 -1 "Icon (-1=none)"}
} -target {} -proc icons_set_highlight -getter icons_get_highlight \
  -label "Highlight Icon"
