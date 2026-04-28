# examples/motionpatch/mp_sequence.tcl
# Motion Direction Sequence
# Demonstrates: chained sequence of motion directions in a stationary
# central aperture, with three center/surround configurations for
# comparing real motion energy vs. induced (motion-contrast) percepts.
#
# Two overlapping motionpatches share one circular mask texture.
# Surround patch (samplermaskmode 2) renders dots OUTSIDE the aperture.
# Target  patch (samplermaskmode 1) renders dots INSIDE  the aperture.
#
# Modes (set the same direction sequence drives the perceived motion at
# the central spot, regardless of whether the energy is in the center
# or the surround):
#   real     - target coherent in seq direction; surround flicker noise
#   induced  - target flicker noise; surround coherent in seq+180 (so
#              the induced motion at the center matches the seq dir)
#   contrast - target coherent in seq dir + surround coherent in seq+180
#              (real motion energy AND induced contrast aligned)
#   baseline - flicker everywhere (no coherent motion); control
#
# The direction sequence is generated from a pattern preset (cw, ccw,
# flip, random), a number of steps, a per-step duration, and a starting
# direction. Optional smoothing ramps each transition over N ms.

load_Impro

# ============================================================
# STIM CODE
# ============================================================

# Render a filled circle into an RGBA texture for the shared mask.
proc mp_seq_make_circle_tex {size} {
    set depth 4
    set half [expr {$size / 2.0}]
    set npoints 64
    set step [expr {2.0 * 3.14159265 / $npoints}]
    dl_local angles [dl_fromto 0 [expr {2.0 * 3.14159265}] $step]
    dl_local x [dl_add [dl_mult [dl_cos $angles] $half] $half]
    dl_local y [dl_add [dl_mult [dl_sin $angles] $half] $half]

    set img  [img_create -width $size -height $size -depth $depth]
    set poly [img_drawPolygon $img $x $y 255 255 255 255]
    dl_local pix [img_imgtolist $poly]
    img_delete $img $poly
    set tex [shaderImageCreate $pix $size $size linear]
    return $tex
}

# Build a (dirs, durs) sequence from a pattern preset.
proc mp_sequence_apply_pattern {pattern n_steps step_ms start_deg} {
    set durs [list]
    set dirs [list]
    for {set i 0} {$i < $n_steps} {incr i} { lappend durs $step_ms }
    switch -- $pattern {
        cw {
            set step [expr {360.0 / $n_steps}]
            for {set i 0} {$i < $n_steps} {incr i} {
                lappend dirs [expr {fmod($start_deg + $i * $step, 360.0)}]
            }
        }
        ccw {
            set step [expr {-360.0 / $n_steps}]
            for {set i 0} {$i < $n_steps} {incr i} {
                lappend dirs [expr {fmod($start_deg + $i * $step + 720.0, 360.0)}]
            }
        }
        flip {
            for {set i 0} {$i < $n_steps} {incr i} {
                if {$i % 2 == 0} {
                    lappend dirs $start_deg
                } else {
                    lappend dirs [expr {fmod($start_deg + 180.0, 360.0)}]
                }
            }
        }
        random {
            for {set i 0} {$i < $n_steps} {incr i} {
                lappend dirs [expr {360.0 * rand()}]
            }
        }
    }
    set ::mp_sequence::seq_dirs $dirs
    set ::mp_sequence::seq_durs $durs
    set ::mp_sequence::seq_idx  0
    set ::mp_sequence::seq_t    0.0
    set ::mp_sequence::last_dir [lindex $dirs 0]
}

# Per-frame preScript: advance the sequence index based on StimTime,
# optionally interpolate across direction transitions, and push the
# resulting direction (and inverted-surround direction in induced /
# contrast modes) to both motionpatches.
proc mp_sequence_update {} {
    set t [expr {$::StimTime / 1000.0}]
    set dt [expr {$t - $::mp_sequence::last_t}]
    if {$dt < 0.0 || $dt > 0.1} { set dt 0.016 }
    if {!$::mp_sequence::playing} { set dt 0.0 }
    set ::mp_sequence::last_t $t
    set ::mp_sequence::seq_t [expr {$::mp_sequence::seq_t + $dt}]

    set dirs $::mp_sequence::seq_dirs
    set durs $::mp_sequence::seq_durs
    set n [llength $dirs]
    if {$n == 0} return

    set idx $::mp_sequence::seq_idx
    set cur_dur [expr {[lindex $durs $idx] / 1000.0}]
    if {$::mp_sequence::seq_t >= $cur_dur} {
        set ::mp_sequence::last_dir [lindex $dirs $idx]
        set idx [expr {($idx + 1) % $n}]
        set ::mp_sequence::seq_idx $idx
        set ::mp_sequence::seq_t   0.0
    }
    set target_dir [lindex $dirs $idx]

    # Optional ramp across direction changes (shortest angular path).
    set trans_s [expr {$::mp_sequence::trans_ms / 1000.0}]
    if {$trans_s > 0.0 && $::mp_sequence::seq_t < $trans_s} {
        set last $::mp_sequence::last_dir
        set f [expr {$::mp_sequence::seq_t / $trans_s}]
        set delta [expr {fmod($target_dir - $last + 540.0, 360.0) - 180.0}]
        set dir [expr {$last + $f * $delta}]
    } else {
        set dir $target_dir
    }
    set ::mp_sequence::cur_dir $dir

    set rad [expr {$dir * 3.14159265 / 180.0}]
    motionpatch_direction dots_target $rad
    if {$::mp_sequence::bg_invert} {
        motionpatch_direction dots_bg [expr {$rad + 3.14159265}]
    } else {
        motionpatch_direction dots_bg $rad
    }
}

# Configure coherence / lifetime / surround-inversion per mode.
proc mp_sequence_apply_mode {mode} {
    set ::mp_sequence::mode $mode
    switch -- $mode {
        real {
            motionpatch_coherence dots_target 1.0
            motionpatch_lifetime  dots_target 30
            motionpatch_coherence dots_bg     0.0
            motionpatch_lifetime  dots_bg     2
            set ::mp_sequence::bg_invert 0
        }
        induced {
            motionpatch_coherence dots_target 0.0
            motionpatch_lifetime  dots_target 2
            motionpatch_coherence dots_bg     1.0
            motionpatch_lifetime  dots_bg     30
            set ::mp_sequence::bg_invert 1
        }
        contrast {
            motionpatch_coherence dots_target 1.0
            motionpatch_lifetime  dots_target 30
            motionpatch_coherence dots_bg     1.0
            motionpatch_lifetime  dots_bg     30
            set ::mp_sequence::bg_invert 1
        }
        baseline {
            motionpatch_coherence dots_target 0.0
            motionpatch_lifetime  dots_target 2
            motionpatch_coherence dots_bg     0.0
            motionpatch_lifetime  dots_bg     2
            set ::mp_sequence::bg_invert 0
        }
    }
}

proc mp_sequence_setup {nDots shapeSize} {
    glistInit 1
    resetObjList
    shaderImageReset

    namespace eval ::mp_sequence {
        variable mode      real
        variable seq_dirs  {0.0 90.0 180.0 270.0}
        variable seq_durs  {500 500 500 500}
        variable seq_idx   0
        variable seq_t     0.0
        variable last_t    0.0
        variable last_dir  0.0
        variable cur_dir   0.0
        variable trans_ms  0
        variable playing   1
        variable speed     0.003
        variable bg_invert 0
    }

    set texSize 256
    set tex   [mp_seq_make_circle_tex $texSize]
    set texID [shaderImageID $tex]

    set color 0.8
    set ptSize 3.0
    set baseSpeed $::mp_sequence::speed

    set mg [metagroup]
    objName $mg patch

    # Surround dots (OUTSIDE aperture).
    set mp_bg [motionpatch $nDots 0.01 30]
    objName $mp_bg dots_bg
    motionpatch_pointsize $mp_bg $ptSize
    motionpatch_color $mp_bg $color $color $color 1.0
    motionpatch_masktype $mp_bg 0
    motionpatch_coherence $mp_bg 0.0
    motionpatch_direction $mp_bg 3.14159265
    motionpatch_speed $mp_bg $baseSpeed
    motionpatch_lifetime $mp_bg 2
    motionpatch_setSampler $mp_bg $texID 0
    motionpatch_samplermaskmode $mp_bg 2
    motionpatch_maskscale $mp_bg $shapeSize
    metagroupAdd $mg $mp_bg

    # Target dots (INSIDE aperture).
    set mp_tg [motionpatch $nDots 0.01 30]
    objName $mp_tg dots_target
    motionpatch_pointsize $mp_tg $ptSize
    motionpatch_color $mp_tg $color $color $color 1.0
    motionpatch_masktype $mp_tg 0
    motionpatch_coherence $mp_tg 1.0
    motionpatch_direction $mp_tg 0.0
    motionpatch_speed $mp_tg $baseSpeed
    motionpatch_lifetime $mp_tg 30
    motionpatch_setSampler $mp_tg $texID 0
    motionpatch_samplermaskmode $mp_tg 1
    motionpatch_maskscale $mp_tg $shapeSize
    metagroupAdd $mg $mp_tg

    addPreScript $mp_bg mp_sequence_update

    scaleObj $mg 10.0 10.0

    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1

    mp_sequence_apply_pattern cw 4 500 0
    mp_sequence_apply_mode $::mp_sequence::mode
    redraw
}

# ---- Adjuster helper procs ----

proc mp_sequence_set_mode {mode} { mp_sequence_apply_mode $mode }
proc mp_sequence_get_mode {} { dict create mode real }

proc mp_sequence_set_pattern {pattern n_steps step_ms start_deg} {
    mp_sequence_apply_pattern $pattern $n_steps $step_ms $start_deg
}
proc mp_sequence_get_pattern {} {
    dict create pattern cw n_steps 4 step_ms 500 start_deg 0
}

proc mp_sequence_set_motion {speed} {
    motionpatch_speed dots_target $speed
    motionpatch_speed dots_bg     $speed
    set ::mp_sequence::speed $speed
}
proc mp_sequence_get_motion {} { dict create speed 0.003 }

proc mp_sequence_set_transition {trans_ms} {
    set ::mp_sequence::trans_ms $trans_ms
}
proc mp_sequence_get_transition {} { dict create trans_ms 0 }

proc mp_sequence_set_shape_size {shape_size} {
    motionpatch_maskscale dots_target $shape_size
    motionpatch_maskscale dots_bg     $shape_size
}
proc mp_sequence_get_shape_size {} { dict create shape_size 0.2 }

proc mp_sequence_set_softness {softness} {
    motionpatch_masksoftness dots_target $softness
    motionpatch_masksoftness dots_bg     $softness
}
proc mp_sequence_get_softness {} { dict create softness 0.0 }

proc mp_sequence_set_freeze {frozen speed} {
    if {$frozen} {
        motionpatch_speed dots_target 0.0
        motionpatch_speed dots_bg     0.0
        motionpatch_refreshPositions dots_target
        motionpatch_refreshPositions dots_bg
        set ::mp_sequence::playing 0
    } else {
        motionpatch_speed dots_target $speed
        motionpatch_speed dots_bg     $speed
        set ::mp_sequence::speed   $speed
        set ::mp_sequence::playing 1
    }
}
proc mp_sequence_get_freeze {} { dict create frozen 0 speed 0.003 }

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup mp_sequence_setup {
    nDots     {int 200 4000 100 1500 "Number of Dots (per patch)"}
    shapeSize {float 0.05 0.5 0.01 0.2 "Aperture Size (fraction of patch)"}
} -adjusters {seq_freeze seq_mode seq_pattern seq_transition seq_motion seq_shape seq_softness seq_transform} \
  -label "Motion Sequence"

workspace::variant seq_real {nDots 1500 shapeSize 0.2} \
  -adjusters {seq_freeze seq_mode seq_pattern seq_transition seq_motion seq_shape seq_softness seq_transform} \
  -label "Real Motion (coherent center)"

workspace::variant seq_induced {nDots 1500 shapeSize 0.2} \
  -adjusters {seq_freeze seq_mode seq_pattern seq_transition seq_motion seq_shape seq_softness seq_transform} \
  -label "Induced Motion (flicker center, inverted surround)"

workspace::variant seq_contrast {nDots 1500 shapeSize 0.2} \
  -adjusters {seq_freeze seq_mode seq_pattern seq_transition seq_motion seq_shape seq_softness seq_transform} \
  -label "Motion Contrast (both, surround inverted)"

workspace::adjuster seq_freeze {
    frozen {choice {0 1} 0 "Frozen"}
    speed  {float 0.0 0.01 0.0005 0.003 "Speed when playing"}
} -target {} -proc mp_sequence_set_freeze -getter mp_sequence_get_freeze \
  -label "Freeze / Play"

workspace::adjuster seq_mode {
    mode {choice {real induced contrast baseline} real "Mode"}
} -target {} -proc mp_sequence_set_mode -getter mp_sequence_get_mode \
  -label "Center / Surround Mode"

workspace::adjuster seq_pattern {
    pattern   {choice {cw ccw flip random} cw "Sequence Pattern"}
    n_steps   {int 2 16 1 4 "Number of Steps"}
    step_ms   {int 50 2000 50 500 "Step Duration (ms)"}
    start_deg {float 0 360 15 0 "Starting Direction (deg)"}
} -target {} -proc mp_sequence_set_pattern -getter mp_sequence_get_pattern \
  -label "Direction Sequence"

workspace::adjuster seq_transition {
    trans_ms {int 0 500 10 0 "Transition Smoothing (ms)"}
} -target {} -proc mp_sequence_set_transition -getter mp_sequence_get_transition \
  -label "Direction Transitions"

workspace::adjuster seq_motion {
    speed {float 0.0 0.01 0.0005 0.003 "Dot Speed"}
} -target {} -proc mp_sequence_set_motion -getter mp_sequence_get_motion \
  -label "Motion Speed"

workspace::adjuster seq_shape {
    shape_size {float 0.05 0.5 0.01 0.2 "Aperture Size"}
} -target {} -proc mp_sequence_set_shape_size -getter mp_sequence_get_shape_size \
  -label "Aperture Size"

workspace::adjuster seq_softness {
    softness {float 0.0 1.0 0.02 0.0 "Edge Softness"}
} -target {} -proc mp_sequence_set_softness -getter mp_sequence_get_softness \
  -label "Aperture Softness"

workspace::adjuster seq_transform -template scale -target patch \
  -label "Scene Size"
