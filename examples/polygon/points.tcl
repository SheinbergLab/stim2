# examples/polygon/points.tcl
# Random point cloud demonstration
# Demonstrates: point primitives, metagroup transforms, independent adjusters
#
# Uses metagroup pattern:
#   - pts_shape (polygon): pointsize, color
#   - pts (metagroup): scale, rotation
#
# Note: Point count requires re-running setup (geometry change),
# but all other properties are adjustable without reset!

# ============================================================
# STIM CODE
# ============================================================

proc make_points {n} {
    set s [polygon]
    polyverts $s [dl_zrand $n] [dl_zrand $n] [dl_zrand $n]
    polytype $s points
    return $s
}

proc points_setup {n} {
    glistInit 1
    resetObjList

    # Create point cloud (unit scale from zrand)
    set p [make_points $n]
    objName $p pts_shape
    polypointsize $p 5.0             ;# default point size
    polycolor $p 0.5 0.5 1.0         ;# default light blue

    # Wrap in metagroup for display transforms
    set mg [metagroup]
    metagroupAdd $mg $p
    objName $mg pts
    scaleObj $mg 5.0 5.0             ;# default display scale

    glistAddObject $mg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Point count requires setup (geometry change)
workspace::setup points_setup {
    n {int 10 10000 10 500 "Point Count"}
} -adjusters {pts_scale pts_rotation pts_pointsize pts_color} -label "Random Points"

# Adjusters - scale/rotation on metagroup, pointsize/color on polygon
workspace::adjuster pts_scale -template scale -target pts
workspace::adjuster pts_rotation -template rotation -target pts
workspace::adjuster pts_pointsize -template pointsize -target pts_shape
workspace::adjuster pts_color -template color -target pts_shape
