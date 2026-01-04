# examples/polygon/lines.tcl
# Line primitives demonstration
# Demonstrates: line types, metagroup transforms, independent adjusters
#
# Line types:
#   - lines: pairs of vertices form separate segments
#   - line_strip: connected path through all vertices  
#   - line_loop: like strip but closes back to start
#
# Uses metagroup pattern:
#   - lines_shape (polygon): color
#   - lines (metagroup): scale, rotation
#
# Note: Segment/point count requires re-running setup (geometry change),
# but all other properties are adjustable without reset!

# ============================================================
# STIM CODE
# ============================================================

# Create random line segments (pairs of points)
proc make_line_segments {n} {
    set s [polygon]
    # n segments = 2n vertices
    set nv [expr {$n * 2}]
    polyverts $s [dl_zrand $nv] [dl_zrand $nv]
    polytype $s lines
    return $s
}

# Create a random walk line strip
proc make_line_strip {n} {
    set s [polygon]
    polyverts $s [dl_zrand $n] [dl_zrand $n]
    polytype $s line_strip
    return $s
}

# Create a random closed loop
proc make_line_loop {n} {
    set s [polygon]
    polyverts $s [dl_zrand $n] [dl_zrand $n]
    polytype $s line_loop
    return $s
}

# Setup for random line segments
proc segments_setup {n} {
    glistInit 1
    resetObjList
    
    set p [make_line_segments $n]
    objName $p lines_shape
    polycolor $p 0.2 0.8 0.4         ;# green default
    
    set mg [metagroup]
    metagroupAdd $mg $p
    objName $mg lines
    scaleObj $mg 5.0 5.0
    
    glistAddObject $mg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# Setup for line strip (connected path)
proc strip_setup {n} {
    glistInit 1
    resetObjList
    
    set p [make_line_strip $n]
    objName $p lines_shape
    polycolor $p 0.8 0.4 0.2         ;# orange default
    
    set mg [metagroup]
    metagroupAdd $mg $p
    objName $mg lines
    scaleObj $mg 5.0 5.0
    
    glistAddObject $mg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# Setup for closed loop
proc loop_setup {n} {
    glistInit 1
    resetObjList
    
    set p [make_line_loop $n]
    objName $p lines_shape
    polycolor $p 0.6 0.2 0.8         ;# purple default
    
    set mg [metagroup]
    metagroupAdd $mg $p
    objName $mg lines
    scaleObj $mg 5.0 5.0
    
    glistAddObject $mg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Line segments - count requires setup (geometry change)
workspace::setup segments_setup {
    n {int 5 500 5 50 "Segment Count"}
} -adjusters {lines_scale lines_rotation lines_color} -label "Line Segments"

# Line strip variant
workspace::variant strip {
    n {int 5 500 5 20 "Point Count"}
} -proc strip_setup -adjusters {lines_scale lines_rotation lines_color} -label "Line Strip"

# Closed loop variant
workspace::variant loop {
    n {int 3 100 1 10 "Point Count"}
} -proc loop_setup -adjusters {lines_scale lines_rotation lines_color} -label "Line Loop"

# Adjusters - scale/rotation on metagroup, color on polygon
workspace::adjuster lines_scale -template scale -target lines
workspace::adjuster lines_rotation -template rotation -target lines
workspace::adjuster lines_color -template color -target lines_shape
