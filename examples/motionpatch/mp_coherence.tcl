# examples/motionpatch/mp_coherence.tcl
# Motion Coherence Demonstration
# Demonstrates: random dot kinematogram, coherence levels, direction control
#
# Classic psychophysics stimulus - a field of moving dots where
# a fraction (coherence) move in the same direction while others
# move randomly. Used extensively in motion perception research.

# ============================================================
# STIM CODE
# ============================================================

proc mp_coherence_setup {nDots masktype} {
    glistInit 1
    resetObjList
    
    # Create motionpatch: n dots, speed, lifetime
    # Use conservative defaults - adjusters will set actual values
    set mp [motionpatch $nDots 0.01 30]
    objName $mp dots
    
    # Set initial appearance
    motionpatch_pointsize $mp 4.0
    motionpatch_color $mp 0.9 0.9 0.9 1.0
    motionpatch_masktype $mp $masktype
    motionpatch_coherence $mp 0.5
    motionpatch_direction $mp 0.0
    motionpatch_speed $mp 0.005
    
    # Wrap in metagroup for positioning/scaling
    set mg [metagroup]
    metagroupAdd $mg $mp
    objName $mg patch
    scaleObj $mg 6.0 6.0
    
    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ---- Adjuster helper procs ----

# Motion parameters
proc mp_set_motion {name coherence direction speed} {
    motionpatch_coherence $name $coherence
    motionpatch_direction $name [expr {$direction * 3.14159265 / 180.0}]
    motionpatch_speed $name $speed
}

proc mp_get_motion {name} {
    # Return defaults - motionpatch doesn't expose getters
    dict create coherence 0.5 direction 0 speed 0.005
}

# Dot appearance - pointsize
proc mp_set_pointsize {name pointsize} {
    motionpatch_pointsize $name $pointsize
}

proc mp_get_pointsize {name} {
    dict create pointsize 4.0
}

# Dot appearance - color
proc mp_set_color {name r g b} {
    motionpatch_color $name $r $g $b 1.0
}

proc mp_get_color {name} {
    dict create r 0.9 g 0.9 b 0.9
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Main setup - nDots and masktype require recreation
workspace::setup mp_coherence_setup {
    nDots    {int 50 2000 50 500 "Number of Dots"}
    masktype {choice {1 2} 1 "Aperture (1=circle, 2=hex)"}
} -adjusters {motion_params dot_size dot_color patch_transform} \
  -label "Motion Coherence"

# Motion parameters adjuster
workspace::adjuster motion_params {
    coherence {float 0.0 1.0 0.05 0.5 "Coherence"}
    direction {float 0 360 15 0 "Direction (deg)"}
    speed     {float 0.0 0.01 0.0005 0.005 "Speed"}
} -target dots -proc mp_set_motion -getter mp_get_motion \
  -label "Motion Parameters"

# Dot size adjuster
workspace::adjuster dot_size {
    pointsize {float 1.0 10.0 0.5 4.0 "Dot Size"}
} -target dots -proc mp_set_pointsize -getter mp_get_pointsize \
  -label "Dot Size"

# Dot color adjuster  
workspace::adjuster dot_color {
    r {float 0.0 1.0 0.05 0.9 "Red"}
    g {float 0.0 1.0 0.05 0.9 "Green"}
    b {float 0.0 1.0 0.05 0.9 "Blue"}
} -target dots -proc mp_set_color -getter mp_get_color \
  -label "Dot Color"

# Transform adjuster for the metagroup
workspace::adjuster patch_transform -template scale -target patch -label "Patch Size"
