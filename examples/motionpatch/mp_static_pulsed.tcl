# examples/motionpatch/mp_static_pulsed.tcl
# Minimal pulsed-motion demo on a STATIC patch.
#
# This is the conceptual core of mp_pulsed.tcl with the entire trajectory
# layer removed. The aperture sits at screen center; only the inside-patch
# motion statistics are gated by a sum-of-Gaussians envelope.
#
# Per-frame, three statistics interpolate between "target" and "surround":
#   coherence : peak base_coh at pulse centers, 0 between pulses
#   speed     : target_speed at peaks, surround_speed between
#   lifetime  : target_lifetime at peaks, surround_lifetime between
#
# At pulse troughs the inside dots are statistically identical to the
# surround flicker, so the patch is invisible (motion-defined only).
# At peaks a coherent target direction is briefly imposed.
#
# Extending to a moving patch is one extra line per frame:
#   motionpatch_maskoffset dots_target $x $y
# (and likewise the direction). That is precisely what mp_pulsed.tcl
# does -- the rest of its 1400 lines is trajectory builders, bounce
# handling, recording / design-dg export, and adjusters.
#
# Controls:
#   Down Arrow - start the pulse train
#   Up Arrow   - reset

load_Impro

proc mp_static_make_circle_tex {size} {
    set depth 4
    set half [expr {$size / 2.0}]
    set npoints 64
    set step [expr {2.0 * 3.14159265 / $npoints}]
    dl_local angles [dl_fromto 0 [expr {2.0 * 3.14159265}] $step]
    dl_local x [dl_add [dl_mult [dl_cos $angles] $half] $half]
    dl_local y [dl_add [dl_mult [dl_sin $angles] $half] $half]
    set img  [img_create -width $size -height $size -depth $depth]
    set poly [img_drawPolygonFast $img $x $y 255 255 255 255]
    dl_local pix [img_imgtolist $poly]
    img_delete $img $poly
    return [shaderImageCreate $pix $size $size linear]
}

# Sum-of-N Gaussians, N pulse centers evenly spaced over [0, T].
proc mp_static_envelope {tplay} {
    set sigma [expr {$::mp_static::sigma_ms / 1000.0}]
    if {$sigma <= 0.0} { return $::mp_static::base_coh }
    set sum 0.0
    foreach ti $::mp_static::tile_times {
        set z [expr {($tplay - $ti) / $sigma}]
        set sum [expr {$sum + exp(-0.5 * $z * $z)}]
    }
    set v [expr {$::mp_static::base_coh * $sum}]
    if {$v > $::mp_static::base_coh} { set v $::mp_static::base_coh }
    if {$v < 0.0} { set v 0.0 }
    return $v
}

proc mp_static_recompute_tiles {} {
    set ::mp_static::tile_times [list]
    set N $::mp_static::n_snapshots
    set T $::mp_static::duration
    if {$N > 0} {
        for {set k 0} {$k < $N} {incr k} {
            lappend ::mp_static::tile_times [expr {$T * ($k + 0.5) / $N}]
        }
    }
}

# dva/sec -> patch-local-units/sec
proc mp_static_speed_pu {v} {
    set ps $::mp_static::patch_size
    if {$ps <= 0} { return 0.0 }
    return [expr {$v / $ps}]
}

proc mp_static_update {} {
    set t [expr {$::StimTime / 1000.0}]
    set dt [expr {$t - $::mp_static::last_t}]
    if {$dt < 0.0 || $dt > 0.1} { set dt 0.016 }
    set ::mp_static::last_t $t

    if {!$::mp_static::dropping} { return }

    set ::mp_static::play_t [expr {$::mp_static::play_t + $dt}]
    set tplay $::mp_static::play_t
    if {$tplay > $::mp_static::duration} {
        set ::mp_static::dropping 0
        # Collapse inside dots to the surround state so the trial ends
        # cleanly rather than freezing at peak.
        motionpatch_coherence dots_target 0.0
        motionpatch_speed     dots_target [mp_static_speed_pu $::mp_static::surround_speed]
        motionpatch_lifetime  dots_target $::mp_static::bg_lifetime
        return
    }

    set base $::mp_static::base_coh
    set coh  [mp_static_envelope $tplay]
    set frac [expr {($base > 0.0) ? ($coh / $base) : 0.0}]

    set tgt_pu  [mp_static_speed_pu $::mp_static::target_speed]
    set surr_pu [mp_static_speed_pu $::mp_static::surround_speed]
    set sp_eff   [expr {$frac * $tgt_pu + (1.0 - $frac) * $surr_pu}]
    set life_eff [expr {$frac * $::mp_static::target_lifetime + \
                        (1.0 - $frac) * $::mp_static::bg_lifetime}]

    motionpatch_direction dots_target $::mp_static::target_dir_rad
    motionpatch_speed     dots_target $sp_eff
    motionpatch_lifetime  dots_target $life_eff
    motionpatch_coherence dots_target $coh
}

proc mp_static_setup {patch_size_dva dot_density} {
    glistInit 1
    resetObjList
    shaderImageReset

    namespace eval ::mp_static {
        variable dropping         0
        variable last_t           0.0
        variable play_t           0.0

        variable duration         2.0
        variable patch_size      13.0
        variable shape_size       0.10

        variable n_snapshots      5
        variable sigma_ms        50.0
        variable base_coh         1.0

        variable target_speed     8.0   ;# dva/sec at pulse peak
        variable target_dir_deg   0.0
        variable target_dir_rad   0.0
        variable target_lifetime  0.5

        variable surround_speed   3.0   ;# dva/sec
        variable bg_lifetime      0.08

        variable ball_lum         0.85
        variable surround_lum     0.85

        variable tile_times       {}
    }
    set ::mp_static::patch_size $patch_size_dva
    mp_static_recompute_tiles

    set nDots [expr {int($dot_density * $patch_size_dva * $patch_size_dva)}]
    if {$nDots < 100} { set nDots 100 }

    set tex   [mp_static_make_circle_tex 256]
    set texID [shaderImageID $tex]

    set surr_pu [mp_static_speed_pu $::mp_static::surround_speed]
    set ptSize 3.0

    set mg [metagroup]
    objName $mg patch

    set bgL $::mp_static::surround_lum
    set tgL $::mp_static::ball_lum

    # Surround: full-field flicker. masktype=0, samplermaskmode=2 means
    # the texture sampler INVERTS the circle -- dots draw outside it.
    set mp_bg [motionpatch $nDots $surr_pu 0.5]
    objName $mp_bg dots_bg
    motionpatch_pointsize $mp_bg $ptSize
    motionpatch_color     $mp_bg $bgL $bgL $bgL 1.0
    motionpatch_masktype  $mp_bg 0
    motionpatch_coherence $mp_bg 0.0
    motionpatch_lifetime  $mp_bg $::mp_static::bg_lifetime
    motionpatch_direction $mp_bg 0.0
    motionpatch_speed     $mp_bg $surr_pu
    motionpatch_setSampler $mp_bg $texID 0
    motionpatch_samplermaskmode $mp_bg 2
    motionpatch_maskscale $mp_bg $::mp_static::shape_size
    metagroupAdd $mg $mp_bg

    # Target: dots draw INSIDE the circle (samplermaskmode=1). Begins
    # held at zero coherence + surround speed so the patch is invisible
    # until the trial starts.
    set mp_tg [motionpatch $nDots $surr_pu 0.5]
    objName $mp_tg dots_target
    motionpatch_pointsize $mp_tg $ptSize
    motionpatch_color     $mp_tg $tgL $tgL $tgL 1.0
    motionpatch_masktype  $mp_tg 0
    motionpatch_coherence $mp_tg 0.0
    motionpatch_lifetime  $mp_tg $::mp_static::bg_lifetime
    motionpatch_direction $mp_tg 0.0
    motionpatch_speed     $mp_tg $surr_pu
    motionpatch_setSampler $mp_tg $texID 0
    motionpatch_samplermaskmode $mp_tg 1
    motionpatch_maskscale $mp_tg $::mp_static::shape_size
    metagroupAdd $mg $mp_tg

    addPreScript $mp_bg mp_static_update

    scaleObj $mg $::mp_static::patch_size $::mp_static::patch_size
    glistAddObject $mg 0

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ---------- actions ----------

proc mp_static_trigger {action} {
    switch -- $action {
        drop {
            set ::mp_static::play_t   0.0
            set ::mp_static::dropping 1
        }
        reset {
            set ::mp_static::dropping 0
            set ::mp_static::play_t   0.0
            catch {
                motionpatch_coherence dots_target 0.0
                motionpatch_speed     dots_target [mp_static_speed_pu $::mp_static::surround_speed]
                motionpatch_lifetime  dots_target $::mp_static::bg_lifetime
            }
        }
    }
    return
}

proc onDownArrow {} { mp_static_trigger drop }
proc onUpArrow   {} { mp_static_trigger reset }

# ---------- adjusters ----------

proc mp_static_set_pulse {n_snapshots sigma_ms base_coh} {
    set ::mp_static::n_snapshots $n_snapshots
    set ::mp_static::sigma_ms    $sigma_ms
    set ::mp_static::base_coh    $base_coh
    mp_static_recompute_tiles
}
proc mp_static_get_pulse {} {
    dict create n_snapshots 5 sigma_ms 50.0 base_coh 1.0
}

proc mp_static_set_duration {duration} {
    set ::mp_static::duration $duration
    mp_static_recompute_tiles
}
proc mp_static_get_duration {} { dict create duration 2.0 }

proc mp_static_set_target {target_speed target_dir_deg target_lifetime} {
    set ::mp_static::target_speed    $target_speed
    set ::mp_static::target_dir_deg  $target_dir_deg
    set ::mp_static::target_dir_rad  [expr {$target_dir_deg * 3.14159265 / 180.0}]
    set ::mp_static::target_lifetime $target_lifetime
}
proc mp_static_get_target {} {
    dict create target_speed 8.0 target_dir_deg 0.0 target_lifetime 0.5
}

proc mp_static_set_surround {surround_speed bg_lifetime} {
    set ::mp_static::surround_speed $surround_speed
    set ::mp_static::bg_lifetime    $bg_lifetime
    catch {
        motionpatch_speed    dots_bg [mp_static_speed_pu $surround_speed]
        motionpatch_lifetime dots_bg $bg_lifetime
    }
}
proc mp_static_get_surround {} {
    dict create surround_speed 3.0 bg_lifetime 0.08
}

proc mp_static_set_shape_size {shape_size} {
    set ::mp_static::shape_size $shape_size
    catch {
        motionpatch_maskscale dots_target $shape_size
        motionpatch_maskscale dots_bg     $shape_size
    }
}
proc mp_static_get_shape_size {} { dict create shape_size 0.10 }

proc mp_static_set_luminance {ball_lum surround_lum} {
    set ::mp_static::ball_lum     $ball_lum
    set ::mp_static::surround_lum $surround_lum
    catch {
        motionpatch_color dots_target $ball_lum     $ball_lum     $ball_lum     1.0
        motionpatch_color dots_bg     $surround_lum $surround_lum $surround_lum 1.0
    }
}
proc mp_static_get_luminance {} {
    dict create ball_lum 0.85 surround_lum 0.85
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup mp_static_setup {
    patch_size_dva {float 4.0 24.0 0.5 13.0 "Patch Size (dva)"}
    dot_density    {float 0.5 100.0 0.5 24.0 "Dot Density (dots/dva^2)"}
} -adjusters {static_actions static_pulse static_duration static_target static_surround static_shape static_luminance} \
  -label "Motion Pulsed (Static)"

workspace::adjuster static_actions {
    drop  {action "Start pulse train (↓)"}
    reset {action "Reset (↑)"}
} -target {} -proc mp_static_trigger -label "Actions"

workspace::adjuster static_pulse {
    n_snapshots {int 1 15 1 5 "Number of Pulses (N)"}
    sigma_ms    {float 5.0 200.0 5.0 50.0 "Pulse Width sigma (ms)"}
    base_coh    {float 0.0 1.0 0.05 1.0 "Peak Coherence"}
} -target {} -proc mp_static_set_pulse -getter mp_static_get_pulse \
  -label "Pulse Envelope"

workspace::adjuster static_duration {
    duration {float 0.5 5.0 0.1 2.0 "Duration (sec)"}
} -target {} -proc mp_static_set_duration -getter mp_static_get_duration \
  -label "Trial Duration"

workspace::adjuster static_target {
    target_speed    {float 0.0 30.0 0.5 8.0 "Peak Speed (dva/sec)"}
    target_dir_deg  {float 0 360 15 0 "Direction (deg)"}
    target_lifetime {float 0.05 2.0 0.05 0.5 "Peak Lifetime (sec)"}
} -target {} -proc mp_static_set_target -getter mp_static_get_target \
  -label "Target (Peak State)"

workspace::adjuster static_surround {
    surround_speed {float 0.0 15.0 0.5 3.0 "Surround Speed (dva/sec)"}
    bg_lifetime    {float 0.01 1.0 0.01 0.08 "Surround Lifetime (sec)"}
} -target {} -proc mp_static_set_surround -getter mp_static_get_surround \
  -label "Surround (Trough State)"

workspace::adjuster static_shape {
    shape_size {float 0.02 0.5 0.01 0.10 "Aperture Size (fraction of patch)"}
} -target {} -proc mp_static_set_shape_size -getter mp_static_get_shape_size \
  -label "Aperture Size"

workspace::adjuster static_luminance {
    ball_lum     {float 0.0 1.0 0.05 0.85 "Ball Luminance"}
    surround_lum {float 0.0 1.0 0.05 0.85 "Surround Luminance"}
} -target {} -proc mp_static_set_luminance -getter mp_static_get_luminance \
  -label "Per-Region Luminance" -colorpicker
