# examples/svg/svg_dynamic.tcl
# Dynamic SVG creation from inline data
# Demonstrates: svg from string data, procedural generation, color tinting
#
# Creates SVG graphics on-the-fly without external files.
# Useful for: fixation targets, geometric stimuli, procedural patterns

# ============================================================
# STIM CODE - Copy this section to your project
# ============================================================

# Simple crosshair/fixation target
proc svg_crosshair_data {size stroke_width color} {
    set half [expr {$size / 2.0}]
    return [subst {<svg xmlns="http://www.w3.org/2000/svg" width="$size" height="$size" viewBox="0 0 $size $size">
  <line x1="$half" y1="0" x2="$half" y2="$size" stroke="$color" stroke-width="$stroke_width"/>
  <line x1="0" y1="$half" x2="$size" y2="$half" stroke="$color" stroke-width="$stroke_width"/>
</svg>}]
}

# Concentric circles (like a target)
proc svg_target_data {size rings color} {
    set cx [expr {$size / 2.0}]
    set cy $cx
    set max_r [expr {$size / 2.0 - 2}]
    set svg "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"$size\" height=\"$size\" viewBox=\"0 0 $size $size\">\n"
    for {set i $rings} {$i >= 1} {incr i -1} {
        set r [expr {$max_r * $i / double($rings)}]
        set fill [expr {$i % 2 == 1 ? $color : "none"}]
        set stroke [expr {$i % 2 == 0 ? $color : "none"}]
        append svg "  <circle cx=\"$cx\" cy=\"$cy\" r=\"$r\" fill=\"$fill\" stroke=\"$stroke\" stroke-width=\"2\"/>\n"
    }
    append svg "</svg>"
    return $svg
}

# Star shape
proc svg_star_data {size points color} {
    set cx [expr {$size / 2.0}]
    set cy $cx
    set outer_r [expr {$size / 2.0 - 2}]
    set inner_r [expr {$outer_r * 0.4}]
    
    set path "M"
    for {set i 0} {$i < $points * 2} {incr i} {
        set angle [expr {3.14159265 * $i / $points - 3.14159265/2}]
        set r [expr {$i % 2 == 0 ? $outer_r : $inner_r}]
        set x [expr {$cx + $r * cos($angle)}]
        set y [expr {$cy + $r * sin($angle)}]
        if {$i > 0} {append path " L"}
        append path " $x $y"
    }
    append path " Z"
    
    return [subst {<svg xmlns="http://www.w3.org/2000/svg" width="$size" height="$size" viewBox="0 0 $size $size">
  <path d="$path" fill="$color"/>
</svg>}]
}

proc svg_dynamic_setup {shape} {
    glistInit 1
    resetObjList
    
    # Generate SVG data based on shape selection
    switch $shape {
        crosshair {
            set svg_data [svg_crosshair_data 100 4 "#00FF00"]
        }
        target {
            set svg_data [svg_target_data 100 5 "#FF6600"]
        }
        star {
            set svg_data [svg_star_data 100 5 "#FFDD00"]
        }
    }
    
    # Create SVG from inline data
    set s [svg $svg_data]
    objName $s shape_svg
    
    # Wrap in metagroup for transforms
    set mg [metagroup]
    metagroupAdd $mg $s
    objName $mg shape
    scaleObj $mg 5.0 5.0
    
    glistAddObject $mg 0
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ---- Adjuster helper procs ----

proc svg_set_color_tint {name mode r g b a} {
    svgColor $name $mode $r $g $b $a
    redraw
}

proc svg_get_color_tint {name} {
    set c [svgColor $name]
    dict create \
        mode [lindex $c 0] \
        r [lindex $c 1] \
        g [lindex $c 2] \
        b [lindex $c 3] \
        a [lindex $c 4]
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup svg_dynamic_setup {
    shape {choice {crosshair target star} crosshair "Shape"}
} -adjusters {shape_scale shape_rotation shape_position color_tint} \
  -label "SVG Dynamic Shapes"

# Transform adjusters
workspace::adjuster shape_scale -template scale -target shape \
    -defaults {scale 5.0} -label "Scale"

workspace::adjuster shape_rotation -template rotation -target shape \
    -label "Rotation"

workspace::adjuster shape_position -template position -target shape \
    -label "Position"

# Color tinting adjuster
# mode: 0=preserve original, 1=replace color, 2=multiply
workspace::adjuster color_tint {
    mode {choice {0 1 2} 0 "Mode (0=off, 1=replace, 2=mult)"}
    r {float 0.0 1.0 0.01 1.0 "Red"}
    g {float 0.0 1.0 0.01 1.0 "Green"}
    b {float 0.0 1.0 0.01 1.0 "Blue"}
    a {float 0.0 1.0 0.01 1.0 "Alpha"}
} -target shape_svg -proc svg_set_color_tint -getter svg_get_color_tint \
  -label "Color Tint" -colorpicker
