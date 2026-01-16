# examples/priority/z_order.tcl
# Z-Order Priority demonstration
# Demonstrates: priority-based draw ordering, dynamic z-order changes
#
# Shows three overlapping colored squares that can be reordered
# using the priority system. Higher priority = drawn later (in front).
#
# Uses metagroup pattern:
#   - red_shape, green_shape, blue_shape (polygons): individual colors
#   - shapes (metagroup): contains all three, sorts by priority
#
# All properties including z-order are adjustable without re-running setup!

# ============================================================
# STIM CODE
# ============================================================

proc priority_setup {} {
    glistInit 1
    resetObjList
    
    # Create metagroup container
    set mg [metagroup]
    objName $mg shapes
    
    # Red square - offset upper-left
    set r [polygon]
    objName $r red_shape
    polycolor $r 0.9 0.2 0.2
    scaleObj $r 3.0 3.0
    translateObj $r -1.0 1.0 0
    priorityObj $r 0.0
    metagroupAdd $mg $r
    
    # Green square - center
    set g [polygon]
    objName $g green_shape
    polycolor $g 0.2 0.8 0.3
    scaleObj $g 3.0 3.0
    translateObj $g 0.0 0.0 0
    priorityObj $g 1.0
    metagroupAdd $mg $g
    
    # Blue square - offset lower-right
    set b [polygon]
    objName $b blue_shape
    polycolor $b 0.2 0.4 0.9
    scaleObj $b 3.0 3.0
    translateObj $b 1.0 -1.0 0
    priorityObj $b 2.0
    metagroupAdd $mg $b
    
    glistAddObject $mg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# Variant with group-level (non-metagroup) priority
proc priority_group_setup {} {
    glistInit 1
    resetObjList
    
    # Red square - offset upper-left
    set r [polygon]
    objName $r red_shape
    polycolor $r 0.9 0.2 0.2
    scaleObj $r 3.0 3.0
    translateObj $r -1.0 1.0 0
    priorityObj $r 0.0
    glistAddObject $r 0
    
    # Green square - center
    set g [polygon]
    objName $g green_shape
    polycolor $g 0.2 0.8 0.3
    scaleObj $g 3.0 3.0
    translateObj $g 0.0 0.0 0
    priorityObj $g 1.0
    glistAddObject $g 0
    
    # Blue square - offset lower-right
    set b [polygon]
    objName $b blue_shape
    polycolor $b 0.2 0.4 0.9
    scaleObj $b 3.0 3.0
    translateObj $b 1.0 -1.0 0
    priorityObj $b 2.0
    glistAddObject $b 0
    
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# CUSTOM ADJUSTER PROCS
# ============================================================

# Priority setter - args are target then param values in order
proc set_priority {target priority} {
    priorityObj $target $priority
    redraw
    return
}

# Priority getter - returns dict with keys matching param names
proc get_priority {target} {
    dict create priority [priorityObj $target]
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Main setup - metagroup-based priority sorting
workspace::setup priority_setup {} \
    -adjusters {red_priority green_priority blue_priority red_pos green_pos blue_pos} \
    -label "Z-Order Priority (Metagroup)"

# Variant - group-level priority sorting  
workspace::variant group {} -proc priority_group_setup \
    -adjusters {red_priority green_priority blue_priority red_pos green_pos blue_pos} \
    -label "Z-Order Priority (Group)"

# Priority adjusters - custom since there's no template
workspace::adjuster red_priority {
    priority {float 0 10 0.1 0 "Priority"}
} -target red_shape -proc set_priority -getter get_priority -label "Red Z-Order"

workspace::adjuster green_priority {
    priority {float 0 10 0.1 1 "Priority"}
} -target green_shape -proc set_priority -getter get_priority -label "Green Z-Order"

workspace::adjuster blue_priority {
    priority {float 0 10 0.1 2 "Priority"}
} -target blue_shape -proc set_priority -getter get_priority -label "Blue Z-Order"

# Position adjusters
workspace::adjuster red_pos -template position -target red_shape
workspace::adjuster green_pos -template position -target green_shape
workspace::adjuster blue_pos -template position -target blue_shape
