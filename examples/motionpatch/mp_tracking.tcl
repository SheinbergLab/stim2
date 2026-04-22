# examples/motionpatch/mp_tracking.tcl
# Motion-Defined Moving Target
# Demonstrates: maskOffset uniform driving a real-time shape translation
#
# Two overlapping motionpatches share one circular mask texture.
# Background patch (samplermaskmode 2) renders dots OUTSIDE the shape.
# Target patch   (samplermaskmode 1) renders dots INSIDE  the shape.
# Both patches use identical dot color/size/density, so when all dot
# speeds are zero the scene is a uniform random dot field — the shape
# is invisible. When motion resumes, inside and outside dots move in
# different directions and the shape emerges. The mask offset is
# updated every frame via a preScript so the target glides along a
# Lissajous path.

load_Impro

# ============================================================
# STIM CODE
# ============================================================

# Render a filled circle into an RGBA texture for use as the mask.
proc mp_tracking_make_circle_tex {size} {
    set depth 4
    set half [expr {$size / 2.0}]
    set npoints 64
    set step [expr {2.0 * 3.14159265 / $npoints}]
    dl_local angles [dl_fromto 0 [expr {2.0 * 3.14159265}] $step]
    # circle spans the full texture so maskScale controls final size
    dl_local x [dl_add [dl_mult [dl_cos $angles] $half] $half]
    dl_local y [dl_add [dl_mult [dl_sin $angles] $half] $half]

    set img  [img_create -width $size -height $size -depth $depth]
    set poly [img_drawPolygon $img $x $y 255 255 255 255]
    dl_local pix [img_imgtolist $poly]
    img_delete $img $poly
    set tex [shaderImageCreate $pix $size $size linear]
    return $tex
}

# Per-frame preScript: update the shared mask offset along a Lissajous path.
# Uses ::StimTime (ms) so freezing motion is independent of time-driving.
# The offset is in patch-local centered coords; patch spans [-0.5, 0.5]
# before the metagroup scale, so |offset| < 0.5 keeps the shape on-screen.
proc mp_tracking_update_offset {} {
    set t [expr {$::StimTime / 1000.0}]
    # Only advance the path when tracking is "playing"
    if {$::mp_tracking::playing} {
        set ::mp_tracking::path_t [expr {$::mp_tracking::path_t + \
            ($t - $::mp_tracking::last_t)}]
    }
    set ::mp_tracking::last_t $t

    set pt $::mp_tracking::path_t
    set ax $::mp_tracking::amp_x
    set ay $::mp_tracking::amp_y
    set fx $::mp_tracking::freq_x
    set fy $::mp_tracking::freq_y
    set ox [expr {$ax * sin(2.0 * 3.14159265 * $fx * $pt)}]
    set oy [expr {$ay * sin(2.0 * 3.14159265 * $fy * $pt + 1.5708)}]

    motionpatch_maskoffset dots_bg     $ox $oy
    motionpatch_maskoffset dots_target $ox $oy
}

proc mp_tracking_setup {nDots shapeSize} {
    glistInit 1
    resetObjList
    shaderImageReset

    namespace eval ::mp_tracking {
        variable playing   1
        variable path_t    0.0
        variable last_t    0.0
        variable amp_x     0.35
        variable amp_y     0.25
        variable freq_x    0.17
        variable freq_y    0.23
    }

    set texSize 256
    set tex   [mp_tracking_make_circle_tex $texSize]
    set texID [shaderImageID $tex]

    set color 0.8
    set ptSize 3.0
    set speed 0.003

    set mg [metagroup]
    objName $mg patch

    # Background dots (OUTSIDE mask shape): moving leftward
    set mp_bg [motionpatch $nDots 0.01 30]
    objName $mp_bg dots_bg
    motionpatch_pointsize $mp_bg $ptSize
    motionpatch_color $mp_bg $color $color $color 1.0
    motionpatch_masktype $mp_bg 0
    motionpatch_coherence $mp_bg 1.0
    motionpatch_direction $mp_bg 3.14159265
    motionpatch_speed $mp_bg $speed
    motionpatch_setSampler $mp_bg $texID 0
    motionpatch_samplermaskmode $mp_bg 2
    motionpatch_maskscale $mp_bg $shapeSize
    metagroupAdd $mg $mp_bg

    # Target dots (INSIDE mask shape): moving rightward
    set mp_tg [motionpatch $nDots 0.01 30]
    objName $mp_tg dots_target
    motionpatch_pointsize $mp_tg $ptSize
    motionpatch_color $mp_tg $color $color $color 1.0
    motionpatch_masktype $mp_tg 0
    motionpatch_coherence $mp_tg 1.0
    motionpatch_direction $mp_tg 0.0
    motionpatch_speed $mp_tg $speed
    motionpatch_setSampler $mp_tg $texID 0
    motionpatch_samplermaskmode $mp_tg 1
    motionpatch_maskscale $mp_tg $shapeSize
    metagroupAdd $mg $mp_tg

    # Drive the shared mask offset every frame from a preScript.
    addPreScript $mp_bg mp_tracking_update_offset

    scaleObj $mg 10.0 10.0

    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ---- Adjuster helper procs ----

proc mp_tracking_set_motion {bg_coh tg_coh speed} {
    motionpatch_coherence dots_bg     $bg_coh
    motionpatch_coherence dots_target $tg_coh
    motionpatch_speed     dots_bg     $speed
    motionpatch_speed     dots_target $speed
}

proc mp_tracking_get_motion {} {
    dict create bg_coh 1.0 tg_coh 1.0 speed 0.003
}

proc mp_tracking_set_directions {bg_dir tg_dir} {
    motionpatch_direction dots_bg     [expr {$bg_dir * 3.14159265 / 180.0}]
    motionpatch_direction dots_target [expr {$tg_dir * 3.14159265 / 180.0}]
}

proc mp_tracking_get_directions {} {
    dict create bg_dir 180 tg_dir 0
}

proc mp_tracking_set_shape_size {shape_size} {
    motionpatch_maskscale dots_bg     $shape_size
    motionpatch_maskscale dots_target $shape_size
}

proc mp_tracking_get_shape_size {} {
    dict create shape_size 0.25
}

proc mp_tracking_set_path {amp_x amp_y freq_x freq_y} {
    set ::mp_tracking::amp_x  $amp_x
    set ::mp_tracking::amp_y  $amp_y
    set ::mp_tracking::freq_x $freq_x
    set ::mp_tracking::freq_y $freq_y
}

proc mp_tracking_get_path {} {
    dict create amp_x 0.35 amp_y 0.25 freq_x 0.17 freq_y 0.23
}

# Freeze toggles BOTH the dot motion (speed=0) and the mask translation,
# so a frozen trial is statistically indistinguishable from a uniform
# random dot field.
proc mp_tracking_set_freeze {frozen speed} {
    if {$frozen} {
        motionpatch_speed dots_bg     0.0
        motionpatch_speed dots_target 0.0
        set ::mp_tracking::playing 0
    } else {
        motionpatch_speed dots_bg     $speed
        motionpatch_speed dots_target $speed
        set ::mp_tracking::playing 1
    }
}

proc mp_tracking_get_freeze {} {
    dict create frozen 0 speed 0.003
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup mp_tracking_setup {
    nDots     {int 200 4000 100 1500 "Number of Dots (per patch)"}
    shapeSize {float 0.05 0.5 0.01 0.2 "Shape Size (fraction of patch)"}
} -adjusters {track_freeze track_motion track_directions track_shape track_path track_transform} \
  -label "Motion Tracking"

workspace::adjuster track_freeze {
    frozen {choice {0 1} 0 "Frozen (1=invisible)"}
    speed  {float 0.0 0.01 0.0005 0.003 "Speed when playing"}
} -target {} -proc mp_tracking_set_freeze -getter mp_tracking_get_freeze \
  -label "Freeze / Play"

workspace::adjuster track_motion {
    bg_coh {float 0.0 1.0 0.05 1.0 "Background Coherence"}
    tg_coh {float 0.0 1.0 0.05 1.0 "Target Coherence"}
    speed  {float 0.0 0.01 0.0005 0.003 "Dot Speed"}
} -target {} -proc mp_tracking_set_motion -getter mp_tracking_get_motion \
  -label "Dot Motion"

workspace::adjuster track_directions {
    bg_dir {float 0 360 15 180 "Background Direction (deg)"}
    tg_dir {float 0 360 15 0   "Target Direction (deg)"}
} -target {} -proc mp_tracking_set_directions -getter mp_tracking_get_directions \
  -label "Inside / Outside Directions"

workspace::adjuster track_shape {
    shape_size {float 0.05 0.5 0.01 0.2 "Shape Size"}
} -target {} -proc mp_tracking_set_shape_size -getter mp_tracking_get_shape_size \
  -label "Shape Size"

workspace::adjuster track_path {
    amp_x  {float 0.0 0.5 0.05 0.35 "Amplitude X"}
    amp_y  {float 0.0 0.5 0.05 0.25 "Amplitude Y"}
    freq_x {float 0.0 1.0 0.01 0.17 "Frequency X (Hz)"}
    freq_y {float 0.0 1.0 0.01 0.23 "Frequency Y (Hz)"}
} -target {} -proc mp_tracking_set_path -getter mp_tracking_get_path \
  -label "Target Path (Lissajous)"

workspace::adjuster track_transform -template scale -target patch \
  -label "Scene Size"
