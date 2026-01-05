# examples/animation/anim_blink.tcl
# Blinking/flashing animation demonstration
# Demonstrates: animateBlink for visibility toggling
#
# Common uses: fixation points, cues, feedback, attention getters

# ============================================================
# SETUP PROCS
# ============================================================

proc setup_blink { {rate 2.0} {duty 0.5} } {
    glistInit 1
    resetObjList
    
    set p [polygon]
    objName $p blinky
    polycolor $p 1.0 0.3 0.3
    scaleObj $p 1.5 1.5
    
    animateBlink blinky -rate $rate -duty $duty
    
    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Fixation cross that blinks
proc setup_fixation { {rate 1.0} {duty 0.8} } {
    glistInit 1
    resetObjList
    
    # Horizontal bar: 1.0 wide x 0.2 tall
    set h [polygon]
    polyverts $h [dl_flist -0.5 0.5 -0.5 0.5] [dl_flist -0.1 -0.1 0.1 0.1]
    polytype $h triangle_strip
    polycolor $h 1 1 1
    objName $h fix_h
    
    # Vertical bar: 0.2 wide x 1.0 tall
    set v [polygon]
    polyverts $v [dl_flist -0.1 -0.1 0.1 0.1] [dl_flist -0.5 0.5 -0.5 0.5] 
    polytype $v triangle_strip
    polycolor $v 1 1 1
    objName $v fix_v
    
    # Group them
    set mg [metagroup]
    metagroupAdd $mg $h
    metagroupAdd $mg $v
    objName $mg fixation
    
    animateBlink fixation -rate $rate -duty $duty
    
    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Multiple objects blinking at different rates
proc setup_multi { } {
    glistInit 1
    resetObjList
    
    # Three squares at different positions and rates
    foreach {name x color rate} {
        slow  -3.0 {0.3 0.7 1.0} 0.5
        med    0.0 {0.3 1.0 0.5} 1.0
        fast   3.0 {1.0 0.5 0.3} 3.0
    } {
        set p [polygon]
        objName $p $name
        lassign $color r g b
        polycolor $p $r $g $b
        translateObj $p $x 0 0
        animateBlink $name -rate $rate
        glistAddObject $p 0
    }
    
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Frame-precise blinking (for psychophysics)
proc setup_frame_precise { {frames_per_cycle 60} {duty 0.5} } {
    glistInit 1
    resetObjList
    
    set p [polygon]
    objName $p target
    polycolor $p 0.2 0.8 0.2
    scaleObj $p 2.0 2.0
    
    # -perframe means rate is frames per cycle, not Hz
    animateBlink target -rate $frames_per_cycle -duty $duty -perframe
    
    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# ============================================================
# ADJUSTERS
# ============================================================

proc set_blink { rate duty } {
    animateBlink blinky -rate $rate -duty $duty
}

proc get_blink {{target {}}} {
    animateBlink blinky
}

proc set_fixation_blink { rate duty } {
    animateBlink fixation -rate $rate -duty $duty
}

proc get_fixation_blink {{target {}}} {
    animateBlink fixation
}

proc set_frame_blink { rate duty } {
    animateBlink target -rate $rate -duty $duty
}

proc get_frame_blink {{target {}}} {
    animateBlink target
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

# Basic blink
workspace::setup setup_blink {} -adjusters {blink_adj} -label "Basic Blink"

workspace::adjuster blink_adj {
    rate {float 0.5 10.0 0.5 2.0 "Rate" Hz}
    duty {float 0.1 0.9 0.1 0.5 "Duty Cycle"}
} -target {} -proc set_blink -getter get_blink -label "Blink"

# Fixation cross
workspace::variant fixation {} \
    -proc setup_fixation -adjusters {fixation_adj} -label "Fixation Cross"

workspace::adjuster fixation_adj {
    rate {float 0.5 5.0 0.25 1.0 "Rate" Hz}
    duty {float 0.1 0.9 0.1 0.8 "Duty Cycle"}
} -target {} -proc set_fixation_blink -getter get_fixation_blink -label "Blink"

# Multiple rates
workspace::variant multi {} \
    -proc setup_multi -adjusters {} -label "Multiple Rates"

# Frame-precise
workspace::variant frame_precise {} \
    -proc setup_frame_precise -adjusters {frame_adj} -label "Frame Precise"

workspace::adjuster frame_adj {
    rate {float 10 120 10 60 "Frames/Cycle"}
    duty {float 0.1 0.9 0.1 0.5 "Duty Cycle"}
} -target {} -proc set_frame_blink -getter get_frame_blink -label "Blink"
