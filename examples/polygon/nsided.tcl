# examples/polygon/nsided.tcl
# Regular polygon demonstration
# Demonstrates: procedural geometry, metagroup transforms, independent adjusters
#
# This single file provides multiple shape configurations:
#   - N-Sided Polygon (adjustable sides)
#   - Triangle, Square, Pentagon, Hexagon, Octagon (presets)
#   - Circle (smooth, using polycirc)
#
# Uses metagroup pattern:
#   - poly_shape (polygon): color
#   - poly (metagroup): scale, rotation
#
# Note: Number of sides requires re-running setup (geometry change),
# but all other properties are adjustable without reset!

# ============================================================
# STIM CODE
# ============================================================

# Create an n-sided regular polygon using triangle fan
proc make_nsided {n} {
    set s [polygon]
    set step [expr {2.0 * 3.14159265 / $n}]
    dl_local x0 [dl_cos [dl_fromto 0 [expr {2 * 3.14159265}] $step]]
    dl_local y0 [dl_sin [dl_fromto 0 [expr {2 * 3.14159265}] $step]]
    dl_local x [dl_combine 0.0 $x0 [dl_first $x0]]
    dl_local y [dl_combine 0.0 $y0 [dl_first $y0]]
    polyverts $s $x $y
    polytype $s triangle_fan
    return $s
}

# Create a smooth circle using polycirc
proc make_circle {} {
    set s [polygon]
    polycirc $s 1
    return $s
}

# Main setup for n-sided polygon
proc nsided_setup {n} {
    glistInit 1
    resetObjList
    
    # Create polygon (unit size)
    set p [make_nsided $n]
    objName $p poly_shape
    polycolor $p 1.0 0.5 0.0         ;# default orange
    
    # Wrap in metagroup for display transforms
    set mg [metagroup]
    metagroupAdd $mg $p
    objName $mg poly
    scaleObj $mg 3.0 3.0             ;# default display scale
    
    glistAddObject $mg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# Setup for circle (uses polycirc for smooth edges)
proc circle_setup {} {
    glistInit 1
    resetObjList
    
    set p [make_circle]
    objName $p poly_shape
    polycolor $p 0.2 0.6 1.0         ;# default blue
    
    set mg [metagroup]
    metagroupAdd $mg $p
    objName $mg poly
    scaleObj $mg 3.0 3.0
    
    glistAddObject $mg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# Wrapper procs for fixed-n variants
proc triangle_setup {} { nsided_setup 3 }
proc square_setup {} { nsided_setup 4 }
proc pentagon_setup {} { nsided_setup 5 }
proc hexagon_setup {} { nsided_setup 6 }
proc octagon_setup {} { nsided_setup 8 }

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Primary setup - n requires setup (geometry change)
workspace::setup nsided_setup {
    n {int 3 24 1 6 "Sides"}
} -adjusters {poly_scale poly_rotation poly_color} -label "N-Sided Polygon"

# Preset variants - no params, just different starting geometry
workspace::variant circle {} -proc circle_setup \
    -adjusters {poly_scale poly_rotation poly_color} -label "Circle"

workspace::variant triangle {} -proc triangle_setup \
    -adjusters {poly_scale poly_rotation poly_color} -label "Triangle"

workspace::variant square {} -proc square_setup \
    -adjusters {poly_scale poly_rotation poly_color} -label "Square"

workspace::variant pentagon {} -proc pentagon_setup \
    -adjusters {poly_scale poly_rotation poly_color} -label "Pentagon"

workspace::variant hexagon {} -proc hexagon_setup \
    -adjusters {poly_scale poly_rotation poly_color} -label "Hexagon"

workspace::variant octagon {} -proc octagon_setup \
    -adjusters {poly_scale poly_rotation poly_color} -label "Octagon"

# Adjusters - scale/rotation on metagroup, color on polygon
workspace::adjuster poly_scale -template scale -target poly
workspace::adjuster poly_rotation -template rotation -target poly
workspace::adjuster poly_color -template color -target poly_shape
