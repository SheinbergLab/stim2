# examples/motionpatch/mp_pulsed.tcl
# Pulsed-trajectory demo: smooth-translating aperture with inside-dot
# coherence gated by a sum-of-Gaussians envelope at N tile times.
#
# Tests the Singer & Sheinberg "ordered-snapshots vs continuous
# trajectory" question without the planko-specific machinery:
# trajectories here are simple parametric paths (horizontal sweep
# or parabolic arc), so the pulsing behavior can be validated and
# tuned in isolation before being ported back into the experimental
# framework.
#
# Modes:
#   continuous -- aperture follows the trajectory; coherence held at
#                 base value throughout. Equivalent to mp_planko's
#                 trajectory_aperture mode. Use as the control.
#   pulsed     -- same trajectory and aperture motion, but inside
#                 coherence is the sum of N Gaussians centered at
#                 tile times t_i = T*(i+0.5)/N. Between hotspots the
#                 coherent fraction drops toward 0 -- the ball fades
#                 to invisibility (motion-defined only) and reappears
#                 at the next hotspot. Retinal sampling matches
#                 continuous; only the temporal modulation differs.
#
# Trajectory presets:
#   sweep    -- straight-line horizontal sweep at constant speed
#   arc      -- ballistic parabola (gravity-only fall from a throw)
#
# Controls:
#   Down Arrow - drop / replay
#   Up Arrow   - reset (regenerates trajectory samples + envelope)

load_Impro

# ============================================================
# STIM CODE
# ============================================================

proc mp_pulsed_make_circle_tex {size} {
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

# ---------- trajectory generation ----------

# Build a parametric trajectory at frame-rate sampling. Stores
# per-frame (t, x, y, vx, vy) lists in the namespace. Returns
# nothing; callers read ::mp_pulsed::traj_*.
proc mp_pulsed_build_trajectory {} {
    # Sample the trajectory at a fixed nominal step (~16.67 ms) for
    # storage. Per-frame replay re-indexes into this by real time, so
    # the actual display rate doesn't matter -- we just need fine
    # enough sampling that the lookup is smooth.
    set step 0.01667
    set T   $::mp_pulsed::duration
    set ts {}; set xs {}; set ys {}; set vxs {}; set vys {}

    switch -- $::mp_pulsed::preset {
        sweep {
            # Constant-velocity left-to-right horizontal sweep.
            set x0  $::mp_pulsed::sweep_x0
            set x1  $::mp_pulsed::sweep_x1
            set y0  $::mp_pulsed::sweep_y
            set vx [expr {($x1 - $x0) / $T}]
            set vy 0.0
            for {set t 0} {$t <= $T + 1e-6} {set t [expr {$t + $step}]} {
                lappend ts $t
                lappend xs [expr {$x0 + $vx * $t}]
                lappend ys $y0
                lappend vxs $vx
                lappend vys $vy
            }
        }
        arc {
            # Ballistic parabola: thrown from (x0,y0) with initial
            # velocity (vx0, vy0), gravity g (negative = down).
            set x0  $::mp_pulsed::arc_x0
            set y0  $::mp_pulsed::arc_y0
            set vx0 $::mp_pulsed::arc_vx0
            set vy0 $::mp_pulsed::arc_vy0
            set g   $::mp_pulsed::arc_g
            for {set t 0} {$t <= $T + 1e-6} {set t [expr {$t + $step}]} {
                lappend ts $t
                lappend xs [expr {$x0 + $vx0 * $t}]
                lappend ys [expr {$y0 + $vy0 * $t + 0.5 * $g * $t * $t}]
                lappend vxs $vx0
                lappend vys [expr {$vy0 + $g * $t}]
            }
        }
    }

    set ::mp_pulsed::traj_t  $ts
    set ::mp_pulsed::traj_x  $xs
    set ::mp_pulsed::traj_y  $ys
    set ::mp_pulsed::traj_vx $vxs
    set ::mp_pulsed::traj_vy $vys
    set ::mp_pulsed::traj_dt $step
    set ::mp_pulsed::traj_n  [llength $ts]

    # Tile times: N evenly-spaced centers at mid-interval positions.
    set ::mp_pulsed::tile_times [list]
    set N $::mp_pulsed::n_snapshots
    if {$N > 0} {
        for {set k 0} {$k < $N} {incr k} {
            lappend ::mp_pulsed::tile_times \
                [expr {$T * ($k + 0.5) / $N}]
        }
    }
}

# Sum-of-Gaussians envelope. Returns coherence value in [0, base].
proc mp_pulsed_envelope {tplay} {
    set sigma [expr {$::mp_pulsed::sigma_ms / 1000.0}]
    if {$sigma <= 0.0} { return $::mp_pulsed::base_coh }
    set sum 0.0
    foreach ti $::mp_pulsed::tile_times {
        set z [expr {($tplay - $ti) / $sigma}]
        set sum [expr {$sum + exp(-0.5 * $z * $z)}]
    }
    set v [expr {$::mp_pulsed::base_coh * $sum}]
    if {$v > $::mp_pulsed::base_coh} { set v $::mp_pulsed::base_coh }
    if {$v < 0.0} { set v 0.0 }
    return $v
}

# Convert dva/sec -> motionpatch_speed (patch-local-units per second).
# motionpatch.c does dt-scaled integration, so this is frame-rate-
# independent. The conversion is purely the dva-to-patch-local mapping.
proc mp_pulsed_speed_from_deg_sec {v} {
    set ps $::mp_pulsed::patch_size
    if {$ps <= 0} { return 0.0 }
    return [expr {$v / $ps}]
}

# ---------- per-frame driver ----------

proc mp_pulsed_index_for_time {tsec} {
    set dt $::mp_pulsed::traj_dt
    set n  $::mp_pulsed::traj_n
    if {$dt <= 0 || $n <= 0} { return 0 }
    set idx [expr {int($tsec / $dt)}]
    if {$idx < 0} { return 0 }
    if {$idx >= $n} { return [expr {$n - 1}] }
    return $idx
}

proc mp_pulsed_update {} {
    set t [expr {$::StimTime / 1000.0}]
    set dt [expr {$t - $::mp_pulsed::last_t}]
    if {$dt < 0.0 || $dt > 0.1} { set dt 0.016 }
    set ::mp_pulsed::last_t $t

    if {!$::mp_pulsed::dropping} { return }

    set ::mp_pulsed::play_t [expr {$::mp_pulsed::play_t + $dt}]
    set tplay $::mp_pulsed::play_t
    set T [expr {$::mp_pulsed::traj_dt * ($::mp_pulsed::traj_n - 1)}]
    if {$tplay > $T} {
        set ::mp_pulsed::dropping 0
        # Trajectory ended -- collapse inside dots to match the
        # surround flicker so the ball perceptually disappears
        # rather than freezing at the last (often high-speed) state.
        # Without this, the per-frame driver's last write (terminal
        # trajectory velocity) would leave inside dots flickering at
        # peak ball speed in random directions, visually distinct
        # from the slower surround flicker.
        set surround_speed [mp_pulsed_speed_from_deg_sec 5.0]
        motionpatch_coherence dots_target 0.0
        motionpatch_speed     dots_target $surround_speed
        return
    }
    set i [mp_pulsed_index_for_time $tplay]

    set x  [lindex $::mp_pulsed::traj_x  $i]
    set y  [lindex $::mp_pulsed::traj_y  $i]
    set vx [lindex $::mp_pulsed::traj_vx $i]
    set vy [lindex $::mp_pulsed::traj_vy $i]

    set ps $::mp_pulsed::patch_size
    set ox [expr {$x / $ps}]
    set oy [expr {$y / $ps}]
    set vmag [expr {hypot($vx, $vy)}]
    if {$vmag < 1e-6} {
        set dir 0.0
    } else {
        set dir [expr {atan2($vy, $vx)}]
    }
    set v_clamped $vmag
    if {$v_clamped > $::mp_pulsed::max_speed_deg_sec} {
        set v_clamped $::mp_pulsed::max_speed_deg_sec
    }
    set sp [mp_pulsed_speed_from_deg_sec $v_clamped]

    motionpatch_maskoffset dots_target $ox $oy
    motionpatch_maskoffset dots_bg     $ox $oy
    motionpatch_direction  dots_target $dir
    motionpatch_speed      dots_target $sp

    # Pulsed mode gates ONLY coherence (motion energy). Ball and
    # surround luminance are independent visualization parameters --
    # turning surround_lum down to see the pulsing more clearly
    # against a quiet field doesn't change the experimental
    # manipulation, which is purely motion-defined. With ball_lum ==
    # surround_lum (the experimental default), the ball is invisible
    # between pulses because inside-dot statistics match the surround
    # flicker. With ball_lum > surround_lum, the ball is luminance-
    # visible throughout and the pulses add motion-energy bursts on
    # top of a continuously-visible blob.
    if {$::mp_pulsed::mode eq "pulsed"} {
        set coh [mp_pulsed_envelope $tplay]
    } else {
        set coh $::mp_pulsed::base_coh
    }
    motionpatch_coherence dots_target $coh
}

# ---------- setup ----------

proc mp_pulsed_setup {patch_size_dva dot_density} {
    glistInit 1
    resetObjList
    shaderImageReset

    namespace eval ::mp_pulsed {
        variable mode             continuous
        variable preset           arc
        variable playing          1
        variable dropping         0
        variable last_t           0.0
        variable play_t           0.0

        # Sweep preset
        variable sweep_x0        -5.0
        variable sweep_x1         5.0
        variable sweep_y          0.0

        # Arc preset
        variable arc_x0          -5.0
        variable arc_y0           4.0
        variable arc_vx0          7.0
        variable arc_vy0          2.0
        variable arc_g          -10.0

        variable duration         1.5
        variable patch_size      13.0
        variable max_speed_deg_sec 30.0
        variable shape_size       0.08
        variable bg_lifetime      0.05   ;# seconds; ~3 frames at 60Hz
        variable target_lifetime  0.5    ;# seconds (was 30 frames)
        # Per-region luminance (0..1). Default both at 0.8 = motion-
        # only definition. Setting surround_lum to 0 effectively
        # "turns off" the surround: only the ball's luminance dots
        # are visible, against a dark background. Useful for
        # disambiguating motion-defined effects from the noisier
        # full-field condition.
        variable ball_lum         0.8
        variable surround_lum     0.8

        # Pulsed-trajectory parameters
        variable n_snapshots      7
        variable sigma_ms        40.0  ;# trough depth depends on
                                        ;# sigma vs inter-tile interval
                                        ;# (T/N). For default T=1.5,
                                        ;# N=7: Delta=214ms; sigma<=
                                        ;# Delta/5.4 (~40ms) gives
                                        ;# troughs near zero.
        # Tie ratio: when sigma is computed from (N, ratio) via the
        # density adjuster, sigma = Delta/ratio = T/(N*ratio). Keeping
        # ratio constant across N values keeps trough depth constant,
        # so changing N alone scales trajectory sampling density
        # without confounding saliency-per-pulse changes. Trough depth
        # at midpoint = 2 * exp(-0.5 * (ratio/2)^2):
        #   ratio 6  -> 0.4%   (crisp, well-separated flashes)
        #   ratio 5  -> 4%     (clear discrete pulses; default)
        #   ratio 4  -> 27%    (visible pulsing, ball never invisible)
        #   ratio 3  -> 70%    (mild modulation, near-continuous)
        variable sigma_ratio      5.0
        variable base_coh         1.0

        variable traj_t  {}
        variable traj_x  {}
        variable traj_y  {}
        variable traj_vx {}
        variable traj_vy {}
        variable traj_dt 0.0
        variable traj_n  0
        variable tile_times {}

        variable last_patch_size  13.0
        variable last_dot_density 24.0
    }
    set ::mp_pulsed::patch_size       $patch_size_dva
    set ::mp_pulsed::last_patch_size  $patch_size_dva
    set ::mp_pulsed::last_dot_density $dot_density

    mp_pulsed_build_trajectory

    set nDots [expr {int($dot_density * $patch_size_dva * $patch_size_dva)}]
    if {$nDots < 100} { set nDots 100 }

    set tex   [mp_pulsed_make_circle_tex 256]
    set texID [shaderImageID $tex]

    set initialSpeed [mp_pulsed_speed_from_deg_sec 5.0]
    set ptSize 3.0

    set mg [metagroup]
    objName $mg patch

    set bgL $::mp_pulsed::surround_lum
    set tgL $::mp_pulsed::ball_lum

    set mp_bg [motionpatch $nDots $initialSpeed 0.5]
    objName $mp_bg dots_bg
    motionpatch_pointsize $mp_bg $ptSize
    motionpatch_color $mp_bg $bgL $bgL $bgL 1.0
    motionpatch_masktype $mp_bg 0
    motionpatch_coherence $mp_bg 0.0
    motionpatch_lifetime $mp_bg $::mp_pulsed::bg_lifetime
    motionpatch_direction $mp_bg 0.0
    motionpatch_speed $mp_bg $initialSpeed
    motionpatch_setSampler $mp_bg $texID 0
    motionpatch_samplermaskmode $mp_bg 2
    motionpatch_maskscale $mp_bg $::mp_pulsed::shape_size
    metagroupAdd $mg $mp_bg

    set mp_tg [motionpatch $nDots $initialSpeed 0.5]
    objName $mp_tg dots_target
    motionpatch_pointsize $mp_tg $ptSize
    motionpatch_color $mp_tg $tgL $tgL $tgL 1.0
    motionpatch_masktype $mp_tg 0
    motionpatch_coherence $mp_tg 1.0
    motionpatch_lifetime $mp_tg $::mp_pulsed::target_lifetime
    motionpatch_direction $mp_tg 0.0
    motionpatch_speed $mp_tg $initialSpeed
    motionpatch_setSampler $mp_tg $texID 0
    motionpatch_samplermaskmode $mp_tg 1
    motionpatch_maskscale $mp_tg $::mp_pulsed::shape_size
    metagroupAdd $mg $mp_tg

    addPreScript $mp_bg mp_pulsed_update

    scaleObj $mg $::mp_pulsed::patch_size $::mp_pulsed::patch_size
    glistAddObject $mg 0

    # Fixation spot (yellow with black center) on top of the patch,
    # for psychophysics where the subject must hold gaze. Positioned
    # at screen center; trajectory percepts (continuous and pulsed)
    # are then experienced with consistent retinal sampling.
    set fix_r 0.2
    set fix_mg [metagroup]
    set fix_outer [polygon]
    polycirc $fix_outer 1
    polycolor $fix_outer 0.7 0.7 0.1
    scaleObj $fix_outer [expr {2.0 * $fix_r}]
    set fix_inner [polygon]
    polycirc $fix_inner 1
    polycolor $fix_inner 0.0 0.0 0.0
    scaleObj $fix_inner [expr {0.6 * $fix_r}]
    metagroupAdd $fix_mg $fix_outer
    metagroupAdd $fix_mg $fix_inner
    glistAddObject $fix_mg 0

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1

    # Pre-position aperture at trajectory start (zero coherence so the
    # ball is invisible until "drop").
    if {$::mp_pulsed::traj_n > 0} {
        set x0 [lindex $::mp_pulsed::traj_x 0]
        set y0 [lindex $::mp_pulsed::traj_y 0]
        set ps $::mp_pulsed::patch_size
        motionpatch_maskoffset dots_target [expr {$x0/$ps}] [expr {$y0/$ps}]
        motionpatch_maskoffset dots_bg     [expr {$x0/$ps}] [expr {$y0/$ps}]
    }
    motionpatch_speed dots_target 0.0
    redraw
}

# ---------- actions ----------

proc mp_pulsed_trigger {action} {
    switch -- $action {
        drop {
            set ::mp_pulsed::play_t   0.0
            set ::mp_pulsed::dropping 1
        }
        reset {
            mp_pulsed_build_trajectory
            set ::mp_pulsed::dropping 0
            set ::mp_pulsed::play_t   0.0
            # Reposition aperture at trajectory start, zero target
            # speed so the ball is held static at its launch point.
            if {$::mp_pulsed::traj_n > 0} {
                set ps $::mp_pulsed::patch_size
                set x0 [lindex $::mp_pulsed::traj_x 0]
                set y0 [lindex $::mp_pulsed::traj_y 0]
                catch {
                    motionpatch_maskoffset dots_target [expr {$x0/$ps}] [expr {$y0/$ps}]
                    motionpatch_maskoffset dots_bg     [expr {$x0/$ps}] [expr {$y0/$ps}]
                    motionpatch_speed      dots_target 0.0
                }
            }
        }
    }
    return
}

proc onDownArrow {} { mp_pulsed_trigger drop }
proc onUpArrow   {} { mp_pulsed_trigger reset }

# ---------- adjusters ----------

proc mp_pulsed_set_mode {mode} { set ::mp_pulsed::mode $mode }
proc mp_pulsed_get_mode {} { dict create mode continuous }

proc mp_pulsed_set_preset {preset} {
    set ::mp_pulsed::preset $preset
    mp_pulsed_build_trajectory
}
proc mp_pulsed_get_preset {} { dict create preset arc }

proc mp_pulsed_set_pulse {n_snapshots sigma_ms base_coh} {
    set ::mp_pulsed::n_snapshots $n_snapshots
    set ::mp_pulsed::sigma_ms    $sigma_ms
    set ::mp_pulsed::base_coh    $base_coh
    mp_pulsed_build_trajectory  ;# rebuild tile times
}
proc mp_pulsed_get_pulse {} {
    dict create n_snapshots 7 sigma_ms 40.0 base_coh 1.0
}

# Tied-density control: set N and trough-depth ratio in one step.
# sigma is auto-computed from sigma = T / (N * ratio) so that trough
# depth between adjacent pulses stays constant as N varies.
# Use this when you want N as a single "trajectory density" knob
# (low N = sparse, easy-to-track snapshots; high N = dense, brief
# flashes approaching continuous flow), without having to manually
# retune sigma to keep gaps deep.
proc mp_pulsed_set_density {n_snapshots ratio} {
    set ::mp_pulsed::n_snapshots $n_snapshots
    set ::mp_pulsed::sigma_ratio $ratio
    set T $::mp_pulsed::duration
    if {$n_snapshots > 0 && $ratio > 0 && $T > 0} {
        set sigma_s [expr {$T / ($n_snapshots * $ratio)}]
        set ::mp_pulsed::sigma_ms [expr {$sigma_s * 1000.0}]
    }
    mp_pulsed_build_trajectory  ;# rebuild tile times
}
proc mp_pulsed_get_density {} {
    dict create n_snapshots 7 ratio 5.0
}

proc mp_pulsed_set_arc {x0 y0 vx0 vy0 g} {
    set ::mp_pulsed::arc_x0  $x0
    set ::mp_pulsed::arc_y0  $y0
    set ::mp_pulsed::arc_vx0 $vx0
    set ::mp_pulsed::arc_vy0 $vy0
    set ::mp_pulsed::arc_g   $g
    mp_pulsed_build_trajectory
}
proc mp_pulsed_get_arc {} {
    dict create x0 -5.0 y0 4.0 vx0 7.0 vy0 2.0 g -10.0
}

proc mp_pulsed_set_sweep {x0 x1 y} {
    set ::mp_pulsed::sweep_x0 $x0
    set ::mp_pulsed::sweep_x1 $x1
    set ::mp_pulsed::sweep_y  $y
    mp_pulsed_build_trajectory
}
proc mp_pulsed_get_sweep {} {
    dict create x0 -5.0 x1 5.0 y 0.0
}

proc mp_pulsed_set_duration {duration} {
    set ::mp_pulsed::duration $duration
    mp_pulsed_build_trajectory
}
proc mp_pulsed_get_duration {} { dict create duration 1.5 }

proc mp_pulsed_set_shape_size {shape_size} {
    set ::mp_pulsed::shape_size $shape_size
    motionpatch_maskscale dots_target $shape_size
    motionpatch_maskscale dots_bg     $shape_size
}
proc mp_pulsed_get_shape_size {} { dict create shape_size 0.08 }

proc mp_pulsed_set_lifetimes {target_lifetime bg_lifetime} {
    set ::mp_pulsed::target_lifetime $target_lifetime
    set ::mp_pulsed::bg_lifetime     $bg_lifetime
    catch {
        motionpatch_lifetime dots_target $target_lifetime
        motionpatch_lifetime dots_bg     $bg_lifetime
    }
}
proc mp_pulsed_get_lifetimes {} {
    dict create target_lifetime 0.5 bg_lifetime 0.05
}

# Per-region grayscale luminance. Setting surround_lum to 0 hides the
# surround entirely -- only the ball's luminance dots are visible
# against a dark background, equivalent to a "ball-only" condition.
# With both at 0.8 (default) the ball and surround are luminance-
# matched and the ball is defined by motion contrast alone.
proc mp_pulsed_set_luminance {ball_lum surround_lum} {
    set ::mp_pulsed::ball_lum     $ball_lum
    set ::mp_pulsed::surround_lum $surround_lum
    catch {
        motionpatch_color dots_target $ball_lum     $ball_lum     $ball_lum     1.0
        motionpatch_color dots_bg     $surround_lum $surround_lum $surround_lum 1.0
    }
}
proc mp_pulsed_get_luminance {} {
    dict create ball_lum 0.8 surround_lum 0.8
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup mp_pulsed_setup {
    patch_size_dva {float 8.0 32.0 1.0 13.0 "Patch Size (dva)"}
    dot_density    {float 0.5 100.0 0.5 24.0 "Dot Density (dots/dva^2)"}
} -adjusters {pulsed_actions pulsed_mode pulsed_preset pulsed_density pulsed_pulse pulsed_arc pulsed_sweep pulsed_duration pulsed_shape pulsed_lifetimes pulsed_luminance pulsed_transform} \
  -label "Motion Pulsed"

workspace::adjuster pulsed_actions {
    drop  {action "Drop / Replay (↓)"}
    reset {action "Reset (↑)"}
} -target {} -proc mp_pulsed_trigger -label "Actions"

workspace::adjuster pulsed_mode {
    mode {choice {continuous pulsed} continuous "Mode"}
} -target {} -proc mp_pulsed_set_mode -getter mp_pulsed_get_mode \
  -label "Mode (continuous vs pulsed)"

workspace::adjuster pulsed_preset {
    preset {choice {sweep arc} arc "Trajectory Preset"}
} -target {} -proc mp_pulsed_set_preset -getter mp_pulsed_get_preset \
  -label "Trajectory Preset"

# Single-knob trajectory density: change N alone to sweep from
# "sparse, easy-to-track" (low N) to "dense, brief flashes near
# continuous" (high N), with sigma auto-tracking N to hold trough
# depth constant. Use this for clean N-staircases where the *only*
# thing varying across conditions is sampling density.
workspace::adjuster pulsed_density {
    n_snapshots {int 1 25 1 7 "Number of Snapshots (N)"}
    ratio       {float 1.5 8.0 0.1 5.0 "Sigma Ratio (Δ/σ)"}
} -target {} -proc mp_pulsed_set_density -getter mp_pulsed_get_density \
  -label "Trajectory Density (auto-σ)"

# Independent (N, σ, base_coh) control for fine-tuning. Setting σ
# here overrides whatever the density adjuster computed.
workspace::adjuster pulsed_pulse {
    n_snapshots {int 1 25 1 7 "Number of Snapshots (N)"}
    sigma_ms    {float 5.0 400.0 5.0 40.0 "Pulse Width sigma (ms)"}
    base_coh    {float 0.0 1.0 0.05 1.0 "Peak Coherence"}
} -target {} -proc mp_pulsed_set_pulse -getter mp_pulsed_get_pulse \
  -label "Pulse Envelope (manual σ)"

workspace::adjuster pulsed_arc {
    x0  {float -10.0 10.0 0.5 -5.0 "Start X (dva)"}
    y0  {float -10.0 10.0 0.5  4.0 "Start Y (dva)"}
    vx0 {float -20.0 20.0 0.5  7.0 "Initial Vx (dva/sec)"}
    vy0 {float -20.0 20.0 0.5  2.0 "Initial Vy (dva/sec)"}
    g   {float -30.0 30.0 0.5 -10.0 "Gravity g (dva/sec^2)"}
} -target {} -proc mp_pulsed_set_arc -getter mp_pulsed_get_arc \
  -label "Arc Trajectory"

workspace::adjuster pulsed_sweep {
    x0 {float -10.0 10.0 0.5 -5.0 "Start X (dva)"}
    x1 {float -10.0 10.0 0.5  5.0 "End X (dva)"}
    y  {float -10.0 10.0 0.5  0.0 "Y (dva)"}
} -target {} -proc mp_pulsed_set_sweep -getter mp_pulsed_get_sweep \
  -label "Sweep Trajectory"

workspace::adjuster pulsed_duration {
    duration {float 0.2 5.0 0.1 1.5 "Duration (sec)"}
} -target {} -proc mp_pulsed_set_duration -getter mp_pulsed_get_duration \
  -label "Trial Duration"

workspace::adjuster pulsed_shape {
    shape_size {float 0.02 0.5 0.01 0.08 "Aperture Size (fraction of patch)"}
} -target {} -proc mp_pulsed_set_shape_size -getter mp_pulsed_get_shape_size \
  -label "Aperture Size"

workspace::adjuster pulsed_lifetimes {
    target_lifetime {float 0.05 2.0 0.05 0.5   "Ball Lifetime (seconds)"}
    bg_lifetime     {float 0.01 1.0 0.01 0.05 "Surround Lifetime (seconds)"}
} -target {} -proc mp_pulsed_set_lifetimes -getter mp_pulsed_get_lifetimes \
  -label "Dot Lifetimes"

workspace::adjuster pulsed_luminance {
    ball_lum     {float 0.0 1.0 0.05 0.8 "Ball Luminance"}
    surround_lum {float 0.0 1.0 0.05 0.8 "Surround Luminance"}
} -target {} -proc mp_pulsed_set_luminance -getter mp_pulsed_get_luminance \
  -label "Per-Region Luminance"

workspace::adjuster pulsed_transform -template scale -target patch \
  -label "Scene Size"
