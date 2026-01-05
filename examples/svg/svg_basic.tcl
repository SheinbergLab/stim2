# examples/svg/svg_basic.tcl
# Basic SVG loading and transform demonstration
# Demonstrates: svg loading from file, positioning, scaling, rotation, opacity
#
# Uses metagroup to separate SVG from display transforms:
#   - tiger_svg (svg object): the loaded SVG graphic
#   - tiger (metagroup): scale, rotation, position
#
# The famous Ghostscript Tiger is a classic SVG test case with
# complex gradients, paths, and transforms.

# ============================================================
# STIM CODE - Copy this section to your project
# ============================================================

proc svg_basic_setup {} {
    glistInit 1
    resetObjList
    
    # Load the tiger SVG from assets
    set s [svgAsset Ghostscript_Tiger.svg]
    objName $s tiger_svg
    svgOpacity $s 1.0
    
    # Wrap in metagroup for display transforms
    set mg [metagroup]
    metagroupAdd $mg $s
    objName $mg tiger
    scaleObj $mg 8.0 8.0                ;# scale up for visibility
    
    glistAddObject $mg 0
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ---- Adjuster helper procs ----

proc svg_set_opacity {name opacity} {
    svgOpacity $name $opacity
    redraw
}

proc svg_get_opacity {name} {
    dict create opacity [svgOpacity $name]
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Main setup - no parameters, everything via adjusters
workspace::setup svg_basic_setup {} \
    -adjusters {tiger_scale tiger_rotation tiger_position tiger_opacity} \
    -label "SVG Basic (Tiger)"

# Transform adjusters on metagroup
workspace::adjuster tiger_scale -template scale -target tiger \
    -defaults {scale 8.0} -label "Scale"

workspace::adjuster tiger_rotation -template rotation -target tiger \
    -label "Rotation"

workspace::adjuster tiger_position -template position -target tiger \
    -label "Position"

# Opacity adjuster on SVG object
workspace::adjuster tiger_opacity {
    opacity {float 0.0 1.0 0.01 1.0 "Opacity"}
} -target tiger_svg -proc svg_set_opacity -getter svg_get_opacity \
  -label "Opacity"
