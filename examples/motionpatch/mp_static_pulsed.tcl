# examples/motionpatch/mp_static_pulsed.tcl
# Pulsed-motion demo, driven by mp_sim.
#
# Two modes:
#   centered  - one patch at screen center; pulse train modulates
#               coherence/speed/lifetime between target and surround.
#   mapping   - patches "jump" between grid positions on each pulse.
#               Position changes happen during OFF windows so the
#               surround flicker is uninterrupted -- a preview of the
#               prf::motionpatch_continuous protocol.
#
# Two pulse shapes:
#   gaussian      - sum-of-Gaussians envelope (sigma_ms width).
#   trapezoid     - cosine-ease-up + plateau + cosine-ease-down per
#                   pulse. Removes the steep leading edge that drives
#                   onset transients.
#
# All per-frame state (coherence, speed, lifetime, mask offset,
# direction) is read from a timeline dg compiled by mp_sim. The driver
# is just a frame-index lookup -- no inline math.
#
# Controls:
#   Down Arrow - start the pulse train
#   Up Arrow   - reset

load_Impro
package require mp_sim

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

# Convert dva/sec -> patch-local units / sec.
# double($v) / double($ps) forces float arithmetic. The stim2 slider
# normalizes integer-valued positions like 4.0 to "4" (Tcl integer
# rep), and Tcl's expr does integer division on int/int -- which
# silently returns 0 for any 0 < v < ps. Forcing both operands to
# double avoids that classic Tcl gotcha.
proc mp_static_speed_pu {v} {
    set ps $::mp_static::patch_size
    if {$ps <= 0} { return 0.0 }
    return [expr {double($v) / double($ps)}]
}

# Build a small N-position grid centered at the origin.
proc mp_static_grid_positions {n_per_side spacing_dva} {
    set out [list]
    set ps $::mp_static::patch_size
    if {$ps <= 0} { return [list {0 0}] }
    if {$n_per_side <= 1} { return [list {0 0}] }
    set half [expr {($n_per_side - 1) / 2.0}]
    for {set yi 0} {$yi < $n_per_side} {incr yi} {
        for {set xi 0} {$xi < $n_per_side} {incr xi} {
            # Position in dva, then converted to patch-local units for
            # mask_offset (motionpatch.c uses mask offsets normalized
            # to the patch dimensions).
            set x_dva [expr {($xi - $half) * $spacing_dva}]
            set y_dva [expr {($yi - $half) * $spacing_dva}]
            lappend out [list [expr {$x_dva / $ps}] [expr {$y_dva / $ps}]]
        }
    }
    return $out
}

# Compile a timeline appropriate for the current mode + shape settings.
# Stores the timeline dg name in ::mp_static::timeline. Called on
# drop and on adjuster changes that affect the timeline.
proc mp_static_build_timeline {} {
    set ps  $::mp_static::patch_size
    set tgt_speed_pu [mp_static_speed_pu $::mp_static::target_speed]
    set sur_speed_pu [mp_static_speed_pu $::mp_static::surround_speed]

    set endpoints [dict create \
        target [dict create \
            coh   1.0 \
            speed $tgt_speed_pu \
            dir   $::mp_static::target_dir_rad \
            life  $::mp_static::target_lifetime] \
        surround [dict create \
            coh   0.0 \
            speed $sur_speed_pu \
            dir   0.0 \
            life  $::mp_static::bg_lifetime]]

    if {$::mp_static::layout eq "centered"} {
        # Single position; envelope produces a pulse train.
        switch -- $::mp_static::shape {
            gaussian {
                set envelope [dict create \
                    kind     sum_gaussians \
                    n_pulses $::mp_static::n_pulses \
                    sigma_ms $::mp_static::sigma_ms \
                    base_coh 1.0]
            }
            trapezoid {
                set centers [mp_sim::evenly_spaced_pulse_centers \
                                 $::mp_static::n_pulses \
                                 $::mp_static::duration]
                set envelope [dict create \
                    kind        trapezoid_train \
                    centers     $centers \
                    plateau_dur [expr {$::mp_static::plateau_ms / 1000.0}] \
                    ease_dur    [expr {$::mp_static::ease_ms    / 1000.0}] \
                    base_coh    1.0]
            }
        }
        # Optional ease-in to suppress the very first pulse's leading
        # edge (the trial-onset transient). When ease_in_ms is 0 this
        # is omitted.
        if {$::mp_static::ease_in_ms > 0} {
            set rt1 [expr {$::mp_static::ease_in_ms / 1000.0}]
            set ease_env [dict create kind cosine_ramp t0 0.0 t1 $rt1 base_coh 1.0]
            set envelope [dict create kind product parts [list $ease_env $envelope]]
        }
        set spec [dict create \
            meta [dict create \
                duration       $::mp_static::duration \
                dt             0.0167 \
                patch_size_dva $ps] \
            endpoints  $endpoints \
            envelope   $envelope \
            trajectory {kind static}]

        if {[dg_exists mp_static_tl]} { dg_delete mp_static_tl }
        set ::mp_static::timeline [mp_sim::compile_spec $spec -gname mp_static_tl]

    } else {
        # Mapping mode: trapezoid_train + step_sequence via
        # compile_mapping_spec. Bypass the ease_in/centered settings
        # in favor of the per-tile ease.
        set positions [mp_static_grid_positions \
                           $::mp_static::grid_n \
                           $::mp_static::grid_spacing_dva]
        if {$::mp_static::random_order} {
            set positions [mp_static_shuffle $positions]
        }

        set npos [llength $positions]
        set per_tile_dirs [list]
        if {$::mp_static::random_dir} {
            for {set i 0} {$i < $npos} {incr i} {
                lappend per_tile_dirs [mp_static_pick_direction]
            }
        }

        set base_spec [dict create \
            meta [dict create dt 0.0167 patch_size_dva $ps] \
            endpoints $endpoints]

        if {[dg_exists mp_static_tl]} { dg_delete mp_static_tl }
        set ::mp_static::timeline [mp_sim::compile_mapping_spec $base_spec \
            -positions    $positions \
            -on_dur       [expr {$::mp_static::on_ms       / 1000.0}] \
            -off_dur      [expr {$::mp_static::off_ms      / 1000.0}] \
            -ease_dur     [expr {$::mp_static::tile_ease_ms / 1000.0}] \
            -direction    $::mp_static::target_dir_rad \
            -directions   $per_tile_dirs \
            -gname mp_static_tl]
    }
}

# Tcl Fisher-Yates shuffle. Returns a new list.
proc mp_static_shuffle {l} {
    set n [llength $l]
    for {set i [expr {$n - 1}]} {$i > 0} {incr i -1} {
        set j [expr {int(rand() * ($i + 1))}]
        set tmp [lindex $l $i]
        lset l $i [lindex $l $j]
        lset l $j $tmp
    }
    return $l
}

# Pick a target direction in radians. n_dirs == 0 -> continuous
# uniform; n_dirs > 0 -> one of N evenly-spaced angles starting at
# target_dir_rad (so that "random off" gives the configured cardinal
# direction as the first option).
proc mp_static_pick_direction {} {
    set n $::mp_static::n_dirs
    if {$n <= 0} {
        return [expr {rand() * 2.0 * 3.14159265358979}]
    }
    set k [expr {int(rand() * $n)}]
    return [expr {$::mp_static::target_dir_rad + $k * 2.0 * 3.14159265358979 / double($n)}]
}

# Per-frame driver: pure timeline lookup, applies state to the
# motionpatches. Both modes share this -- the only mode-specific code
# is in the timeline builder.
proc mp_static_update {} {
    # StimTimeF (float ms) dt source: int StimTime makes dt alternate 8/9 ms
    # at 120 Hz, which the play_t accumulator carries into per-frame judder.
    set t [expr {$::StimTimeF / 1000.0}]
    set dt [expr {$t - $::mp_static::last_t}]
    if {$dt < 0.0 || $dt > 0.1} { set dt 0.016 }
    set ::mp_static::last_t $t

    if {!$::mp_static::dropping} { return }

    set ::mp_static::play_t [expr {$::mp_static::play_t + $dt}]
    set tplay $::mp_static::play_t

    set tl $::mp_static::timeline
    if {$tl eq "" || ![dg_exists $tl]} { return }
    set tl_dt [dl_get $tl:dt 0]
    set nf    [dl_length $tl:t]
    set i     [expr {int($tplay / $tl_dt)}]
    if {$i >= $nf} {
        # Trial ended -- collapse inside dots to surround statistics
        # so the ball perceptually disappears rather than freezing.
        set ::mp_static::dropping 0
        motionpatch_coherence dots_target 0.0
        motionpatch_speed     dots_target [mp_static_speed_pu $::mp_static::surround_speed]
        motionpatch_lifetime  dots_target $::mp_static::bg_lifetime
        return
    }

    set coh [dl_get $tl:coherence    $i]
    set sp  [dl_get $tl:speed        $i]
    set lf  [dl_get $tl:lifetime_s   $i]
    set mox [dl_get $tl:mask_offset_x $i]
    set moy [dl_get $tl:mask_offset_y $i]
    set dir [dl_get $tl:direction    $i]

    motionpatch_maskoffset dots_target $mox $moy
    motionpatch_maskoffset dots_bg     $mox $moy
    motionpatch_direction  dots_target $dir
    motionpatch_speed      dots_target $sp
    motionpatch_lifetime   dots_target $lf
    motionpatch_coherence  dots_target $coh
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
        # Aperture diameter is shape_size * patch_size. With patch=13
        # the default 0.15 gives a 1.95 dva aperture -- enough room to
        # see ~1 dva of coherent drift without the dots wrapping.
        variable shape_size       0.15

        # Mode + shape selection. Default to mapping with trapezoid
        # so the demo opens with the rapid-RF-mapping behavior visible
        # right away.
        variable layout    mapping     ;# centered | mapping
        variable shape     trapezoid   ;# gaussian | trapezoid

        # Centered-mode pulse params (used by both gaussian and
        # trapezoid shapes). plateau_ms is sized to match the mapping
        # on_ms so switching layouts doesn't change the perceived
        # peak speed.
        variable n_pulses    5
        variable sigma_ms    80.0      ;# gaussian width (FWHM ~190ms)
        variable plateau_ms 250.0      ;# trapezoid full-on duration
        variable ease_ms     50.0      ;# trapezoid up/down ramp
        variable ease_in_ms   0.0      ;# overall onset ramp (centered, optional)

        # Mapping-mode params. on_dur = 250ms gives 1 dva of drift at
        # target_speed = 4 dva/sec -- comfortably above visibility
        # threshold in the 1.95 dva aperture.
        variable grid_n              3
        variable grid_spacing_dva    3.0
        variable on_ms             250.0
        variable off_ms            100.0
        variable tile_ease_ms       50.0

        # Mapping-mode randomization. random_order shuffles the
        # position visit order each drop; random_dir picks a fresh
        # direction per tile (otherwise all tiles use target_dir_deg).
        # n_dirs sets the number of distinct angles to sample from --
        # 0 = continuous (uniform 0..2pi), N = N evenly-spaced angles.
        variable random_order   0
        variable random_dir     0
        variable n_dirs         0

        # Endpoint params (target = peak, surround = trough).
        # Defaults sized so target_speed = 4 dva/sec is comfortably
        # visible: surround_speed kept low (1.5) so the inside
        # coherent drift has clear contrast against background flicker.
        variable target_speed     4.0
        variable target_dir_deg   0.0
        variable target_dir_rad   0.0
        variable target_lifetime  0.5

        variable surround_speed   1.5
        variable bg_lifetime      0.08

        variable ball_lum         0.85
        variable surround_lum     0.85

        # Fixation spot (yellow donut + black center) on top of the
        # patch. Sized in dva, NOT scaled by the patch metagroup, so
        # screen-space size stays constant as patch_size changes.
        variable fix_r            0.15
        variable fix_visible      1
        variable fix_id           {}

        variable timeline {}
    }
    set ::mp_static::patch_size $patch_size_dva

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

    # Surround: full-field flicker, dots draw OUTSIDE the circular mask.
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

    # Target: dots draw INSIDE the circular mask. Held at zero
    # coherence + surround speed so the patch is invisible until the
    # trial starts.
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

    # Fixation spot. Lives outside the patch metagroup so its screen-
    # space size is independent of patch_size. Yellow donut + black
    # center is the standard psychophysics fixspot.
    set fix_r $::mp_static::fix_r
    set fix_mg [metagroup]
    set fix_outer [polygon]
    polycirc $fix_outer 1
    polycolor $fix_outer 0.7 0.7 0.1
    scaleObj $fix_outer [expr {2.0 * $fix_r}]
    set fix_inner [polygon]
    polycirc $fix_inner 1
    polycolor $fix_inner 0.0 0.0 0.0
    scaleObj $fix_inner [expr {0.6 * 2.0 * $fix_r}]
    metagroupAdd $fix_mg $fix_outer
    metagroupAdd $fix_mg $fix_inner
    set ::mp_static::fix_id $fix_mg
    glistAddObject $fix_mg 0
    if {!$::mp_static::fix_visible} { catch {setVisible $fix_mg 0} }

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ---------- actions ----------

proc mp_static_trigger {action} {
    switch -- $action {
        drop {
            mp_static_build_timeline
            set ::mp_static::play_t   0.0
            set ::mp_static::dropping 1
        }
        reset {
            set ::mp_static::dropping 0
            set ::mp_static::play_t   0.0
            catch {
                motionpatch_maskoffset dots_target 0.0 0.0
                motionpatch_maskoffset dots_bg     0.0 0.0
                motionpatch_coherence  dots_target 0.0
                motionpatch_speed      dots_target [mp_static_speed_pu $::mp_static::surround_speed]
                motionpatch_lifetime   dots_target $::mp_static::bg_lifetime
            }
        }
    }
    return
}

proc onDownArrow {} { mp_static_trigger drop }
proc onUpArrow   {} { mp_static_trigger reset }

# ---------- adjusters ----------

proc mp_static_set_layout {layout} { set ::mp_static::layout $layout }
proc mp_static_get_layout {} { dict create layout mapping }

proc mp_static_set_shape {shape} { set ::mp_static::shape $shape }
proc mp_static_get_shape {} { dict create shape trapezoid }

proc mp_static_set_pulse {n_pulses sigma_ms plateau_ms ease_ms ease_in_ms} {
    set ::mp_static::n_pulses    $n_pulses
    set ::mp_static::sigma_ms    $sigma_ms
    set ::mp_static::plateau_ms  $plateau_ms
    set ::mp_static::ease_ms     $ease_ms
    set ::mp_static::ease_in_ms  $ease_in_ms
}
proc mp_static_get_pulse {} {
    dict create n_pulses 5 sigma_ms 80.0 plateau_ms 250.0 ease_ms 50.0 ease_in_ms 0.0
}

proc mp_static_set_grid {grid_n grid_spacing_dva on_ms off_ms tile_ease_ms} {
    set ::mp_static::grid_n            $grid_n
    set ::mp_static::grid_spacing_dva  $grid_spacing_dva
    set ::mp_static::on_ms             $on_ms
    set ::mp_static::off_ms            $off_ms
    set ::mp_static::tile_ease_ms      $tile_ease_ms
}
proc mp_static_get_grid {} {
    dict create grid_n 3 grid_spacing_dva 3.0 on_ms 250.0 off_ms 100.0 tile_ease_ms 50.0
}

proc mp_static_set_random {random_order random_dir n_dirs} {
    set ::mp_static::random_order $random_order
    set ::mp_static::random_dir   $random_dir
    set ::mp_static::n_dirs       $n_dirs
}
proc mp_static_get_random {} {
    dict create random_order 0 random_dir 0 n_dirs 0
}

proc mp_static_set_duration {duration} { set ::mp_static::duration $duration }
proc mp_static_get_duration {} { dict create duration 2.0 }

proc mp_static_set_target {target_speed target_dir_deg target_lifetime} {
    set ::mp_static::target_speed    $target_speed
    set ::mp_static::target_dir_deg  $target_dir_deg
    set ::mp_static::target_dir_rad  [expr {$target_dir_deg * 3.14159265 / 180.0}]
    set ::mp_static::target_lifetime $target_lifetime
}
proc mp_static_get_target {} {
    dict create target_speed 4.0 target_dir_deg 0.0 target_lifetime 0.5
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
    dict create surround_speed 1.5 bg_lifetime 0.08
}

proc mp_static_set_shape_size {shape_size} {
    set ::mp_static::shape_size $shape_size
    catch {
        motionpatch_maskscale dots_target $shape_size
        motionpatch_maskscale dots_bg     $shape_size
    }
}
proc mp_static_get_shape_size {} { dict create shape_size 0.15 }

proc mp_static_set_fix {fix_r fix_visible} {
    set ::mp_static::fix_r       $fix_r
    set ::mp_static::fix_visible $fix_visible
    # Live-update: rebuild the fixspot is overkill; just rescale its
    # children. We don't track the polygon ids individually, so we
    # toggle visibility live and let the next setup reflect a new
    # size. (For a demo this is fine; live size changes apply on
    # next setup.)
    if {$::mp_static::fix_id ne ""} {
        catch {setVisible $::mp_static::fix_id $fix_visible}
    }
}
proc mp_static_get_fix {} {
    dict create fix_r 0.15 fix_visible 1
}

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
} -adjusters {static_actions static_layout static_shape static_pulse static_grid static_random static_duration static_target static_surround static_shape_size static_luminance static_fix} \
  -label "Motion Pulsed (Static, mp_sim driven)"

workspace::adjuster static_actions {
    drop  {action "Start pulse train (↓)"}
    reset {action "Reset (↑)"}
} -target {} -proc mp_static_trigger -label "Actions"

workspace::adjuster static_layout {
    layout {choice {centered mapping} mapping "Layout"}
} -target {} -proc mp_static_set_layout -getter mp_static_get_layout \
  -label "Layout (centered or jumping grid)"

workspace::adjuster static_shape {
    shape {choice {gaussian trapezoid} trapezoid "Pulse Shape"}
} -target {} -proc mp_static_set_shape -getter mp_static_get_shape \
  -label "Pulse Shape (centered mode)"

workspace::adjuster static_pulse {
    n_pulses    {int 1 15 1 5 "Number of Pulses (centered)"}
    sigma_ms    {float 5.0 200.0 5.0 80.0 "Gaussian Sigma (ms)"}
    plateau_ms  {float 5.0 400.0 5.0 250.0 "Trapezoid Plateau (ms)"}
    ease_ms     {float 0.0 200.0 5.0 50.0 "Trapezoid Ease (ms)"}
    ease_in_ms  {float 0.0 500.0 10.0 0.0 "Trial Onset Ease (ms, centered)"}
} -target {} -proc mp_static_set_pulse -getter mp_static_get_pulse \
  -label "Centered Pulse Params"

workspace::adjuster static_grid {
    grid_n           {int 1 7 2 3 "Grid Size (NxN, mapping)"}
    grid_spacing_dva {float 0.5 8.0 0.25 3.0 "Grid Spacing (dva)"}
    on_ms            {float 50.0 500.0 10.0 250.0 "On Duration (ms)"}
    off_ms           {float 0.0 500.0 10.0 100.0 "Off Duration (ms)"}
    tile_ease_ms     {float 0.0 200.0 5.0 50.0 "Tile Ease (ms)"}
} -target {} -proc mp_static_set_grid -getter mp_static_get_grid \
  -label "Mapping Grid Params"

workspace::adjuster static_random {
    random_order {bool 0 "Randomize Position Order"}
    random_dir   {bool 0 "Randomize Direction Per Tile"}
    n_dirs       {int 0 16 1 0 "Direction Set Size (0 = continuous)"}
} -target {} -proc mp_static_set_random -getter mp_static_get_random \
  -label "Mapping Randomization"

workspace::adjuster static_duration {
    duration {float 0.5 5.0 0.1 2.0 "Duration (centered, sec)"}
} -target {} -proc mp_static_set_duration -getter mp_static_get_duration \
  -label "Trial Duration (centered)"

workspace::adjuster static_target {
    target_speed    {float 0.0 30.0 0.5 4.0 "Peak Speed (dva/sec)"}
    target_dir_deg  {float 0 360 15 0 "Direction (deg)"}
    target_lifetime {float 0.05 2.0 0.05 0.5 "Peak Lifetime (sec)"}
} -target {} -proc mp_static_set_target -getter mp_static_get_target \
  -label "Target (Peak State)"

workspace::adjuster static_surround {
    surround_speed {float 0.0 15.0 0.5 1.5 "Surround Speed (dva/sec)"}
    bg_lifetime    {float 0.01 1.0 0.01 0.08 "Surround Lifetime (sec)"}
} -target {} -proc mp_static_set_surround -getter mp_static_get_surround \
  -label "Surround (Trough State)"

workspace::adjuster static_shape_size {
    shape_size {float 0.02 0.5 0.01 0.15 "Aperture Size (fraction of patch)"}
} -target {} -proc mp_static_set_shape_size -getter mp_static_get_shape_size \
  -label "Aperture Size"

workspace::adjuster static_luminance {
    ball_lum     {float 0.0 1.0 0.05 0.85 "Ball Luminance"}
    surround_lum {float 0.0 1.0 0.05 0.85 "Surround Luminance"}
} -target {} -proc mp_static_set_luminance -getter mp_static_get_luminance \
  -label "Per-Region Luminance" -colorpicker

workspace::adjuster static_fix {
    fix_r       {float 0.05 0.6 0.025 0.15 "Fixation Spot Radius (dva)"}
    fix_visible {bool 1 "Show Fixation Spot"}
} -target {} -proc mp_static_set_fix -getter mp_static_get_fix \
  -label "Fixation Spot"
