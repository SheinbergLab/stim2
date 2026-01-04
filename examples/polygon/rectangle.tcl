# examples/polygon/rectangle.tcl
# Basic rectangle demonstration
# Demonstrates: polygon creation, metagroup transforms, independent adjusters
#
# Uses metagroup to separate shape definition from display transforms:
#   - rect_shape (polygon): width, height, color
#   - rect (metagroup): scale, rotation, position
#
# All properties are adjustable without re-running setup!

# ============================================================
# STIM CODE - Copy this section to your project
# ============================================================

proc rectangle_setup {} {
    glistInit 1
    resetObjList

    # Create polygon (unit square)
    set p [polygon]
    objName $p rect_shape
    scaleObj $p 1.5 1.0               ;# default aspect (width > height)
    polycolor $p 0.9 0.6 0.2          ;# default orange
    
    # Wrap in metagroup for display transforms
    set mg [metagroup]
    metagroupAdd $mg $p
    objName $mg rect
    scaleObj $mg 3.0 3.0              ;# default display scale
    
    glistAddObject $mg 0
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# Square variant - just sets 1:1 aspect
proc square_setup {} {
    glistInit 1
    resetObjList

    set p [polygon]
    objName $p rect_shape
    scaleObj $p 1.0 1.0               ;# square aspect
    polycolor $p 0.2 0.7 0.9          ;# default cyan
    
    set mg [metagroup]
    metagroupAdd $mg $p
    objName $mg rect
    scaleObj $mg 3.0 3.0
    
    glistAddObject $mg 0
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Rectangle - no setup params, everything via adjusters
workspace::setup rectangle_setup {} \
    -adjusters {rect_size rect_scale rect_rotation rect_color} \
    -label "Rectangle"

# Square variant - no size adjuster (keeps 1:1 aspect)
workspace::variant square {} -proc square_setup \
    -adjusters {rect_scale rect_rotation rect_color} \
    -label "Square"

# Adjusters on polygon (rect_shape)
workspace::adjuster rect_size -template size2d -target rect_shape
workspace::adjuster rect_color -template color -target rect_shape

# Adjusters on metagroup (rect)
workspace::adjuster rect_scale -template scale -target rect
workspace::adjuster rect_rotation -template rotation -target rect
