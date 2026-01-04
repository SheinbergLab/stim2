# examples/motionpatch/mp_noiseflow.tcl
# Noise Flow Demonstration
# Demonstrates: simplex noise direction field, organic flow patterns
#
# Uses 3D simplex noise to set each dot's direction based on its
# position. The result is a smooth, organic flow field that can
# evolve over time. Common in generative art and useful for
# studying motion integration across space.

# ============================================================
# STIM CODE
# ============================================================

proc mp_noiseflow_setup {nDots period rate seed} {
    glistInit 1
    resetObjList
    
    set mp [motionpatch $nDots 0.01 30]
    objName $mp dots
    
    motionpatch_pointsize $mp 4.0
    motionpatch_color $mp 0.8 0.8 0.8 1.0
    motionpatch_masktype $mp 1
    motionpatch_coherence $mp 1.0
    motionpatch_speed $mp 0.003
    
    # Noise direction setup
    motionpatch_setNoiseZ $mp 0.0
    motionpatch_noiseUpdateZ $mp 1
    motionpatch_useNoiseDirection $mp 1 $period $rate
    motionpatch_setSeed $mp 0 $seed
    motionpatch_setSeed $mp 1 [expr {$seed + 12345}]
    
    # Wrap in metagroup for transforms
    set mg [metagroup]
    metagroupAdd $mg $mp
    objName $mg patch
    scaleObj $mg 10.0 10.0
    
    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ---- Adjuster helper procs ----

proc mp_noiseflow_set_appearance {name pointsize r g b} {
    motionpatch_pointsize $name $pointsize
    motionpatch_color $name $r $g $b 1.0
}

proc mp_noiseflow_get_appearance {name} {
    dict create pointsize 4.0 r 0.8 g 0.8 b 0.8
}

proc mp_noiseflow_set_speed {name speed} {
    motionpatch_speed $name $speed
}

proc mp_noiseflow_get_speed {name} {
    dict create speed 0.003
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup mp_noiseflow_setup {
    nDots  {int 200 2000 100 1000 "Number of Dots"}
    period {float 0.5 5.0 0.5 2.0 "Noise Period"}
    rate   {float 0.0 2.0 0.1 0.5 "Evolution Rate"}
    seed   {int 0 99999 1 77374 "Random Seed"}
} -adjusters {noiseflow_speed noiseflow_appearance noiseflow_transform} \
  -label "Noise Flow"

workspace::adjuster noiseflow_speed {
    speed {float 0.0 0.01 0.0005 0.003 "Speed"}
} -target dots -proc mp_noiseflow_set_speed -getter mp_noiseflow_get_speed \
  -label "Speed"

workspace::adjuster noiseflow_appearance {
    pointsize {float 1.0 10.0 0.5 4.0 "Dot Size"}
    r {float 0.0 1.0 0.05 0.8 "Red"}
    g {float 0.0 1.0 0.05 0.8 "Green"}
    b {float 0.0 1.0 0.05 0.8 "Blue"}
} -target dots -proc mp_noiseflow_set_appearance -getter mp_noiseflow_get_appearance \
  -label "Appearance"

workspace::adjuster noiseflow_transform -template scale -target patch \
  -label "Patch Size"
