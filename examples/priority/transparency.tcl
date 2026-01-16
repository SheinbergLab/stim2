# examples/priority/transparency.tcl
# Transparency and Z-Order demonstration
# Demonstrates: alpha blending requires correct draw order (back-to-front)
#
# Shows overlapping semi-transparent circles. With proper priority ordering,
# colors blend correctly. Adjusters let you experiment with order and alpha.
#
# Key insight: OpenGL blending uses (src * alpha + dest * (1-alpha)).
# This only looks right when "dest" is already drawn - so transparent
# objects must be drawn back-to-front.

# ============================================================
# STIM CODE
# ============================================================

proc transparency_setup {} {
    glistInit 1
    resetObjList
    
    set mg [metagroup]
    objName $mg shapes
    
    # Red circle - left
    set r [polygon]
    polycirc $r 1
    objName $r red_circle
    polycolor $r 0.9 0.2 0.2 0.6
    scaleObj $r 2.5 2.5
    translateObj $r -1.2 0.5 0
    priorityObj $r 0.0
    metagroupAdd $mg $r
    
    # Green circle - right  
    set g [polygon]
    polycirc $g 1
    objName $g green_circle
    polycolor $g 0.2 0.8 0.3 0.6
    scaleObj $g 2.5 2.5
    translateObj $g 1.2 0.5 0
    priorityObj $g 1.0
    metagroupAdd $mg $g
    
    # Blue circle - bottom center (overlaps both)
    set b [polygon]
    polycirc $b 1
    objName $b blue_circle
    polycolor $b 0.2 0.4 0.9 0.6
    scaleObj $b 2.5 2.5
    translateObj $b 0.0 -0.8 0
    priorityObj $b 2.0
    metagroupAdd $mg $b
    
    glistAddObject $mg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# Variant with more circles in a ring pattern
proc transparency_ring_setup {} {
    glistInit 1
    resetObjList
    
    set mg [metagroup]
    objName $mg shapes
    
    set n 6
    set radius 2.0
    set colors {
        {0.9 0.2 0.2}
        {0.9 0.6 0.2}
        {0.9 0.9 0.2}
        {0.2 0.8 0.3}
        {0.2 0.4 0.9}
        {0.7 0.2 0.9}
    }
    
    for {set i 0} {$i < $n} {incr i} {
        set angle [expr {2.0 * 3.14159265 * $i / $n}]
        set x [expr {$radius * cos($angle)}]
        set y [expr {$radius * sin($angle)}]
        
        set p [polygon]
        polycirc $p 1
        objName $p "circle_$i"
        
        lassign [lindex $colors $i] cr cg cb
        polycolor $p $cr $cg $cb 0.5
        
        scaleObj $p 2.0 2.0
        translateObj $p $x $y 0
        priorityObj $p [expr {double($i)}]
        metagroupAdd $mg $p
    }
    
    glistAddObject $mg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# CUSTOM ADJUSTER PROCS
# ============================================================

proc set_priority {target priority} {
    priorityObj $target $priority
    redraw
    return
}

proc get_priority {target} {
    dict create priority [priorityObj $target]
}

proc set_alpha {target alpha} {
    set c [polycolor $target]
    lassign $c r g b
    polycolor $target $r $g $b $alpha
    redraw
    return
}

proc get_alpha {target} {
    set c [polycolor $target]
    dict create alpha [lindex $c 3]
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Main setup - three overlapping circles
workspace::setup transparency_setup {} \
    -adjusters {
        red_priority green_priority blue_priority
        red_alpha green_alpha blue_alpha
        red_color green_color blue_color
        red_pos green_pos blue_pos
    } \
    -label "Transparency (3 Circles)"

# Ring variant
workspace::variant ring {} -proc transparency_ring_setup \
    -adjusters {} \
    -label "Transparency Ring (6 Circles)"

# Priority adjusters
workspace::adjuster red_priority {
    priority {float 0 10 0.1 0 "Z-Order"}
} -target red_circle -proc set_priority -getter get_priority -label "Red Priority"

workspace::adjuster green_priority {
    priority {float 0 10 0.1 1 "Z-Order"}
} -target green_circle -proc set_priority -getter get_priority -label "Green Priority"

workspace::adjuster blue_priority {
    priority {float 0 10 0.1 2 "Z-Order"}
} -target blue_circle -proc set_priority -getter get_priority -label "Blue Priority"

# Alpha adjusters
workspace::adjuster red_alpha {
    alpha {float 0 1 0.05 0.6 "Alpha"}
} -target red_circle -proc set_alpha -getter get_alpha -label "Red Alpha"

workspace::adjuster green_alpha {
    alpha {float 0 1 0.05 0.6 "Alpha"}
} -target green_circle -proc set_alpha -getter get_alpha -label "Green Alpha"

workspace::adjuster blue_alpha {
    alpha {float 0 1 0.05 0.6 "Alpha"}
} -target blue_circle -proc set_alpha -getter get_alpha -label "Blue Alpha"

# Color adjusters
workspace::adjuster red_color -template color -target red_circle
workspace::adjuster green_color -template color -target green_circle
workspace::adjuster blue_color -template color -target blue_circle

# Position adjusters
workspace::adjuster red_pos -template position -target red_circle
workspace::adjuster green_pos -template position -target green_circle
workspace::adjuster blue_pos -template position -target blue_circle
