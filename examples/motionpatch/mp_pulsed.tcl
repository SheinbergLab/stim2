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
            set ::mp_pulsed::traj_g_eff 0.0
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
            set ::mp_pulsed::traj_g_eff $g
            for {set t 0} {$t <= $T + 1e-6} {set t [expr {$t + $step}]} {
                lappend ts $t
                lappend xs [expr {$x0 + $vx0 * $t}]
                lappend ys [expr {$y0 + $vy0 * $t + 0.5 * $g * $t * $t}]
                lappend vxs $vx0
                lappend vys [expr {$vy0 + $g * $t}]
            }
        }
        freefall {
            # Pure free-fall onto a peg. The peg sits at
            # (freefall_peg_x, freefall_peg_y); the ball is released
            # from directly above with zero initial velocity. Fall
            # height is auto-derived so the ball reaches the peg at
            # exactly bounce_t (which is determined by bounce_phase /
            # pulse_index): h = 0.5 * |g| * bounce_t^2. Gravity is
            # shared with arc (arc_g). When bounce is disabled, fall
            # for the full trial.
            #
            # Per-trial position jitter (rolled by seed_bounce when
            # freefall_random_position is on) shifts the entire scene
            # uniformly: ball start, trajectory, and peg all translate
            # together. The bounce geometry (ball relative to peg) is
            # preserved, so the deflection-direction logic still works
            # correctly; only the absolute screen position varies.
            set px [expr {$::mp_pulsed::freefall_peg_x + $::mp_pulsed::freefall_jitter_x_actual}]
            set py [expr {$::mp_pulsed::freefall_peg_y + $::mp_pulsed::freefall_jitter_y_actual}]

            # Determine the desired bounce time. If a bounce phase is
            # configured we use it; otherwise pick the trial midpoint
            # so the demo still shows free-fall through a peg.
            set tb [mp_pulsed_compute_bounce_t]
            if {$tb <= 0.0} { set tb [expr {$T * 0.5}] }

            # Effective contact radius: ball CENTER must reach
            # (peg_y + r_eff) at the bounce instant so the ball's
            # bottom kisses the peg's top -- not penetrate it. r_eff
            # = ball_radius + peg_radius. ball_radius is derived from
            # the mask geometry (shape_size * patch_size / 2);
            # peg_radius is the marker's configured radius.
            set r_ball [expr {0.5 * $::mp_pulsed::shape_size * $::mp_pulsed::patch_size}]
            set r_eff  [expr {$r_ball + $::mp_pulsed::bounce_marker_radius}]

            # Fall distance and matching gravity. With fall_height = 0
            # gravity comes from arc_g and h is auto-derived from tb.
            # With fall_height > 0 the user is asking for a specific
            # vertical distance; we compute the gravity that delivers
            # the ball to the peg in tb seconds from that height.
            set h_user $::mp_pulsed::freefall_fall_height
            if {$h_user > 0.0} {
                set h_fall $h_user
                set g_abs  [expr {2.0 * $h_fall / ($tb * $tb)}]
            } else {
                set g_abs [expr {abs($::mp_pulsed::arc_g)}]
                if {$g_abs < 1e-3} { set g_abs 10.0 }
                set h_fall [expr {0.5 * $g_abs * $tb * $tb}]
            }
            set g [expr {-$g_abs}]
            # Stash the gravity used so the bounce post-processing can
            # apply the same value to the post-bounce path -- without
            # this, the ball flies off in a straight line at the post-
            # bounce speed instead of arcing back down.
            set ::mp_pulsed::traj_g_eff $g

            # Total ball-center start height above the peg = fall
            # distance + r_eff so the ball's bottom (center - r_ball)
            # arrives at peg_y + r_peg (peg top) at bounce_t.
            set h_total [expr {$h_fall + $r_eff}]

            set x0 $px
            set y0 [expr {$py + $h_total}]
            for {set t 0} {$t <= $T + 1e-6} {set t [expr {$t + $step}]} {
                lappend ts $t
                lappend xs $x0
                lappend ys [expr {$y0 + 0.5 * $g * $t * $t}]
                lappend vxs 0.0
                lappend vys [expr {$g * $t}]
            }
        }
    }

    # Tile times: N evenly-spaced centers at mid-interval positions.
    # Computed before any bounce post-processing so phase=peak/trough
    # can refer to them.
    set ::mp_pulsed::tile_times [list]
    set N $::mp_pulsed::n_snapshots
    if {$N > 0} {
        for {set k 0} {$k < $N} {incr k} {
            lappend ::mp_pulsed::tile_times \
                [expr {$T * ($k + 0.5) / $N}]
        }
    }

    # Synthetic bounce: at t = bounce_t, rotate the pre-bounce velocity
    # by ±bounce_angle_deg and scale by bounce_restitution. This is
    # the natural rigid-body deflection model: speed is preserved up
    # to a known restitution coefficient, and randomization (via
    # bounce_sign = ±1) just mirrors the deflection direction. Avoids
    # the asymmetric-acceleration artifact of an additive Δv.
    #
    # We also SNAP bounce_t to the nearest sample tick so the bounce
    # position is rendered exactly on one frame, eliminating the
    # straight-line-interpolation overshoot where the ball would
    # otherwise appear to skim past the marker between adjacent
    # frames.
    set ::mp_pulsed::bounce_t [mp_pulsed_compute_bounce_t]
    set ::mp_pulsed::bounce_x 0.0
    set ::mp_pulsed::bounce_y 0.0
    set ::mp_pulsed::bounce_marker_x 0.0
    set ::mp_pulsed::bounce_marker_y 0.0
    if {$::mp_pulsed::bounce_phase ne "none"
        && $::mp_pulsed::bounce_t > 0.0
        && $::mp_pulsed::bounce_t < $T} {
        # Snap to nearest sample. ib >= 1 keeps at least one pre-bounce
        # sample so the entry trajectory is visible.
        set ib [expr {int(round($::mp_pulsed::bounce_t / $step))}]
        if {$ib < 1} { set ib 1 }
        if {$ib >= [llength $ts]} { set ib [expr {[llength $ts] - 1}] }
        set tb [expr {$ib * $step}]
        set ::mp_pulsed::bounce_t $tb
        # CANONICAL peg position: where the un-jittered trajectory is
        # at the bounce instant. The marker is rendered here on EVERY
        # trial regardless of bounce_sign, so its location alone does
        # not predict the deflection direction.
        set peg_x [lindex $xs $ib]
        set peg_y [lindex $ys $ib]
        set ::mp_pulsed::bounce_marker_x $peg_x
        set ::mp_pulsed::bounce_marker_y $peg_y
        # Pre-bounce velocity at the snap time (unchanged by a
        # perpendicular shift, so we can read it from the smooth
        # trajectory before applying jitter).
        set pre_vx [lindex $vxs $ib]
        set pre_vy [lindex $vys $ib]
        set speed  [expr {hypot($pre_vx, $pre_vy)}]
        set in_dir [expr {($speed > 1e-9) ? atan2($pre_vy, $pre_vx) : 0.0}]
        # Perpendicular direction (90° CCW from velocity). The
        # trajectory is shifted by ±bounce_jitter * perp to make the
        # ball glance the peg from one side or the other.
        if {$speed > 1e-9} {
            set perp_x [expr {-$pre_vy / $speed}]
            set perp_y [expr {$pre_vx / $speed}]
        } else {
            set perp_x 0.0
            set perp_y 1.0
        }
        set jit $::mp_pulsed::bounce_jitter
        set off_x [expr {$perp_x * $jit * $::mp_pulsed::bounce_sign}]
        set off_y [expr {$perp_y * $jit * $::mp_pulsed::bounce_sign}]
        # Apply the perpendicular offset to all pre-bounce samples
        # AND the bounce sample. Velocities are unchanged. Note that
        # this is a uniform translation of the pre-bounce trajectory:
        # the start point and ball position at the bounce instant
        # both shift by the same amount, preserving direction.
        for {set i 0} {$i <= $ib} {incr i} {
            lset xs $i [expr {[lindex $xs $i] + $off_x}]
            lset ys $i [expr {[lindex $ys $i] + $off_y}]
        }
        # bx, by = jittered BALL CENTER at bounce_t (offset from peg
        # by ±jitter perpendicular to v_in). With jitter = r_ball +
        # r_peg the ball surface kisses the peg surface at contact.
        set bx [expr {$peg_x + $off_x}]
        set by [expr {$peg_y + $off_y}]
        set ::mp_pulsed::bounce_x $bx
        set ::mp_pulsed::bounce_y $by
        # Apply rotation (signed angle) and restitution. Sign of the
        # deflection matches sign of the perpendicular jitter -- ball
        # passes peg on one side and deflects in the same direction
        # away from the peg, as in real elastic glancing.
        set ang [expr {$::mp_pulsed::bounce_angle_deg * 3.14159265358979 / 180.0 \
                       * $::mp_pulsed::bounce_sign}]
        set out_dir   [expr {$in_dir + $ang}]
        set out_speed [expr {$speed * $::mp_pulsed::bounce_restitution}]
        set post_vx   [expr {$out_speed * cos($out_dir)}]
        set post_vy   [expr {$out_speed * sin($out_dir)}]
        # Sample ib IS the bounce point: position is (bx, by); velocity
        # is post-bounce so the next sample integrates correctly.
        lset vxs $ib $post_vx
        lset vys $ib $post_vy
        # Gravity from the trajectory builder carries through to the
        # post-bounce path: arc preset uses arc_g, freefall uses its
        # auto-derived gravity, sweep stays at 0. Without this the
        # ball would fly off at the post-bounce speed in a straight
        # line instead of arcing back down naturally.
        set g_eff $::mp_pulsed::traj_g_eff
        # Re-integrate from sample ib+1 forward, anchored at (bx, by, tb).
        for {set i [expr {$ib + 1}]} {$i < [llength $ts]} {incr i} {
            set tau [expr {[lindex $ts $i] - $tb}]
            lset xs  $i [expr {$bx + $post_vx * $tau}]
            lset ys  $i [expr {$by + $post_vy * $tau + 0.5 * $g_eff * $tau * $tau}]
            lset vxs $i $post_vx
            lset vys $i [expr {$post_vy + $g_eff * $tau}]
        }
    }

    # Freefall preset: the trajectory's bounce point is the BALL
    # CENTER at contact, which we deliberately placed r_eff above
    # the peg (so the ball's bottom touches the peg's top, not its
    # center). The marker should be drawn at the actual peg --
    # i.e. r_eff below the trajectory's bounce point. Anchor it to
    # the user's configured peg position (with per-trial jitter
    # applied) so the marker doesn't drift if other knobs change.
    if {$::mp_pulsed::preset eq "freefall"} {
        set ::mp_pulsed::bounce_marker_x \
            [expr {$::mp_pulsed::freefall_peg_x + $::mp_pulsed::freefall_jitter_x_actual}]
        set ::mp_pulsed::bounce_marker_y \
            [expr {$::mp_pulsed::freefall_peg_y + $::mp_pulsed::freefall_jitter_y_actual}]
    }

    set ::mp_pulsed::traj_t  $ts
    set ::mp_pulsed::traj_x  $xs
    set ::mp_pulsed::traj_y  $ys
    set ::mp_pulsed::traj_vx $vxs
    set ::mp_pulsed::traj_vy $vys
    set ::mp_pulsed::traj_dt $step
    set ::mp_pulsed::traj_n  [llength $ts]
}

# Reroll the per-trial random elements: deflection sign (if
# bounce_random_sign is on) and freefall scene position (if
# freefall_random_position is on). Called once per trial at
# reset/record; adjuster changes between trials don't reroll.
proc mp_pulsed_seed_bounce {} {
    if {$::mp_pulsed::bounce_random_sign} {
        set ::mp_pulsed::bounce_sign [expr {(rand() < 0.5) ? -1 : 1}]
    } else {
        set ::mp_pulsed::bounce_sign 1
    }
    if {$::mp_pulsed::freefall_random_position} {
        set rx $::mp_pulsed::freefall_jitter_x_range
        set ry $::mp_pulsed::freefall_jitter_y_range
        set ::mp_pulsed::freefall_jitter_x_actual \
            [expr {(rand() * 2.0 - 1.0) * $rx}]
        set ::mp_pulsed::freefall_jitter_y_actual \
            [expr {(rand() * 2.0 - 1.0) * $ry}]
    } else {
        set ::mp_pulsed::freefall_jitter_x_actual 0.0
        set ::mp_pulsed::freefall_jitter_y_actual 0.0
    }
}

# Resolve bounce_phase to an absolute time. "peak" aligns the bounce
# to the center of pulse `bounce_pulse_index`; "trough" puts it
# halfway between that pulse and the next one. "custom" returns the
# user-supplied value directly. "none" returns 0 (treated as disabled).
# bounce_pulse_index is clamped to [0, N-1] for peak and [0, N-2] for
# trough to keep both endpoints inside the trial.
proc mp_pulsed_compute_bounce_t {} {
    set phase $::mp_pulsed::bounce_phase
    if {$phase eq "none"} { return 0.0 }
    if {$phase eq "custom"} { return $::mp_pulsed::bounce_t_custom }
    set N    $::mp_pulsed::n_snapshots
    set tts  $::mp_pulsed::tile_times
    if {$N <= 0 || [llength $tts] == 0} { return 0.0 }
    set k $::mp_pulsed::bounce_pulse_index
    if {$k < 0} { set k 0 }
    if {$phase eq "peak"} {
        if {$k >= $N} { set k [expr {$N - 1}] }
        return [lindex $tts $k]
    }
    # trough: midpoint between adjacent pulses
    if {$k > [expr {$N - 2}]} { set k [expr {$N - 2}] }
    if {$k < 0} { return 0.0 }
    set t_a [lindex $tts $k]
    set t_b [lindex $tts [expr {$k + 1}]]
    return [expr {0.5 * ($t_a + $t_b)}]
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
#
# double($v) / double($ps) forces float arithmetic. The stim2 slider
# normalizes integer-valued positions like 4.0 to "4" (Tcl integer
# rep), and Tcl's expr does integer division on int/int -- which
# silently returns 0 for any 0 < v < ps. Forcing both operands to
# double avoids that classic Tcl gotcha.
proc mp_pulsed_speed_from_deg_sec {v} {
    set ps $::mp_pulsed::patch_size
    if {$ps <= 0} { return 0.0 }
    return [expr {double($v) / double($ps)}]
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
        set surround_speed \
            [mp_pulsed_speed_from_deg_sec $::mp_pulsed::surround_speed_dva_sec]
        motionpatch_coherence dots_target 0.0
        motionpatch_speed     dots_target $surround_speed
        motionpatch_lifetime  dots_target $::mp_pulsed::bg_lifetime
        # If this trial was started via the Record action, finalize
        # capture by stopping any still-active logs and exporting both
        # patches' recorded frames to dgs alongside the design dg
        # (which was built at record-start to capture the parameters
        # that were in effect when the trial began). All three dgs
        # persist in dgTable; user can dg_write them as needed.
        if {$::mp_pulsed::recording} {
            set ::mp_pulsed::recording 0
            catch {motionpatch_logEnd dots_target}
            catch {motionpatch_logEnd dots_bg}
            catch {motionpatch_logExport dots_target \
                       $::mp_pulsed::record_target_name}
            catch {motionpatch_logExport dots_bg \
                       $::mp_pulsed::record_bg_name}
        }
        return
    }
    set i [mp_pulsed_index_for_time $tplay]

    set x  [lindex $::mp_pulsed::traj_x  $i]
    set y  [lindex $::mp_pulsed::traj_y  $i]
    set vx [lindex $::mp_pulsed::traj_vx $i]
    set vy [lindex $::mp_pulsed::traj_vy $i]

    set ps $::mp_pulsed::patch_size
    # double() forces float arithmetic; see comment in
    # mp_pulsed_speed_from_deg_sec for why.
    set ox [expr {double($x) / double($ps)}]
    set oy [expr {double($y) / double($ps)}]
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

    # Pulsed mode gates motion energy via the Gaussian envelope. To
    # make the trough state genuinely indistinguishable from the
    # surround (i.e. a true motion-only manipulation), we interpolate
    # ALL three statistics that differ between ball and surround --
    # coherence, dot speed, and dot lifetime -- with the envelope.
    # At pulse peaks: ball runs at trajectory speed with long-lived
    # coherent dots. At pulse troughs: inside dots match surround
    # speed and surround lifetime exactly, so coherence==0 leaves no
    # residual signature distinguishing inside from outside. With
    # ball_lum == surround_lum (default), the ball is then truly
    # invisible between pulses; with ball_lum > surround_lum, only
    # luminance contrast persists between pulses.
    set surround_speed_pu \
        [mp_pulsed_speed_from_deg_sec $::mp_pulsed::surround_speed_dva_sec]
    set base $::mp_pulsed::base_coh
    if {$::mp_pulsed::mode eq "pulsed" && $base > 0.0} {
        set coh  [mp_pulsed_envelope $tplay]
        set frac [expr {$coh / $base}]
    } else {
        set coh  $base
        set frac 1.0
    }
    set sp_eff   [expr {$frac * $sp + (1.0 - $frac) * $surround_speed_pu}]
    set life_eff [expr {$frac * $::mp_pulsed::target_lifetime + \
                        (1.0 - $frac) * $::mp_pulsed::bg_lifetime}]

    motionpatch_speed     dots_target $sp_eff
    motionpatch_lifetime  dots_target $life_eff
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

        # Freefall preset: ball drops straight down onto a peg. Fall
        # height is auto-derived from the bounce time so the collision
        # naturally lands at the chosen pulse phase. With a tall fall,
        # the ball acquires enough speed that any horizontal jitter
        # looks small relative to the trajectory length, mitigating
        # the start-position cue. Gravity is shared with the arc
        # preset (arc_g, typically negative). Pulse alignment becomes
        # automatic: pick bounce_phase, freefall recomputes start_y.
        variable freefall_peg_x   0.0
        variable freefall_peg_y  -2.0
        # Fall-distance override. When 0, the fall distance is auto-
        # derived from gravity (arc_g) and the bounce time so pulse
        # alignment is exact. When > 0, the user is specifying the
        # fall distance directly and gravity is computed to match
        # bounce_t -- a longer fall therefore implies stronger gravity
        # and a faster ball at impact, but pulse alignment is
        # preserved. Useful when you want more vertical distance
        # between the start and the peg for visual clarity without
        # losing the peak/trough timing.
        variable freefall_fall_height 0.0
        # Per-trial position jitter: when enabled, each reset/record
        # rolls an absolute (dx, dy) translation within the configured
        # half-ranges and applies it uniformly to BOTH the start point
        # AND the peg, preserving the bounce-relative geometry. Result:
        # absolute positions vary trial-to-trial so subjects can't use
        # learned coordinates to anticipate the bounce, but the
        # ball-vs-peg relationship (which determines deflection) is
        # unchanged. Combined with bounce_jitter (small) this creates
        # the plinko-like setup where each trial looks slightly
        # different but the deflection direction can only be inferred
        # by actually observing the ball relative to the peg.
        variable freefall_random_position    0
        variable freefall_jitter_x_range     2.0
        variable freefall_jitter_y_range     0.5
        variable freefall_jitter_x_actual    0.0
        variable freefall_jitter_y_actual    0.0

        variable duration         1.5
        variable patch_size      13.0
        variable max_speed_deg_sec 30.0
        variable shape_size       0.08
        variable bg_lifetime      0.08   ;# seconds; mean Poisson lifetime
        variable target_lifetime  0.5    ;# seconds (peak-pulse value)
        # Surround dot speed in dva/sec. Also the trough target for
        # dots_target speed in pulsed mode -- keeping both in sync is
        # what makes the trough state statistically identical to the
        # surround flicker. Lower values = quieter, less distracting
        # background.
        variable surround_speed_dva_sec 3.0
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
        # Synthetic bounce: an instantaneous velocity change inserted
        # into the smooth trajectory at a chosen time, used to test
        # whether motion-energy at the moment of direction change is
        # necessary for trajectory updating. Phase is "none" (off),
        # "peak" (align to a pulse center), "trough" (midpoint between
        # adjacent pulses), or "custom" (use bounce_t_custom directly).
        # bounce_pulse_index selects which pulse to align to in
        # peak/trough modes (0-based, clamped to [0, N-1]).
        variable bounce_phase       none
        variable bounce_pulse_index 3
        variable bounce_t_custom    0.5
        # Real-collision parameterization: rotate the pre-bounce
        # velocity vector by ±bounce_angle_deg, then scale the magnitude
        # by bounce_restitution. Speed is preserved (or attenuated)
        # regardless of sign roll, so randomization can't covertly
        # accelerate or decelerate the ball -- both outcomes are
        # mirror-image deflections around the incoming trajectory.
        # 0 deg = no deflection; 90 deg = right-angle; 180 deg = reverse.
        variable bounce_angle_deg   60.0
        variable bounce_restitution 1.0
        # When 1, mp_pulsed_seed_bounce flips the sign of the
        # deflection angle with 50/50 probability before each trial:
        # +angle deflects one way, -angle the other. Magnitude and
        # restitution are preserved, so the two outcomes are mirror
        # images of each other (e.g. ball off the edge of a peg --
        # could go left or right with equal probability, same speed).
        # The roll happens at reset / record (once per trial), not on
        # every adjuster change.
        variable bounce_random_sign 0
        variable bounce_sign        1
        variable bounce_t           0.0   ;# computed by build_trajectory
        variable bounce_x           0.0   ;# ball center at bounce instant
        variable bounce_y           0.0   ;# ball center at bounce instant
        # The visual marker represents the PEG that the ball collides
        # with, and a real rigid-body contact puts the peg's center
        # offset from the ball's center by (ball_radius + peg_radius)
        # along the outward contact normal. We compute the offset
        # marker position so the rendered peg is tangent to the ball
        # at the bounce instant rather than overlapping its center.
        variable bounce_marker_x    0.0
        variable bounce_marker_y    0.0
        variable bounce_marker_radius 0.18  ;# dva
        # Trajectory jitter perpendicular to the incoming direction.
        # The peg (marker) sits at the canonical un-jittered bounce
        # point, and bounce_jitter controls how far off the ball's
        # actual pre-bounce trajectory is shifted from that point:
        #   0          -- ball drops directly onto peg center; the
        #                 ball mask overlaps the peg at contact and
        #                 the deflection direction has *zero* pre-
        #                 bounce visual cue. Maximum experimental
        #                 uncertainty: subject can only learn the
        #                 outcome by observing the post-bounce
        #                 trajectory. This is the canonical setting
        #                 for the motion-energy-necessity manipulation.
        #   small      -- ball passes slightly to one side of the peg;
        #                 a careful observer might detect the side
        #                 from the trajectory's sub-pixel offset, but
        #                 motion-energy gating in trough conditions
        #                 should disrupt that channel.
        #   ~r_eff     -- ball's surface kisses the peg's surface;
        #                 visually clean glancing collision but the
        #                 side is obvious to any sighted observer.
        variable bounce_jitter      0.0   ;# dva
        # Marker mode: "hidden" omits the spatial cue entirely;
        # "persistent" renders a small dot at (bounce_x, bounce_y)
        # throughout the trial. The persistent condition tests whether
        # advance knowledge of *where* the bounce will occur is enough
        # to update trajectory predictions in the absence of motion-
        # energy at the bounce instant. The expected null result --
        # marker doesn't rescue trough-bounce predictions -- is the
        # argument that motion-energy is necessary, not optional.
        variable bounce_marker_mode hidden
        variable bounce_marker_id   ""

        # Recording state: when nonzero, mp_pulsed_update will trigger
        # a one-shot export of the captured logs at trial end. Names
        # are namespace vars so the user can override before pressing
        # Record (e.g. for batched naming) or rename the dgs after the
        # fact via dg_rename. The three dgs persist in stim2's dgTable
        # until explicitly written or deleted.
        variable recording          0
        variable record_design_name mp_pulsed_design
        variable record_target_name mp_pulsed_log_target
        variable record_bg_name     mp_pulsed_log_bg
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
        # Effective gravity used by the current trajectory. Set per-
        # preset (sweep -> 0, arc -> arc_g, freefall -> auto-derived
        # gravity that delivers the desired fall_height in bounce_t).
        # The bounce post-processing reads this to apply the same
        # gravity to the post-bounce path so the ball curves back
        # naturally instead of flying off in a straight line.
        variable traj_g_eff 0.0

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

    set initialSpeed \
        [mp_pulsed_speed_from_deg_sec $::mp_pulsed::surround_speed_dva_sec]
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

    # Bounce marker: a small magenta peg rendered tangent to the ball
    # at the bounce instant (the offset is computed in build_trajectory
    # so the peg is geometrically next to the ball, not concentric).
    # Visibility is gated by bounce_marker_mode; always created so the
    # adjuster can flip visibility live without rebuilding the scene.
    set mk_r $::mp_pulsed::bounce_marker_radius
    set mk [polygon]
    polycirc $mk 1
    polycolor $mk 0.9 0.2 0.7
    scaleObj $mk [expr {2.0 * $mk_r}]
    set ::mp_pulsed::bounce_marker_id $mk
    glistAddObject $mk 0
    mp_pulsed_apply_marker

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

# ---------- design-summary dynamic group ----------
#
# Build a dynamic group describing what the stimulus is doing to
# coherence, dot speed, and dot lifetime under both the continuous
# and pulsed modes, alongside the underlying smooth trajectory.
#
# Returns the group name. Persist with `dg_write $g path.dgz`; load
# from Python via dgread.dgread() or from JS via the native dgz
# reader. The group bundles per-sample columns, the pulse-center
# vector, and trial-level scalar metadata so the off-line consumer
# has everything needed to reproduce the methods figure without
# referring back to the live workspace.
#
# Per-sample columns (all length n_samples, aligned to t):
#   t                  : time grid (sec)
#   x, y               : mask center position (dva) -- the carrier path
#   vmag               : trajectory speed |v(t)| (dva/sec, max-clamped)
#   coh_pulsed         : envelope-gated coherence(t)
#   coh_continuous     : flat baseline coherence (always = base_coh)
#   speed_pulsed       : effective inside-dot speed under pulsed mode
#                        (interpolates trajectory <-> surround)
#   speed_continuous   : same under continuous mode (always trajectory)
#   life_pulsed        : effective inside-dot lifetime under pulsed mode
#   life_continuous    : same under continuous mode (always target_lifetime)
#   frac               : envelope/base in [0, 1] -- the interpolation knob
#   path_length        : cumulative arc length traveled by mask up to t
#                        (dva). Useful as an alt x-axis: "distance into
#                        trajectory" instead of "time", which can make
#                        comparisons across speeds / presets cleaner.
#   cum_energy_pulsed  : cumulative integral of coh_pulsed dt (sec).
#                        Total motion-energy time delivered. Compare to
#                        cum_energy_continuous to quantify the energy
#                        deficit of the pulsed manipulation.
#   cum_energy_continuous : same for continuous (== base_coh * t).
#
# Group-level scalars (single-element columns, queryable by readers):
#   tile_times         : pulse centers (sec) -- vertical-line markers
#   T                  : trial duration (sec)
#   surround_speed_dva : surround dot speed (dva/sec); trough target
#   target_lifetime    : peak inside-dot lifetime (sec)
#   bg_lifetime        : trough inside-dot lifetime (sec)
#   sigma_s            : Gaussian pulse width (sec)
#   base_coh           : peak coherence
#   n_snapshots        : pulse count N
#   patch_size_dva     : patch span (dva)
#   shape_size         : aperture diameter as fraction of patch
#   max_speed_deg_sec  : trajectory speed clamp (dva/sec)
#   preset             : "sweep" or "arc"
#   energy_ratio       : cum_energy_pulsed(T) / cum_energy_continuous(T).
#                        Fraction of continuous motion-energy delivered
#                        by the pulse train; one-number summary of
#                        "how much manipulation".
proc mp_pulsed_design_dg {{n_samples 600} {gname mp_pulsed_design}} {
    mp_pulsed_build_trajectory

    set T [expr {$::mp_pulsed::traj_dt * ($::mp_pulsed::traj_n - 1)}]
    if {$T <= 0.0 || $n_samples < 2} { return "" }

    set sigma_s  [expr {$::mp_pulsed::sigma_ms / 1000.0}]
    set base     $::mp_pulsed::base_coh
    set vmax     $::mp_pulsed::max_speed_deg_sec
    set life_t   $::mp_pulsed::target_lifetime
    set life_b   $::mp_pulsed::bg_lifetime
    set surr_dva $::mp_pulsed::surround_speed_dva_sec
    set traj_dt  $::mp_pulsed::traj_dt
    set traj_n   $::mp_pulsed::traj_n

    set ts {} ; set xs {} ; set ys {}
    set vmag_list {} ; set frac_list {}
    set coh_p_list {} ; set coh_c_list {}
    set sp_p_list {}  ; set sp_c_list {}
    set life_p_list {} ; set life_c_list {}
    set path_list {}
    set cumEp_list {} ; set cumEc_list {}

    set step      [expr {$T / double($n_samples - 1)}]
    set path_acc  0.0
    set cumEp_acc 0.0
    set cumEc_acc 0.0
    set prev_x    0.0
    set prev_y    0.0
    set prev_ep   0.0

    for {set k 0} {$k < $n_samples} {incr k} {
        set t [expr {$k * $step}]
        if {$t > $T} { set t $T }

        set i [expr {int($t / $traj_dt)}]
        if {$i >= $traj_n} { set i [expr {$traj_n - 1}] }

        set x  [lindex $::mp_pulsed::traj_x  $i]
        set y  [lindex $::mp_pulsed::traj_y  $i]
        set vx [lindex $::mp_pulsed::traj_vx $i]
        set vy [lindex $::mp_pulsed::traj_vy $i]
        set vm [expr {hypot($vx, $vy)}]
        if {$vm > $vmax} { set vm $vmax }

        # Sum-of-Gaussians envelope. Mirrors mp_pulsed_envelope.
        set sum 0.0
        if {$sigma_s > 0.0} {
            foreach ti $::mp_pulsed::tile_times {
                set z [expr {($t - $ti) / $sigma_s}]
                set sum [expr {$sum + exp(-0.5 * $z * $z)}]
            }
            set ep [expr {$base * $sum}]
            if {$ep > $base} { set ep $base }
            if {$ep < 0.0}   { set ep 0.0 }
        } else {
            set ep $base
        }

        set frac 0.0
        if {$base > 0.0} { set frac [expr {$ep / $base}] }

        # Effective dot statistics under pulsed mode -- mirrors the
        # interpolation in mp_pulsed_update.
        set sp_eff   [expr {$frac * $vm + (1.0 - $frac) * $surr_dva}]
        set life_eff [expr {$frac * $life_t + (1.0 - $frac) * $life_b}]

        # Cumulative integrals via trapezoidal rule. path_length over
        # the mask trajectory (using actual sample-to-sample distance,
        # robust to non-uniform parameterizations); motion-energy as
        # integral of coherence dt for both modes.
        if {$k == 0} {
            set path_acc 0.0
        } else {
            set dx [expr {$x - $prev_x}]
            set dy [expr {$y - $prev_y}]
            set path_acc [expr {$path_acc + hypot($dx, $dy)}]
            set cumEp_acc \
                [expr {$cumEp_acc + 0.5 * ($prev_ep + $ep) * $step}]
            set cumEc_acc \
                [expr {$cumEc_acc + $base * $step}]
        }

        lappend ts          $t
        lappend xs          $x
        lappend ys          $y
        lappend vmag_list   $vm
        lappend coh_p_list  $ep
        lappend coh_c_list  $base
        lappend sp_p_list   $sp_eff
        lappend sp_c_list   $vm
        lappend life_p_list $life_eff
        lappend life_c_list $life_t
        lappend frac_list   $frac
        lappend path_list   $path_acc
        lappend cumEp_list  $cumEp_acc
        lappend cumEc_list  $cumEc_acc

        set prev_x  $x
        set prev_y  $y
        set prev_ep $ep
    }

    set energy_ratio 0.0
    if {$cumEc_acc > 0.0} {
        set energy_ratio [expr {$cumEp_acc / $cumEc_acc}]
    }

    # Replace any prior group of the same name so re-runs don't leak.
    catch {dg_delete $gname}
    set g [dg_create $gname]

    foreach {col src} [list \
            t                  $ts          \
            x                  $xs          \
            y                  $ys          \
            vmag               $vmag_list   \
            coh_pulsed         $coh_p_list  \
            coh_continuous     $coh_c_list  \
            speed_pulsed       $sp_p_list   \
            speed_continuous   $sp_c_list   \
            life_pulsed        $life_p_list \
            life_continuous    $life_c_list \
            frac               $frac_list   \
            path_length        $path_list   \
            cum_energy_pulsed  $cumEp_list  \
            cum_energy_continuous $cumEc_list] {
        dl_set $g:$col [dl_flist {*}$src]
    }

    if {[llength $::mp_pulsed::tile_times] > 0} {
        dl_set $g:tile_times [dl_flist {*}$::mp_pulsed::tile_times]
    } else {
        dl_set $g:tile_times [dl_flist]
    }

    # Trial-level scalars stored as 1-element columns so dgread sees
    # them as plain scalars on the row-side.
    foreach {col val} [list \
            T                    $T                                  \
            surround_speed_dva   $surr_dva                           \
            target_lifetime      $life_t                             \
            bg_lifetime          $life_b                             \
            sigma_s              $sigma_s                            \
            base_coh             $base                               \
            n_snapshots          $::mp_pulsed::n_snapshots           \
            patch_size_dva       $::mp_pulsed::patch_size            \
            shape_size           $::mp_pulsed::shape_size            \
            max_speed_deg_sec    $vmax                               \
            energy_ratio         $energy_ratio                       \
            bounce_t             $::mp_pulsed::bounce_t              \
            bounce_x             $::mp_pulsed::bounce_x              \
            bounce_y             $::mp_pulsed::bounce_y              \
            bounce_marker_x      $::mp_pulsed::bounce_marker_x       \
            bounce_marker_y      $::mp_pulsed::bounce_marker_y       \
            bounce_marker_radius $::mp_pulsed::bounce_marker_radius  \
            bounce_jitter        $::mp_pulsed::bounce_jitter         \
            freefall_jitter_x    $::mp_pulsed::freefall_jitter_x_actual \
            freefall_jitter_y    $::mp_pulsed::freefall_jitter_y_actual \
            freefall_random_position $::mp_pulsed::freefall_random_position \
            freefall_fall_height $::mp_pulsed::freefall_fall_height \
            freefall_peg_x_user  $::mp_pulsed::freefall_peg_x       \
            freefall_peg_y_user  $::mp_pulsed::freefall_peg_y       \
            bounce_angle_deg     [expr {$::mp_pulsed::bounce_angle_deg * $::mp_pulsed::bounce_sign}] \
            bounce_restitution   $::mp_pulsed::bounce_restitution    \
            bounce_random_sign   $::mp_pulsed::bounce_random_sign    \
            bounce_sign          $::mp_pulsed::bounce_sign] {
        dl_set $g:$col [dl_flist $val]
    }
    dl_set $g:preset             [dl_slist $::mp_pulsed::preset]
    dl_set $g:bounce_phase       [dl_slist $::mp_pulsed::bounce_phase]
    dl_set $g:bounce_marker_mode [dl_slist $::mp_pulsed::bounce_marker_mode]

    return $g
}

# ---------- actions ----------

proc mp_pulsed_trigger {action} {
    switch -- $action {
        drop {
            set ::mp_pulsed::play_t   0.0
            set ::mp_pulsed::dropping 1
        }
        record {
            # Reroll the bounce sign FIRST (if randomization is on)
            # so the design dg captures the actual signed Δv for this
            # trial. Then rebuild the trajectory and apply the marker
            # before logging starts.
            mp_pulsed_seed_bounce
            mp_pulsed_build_trajectory
            mp_pulsed_apply_marker
            # Build the forward design dg, capturing the *current*
            # parameter set as a single source of truth. The recorded
            # logs (added at trial end) will be compared against this.
            mp_pulsed_design_dg 600 $::mp_pulsed::record_design_name

            # Size the dot logs generously. Sized at 120 Hz + slack so
            # the buffer covers the trial even on faster displays
            # (Mac ProMotion etc.) where 60-Hz sizing would clip the
            # tail. Trial duration + 0.5 s slack covers pre-drop
            # capture frames and the post-trial collapse frame.
            set max_frames \
                [expr {int(ceil(($::mp_pulsed::duration + 0.5) * 120.0)) + 60}]
            if {$max_frames < 120} { set max_frames 120 }
            catch {motionpatch_logBegin dots_target $max_frames}
            catch {motionpatch_logBegin dots_bg     $max_frames}
            set ::mp_pulsed::recording 1
            mp_pulsed_trigger drop
        }
        write_dgs {
            # Convenience: rename the three recorded dgs with a shared
            # timestamp suffix and write them to /tmp under those new
            # names. Renaming the dg in-memory means the on-disk file
            # AND the embedded group name both carry the timestamp,
            # so a downstream reader (dgread, etc.) sees a uniquely
            # identifiable group rather than a generic name. Silently
            # skip any dg that doesn't exist yet.
            #
            # After write, the in-memory dgs carry the timestamped
            # names; a fresh Record creates new dgs with the base
            # names again, so old recordings are preserved alongside.
            # Prefer human-readable timestamp; fall back to the
            # raw Unix-epoch integer if `clock format` fails (it
            # requires msgcat, which may be missing from minimal
            # Tcl builds). The integer fallback is still uniquely
            # timestamped and sorts correctly by filename.
            if {[catch {clock format [clock seconds] \
                            -format %Y%m%d_%H%M%S} ts]} {
                set ts [clock seconds]
            }
            set written {}
            foreach base [list \
                    $::mp_pulsed::record_design_name \
                    $::mp_pulsed::record_target_name \
                    $::mp_pulsed::record_bg_name] {
                if {[catch {
                    set newname ${base}_${ts}
                    dg_rename $base $newname
                    dg_write  $newname /tmp/${newname}.dgz
                    lappend written /tmp/${newname}.dgz
                } err]} {
                    puts stderr "write_dgs: skipping $base ($err)"
                }
            }
            return $written
        }
        reset {
            # Reroll the bounce sign (if random_sign is on) so each
            # reset gives a fresh, unpredictable post-bounce direction.
            # Then rebuild and re-place the marker accordingly.
            mp_pulsed_seed_bounce
            mp_pulsed_build_trajectory
            mp_pulsed_apply_marker
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

# Freefall preset: peg position + per-trial position-jitter ranges.
# Start position is auto-derived from gravity (arc_g) and bounce_t.
# When random_position is on, each reset/record rolls a random (dx,
# dy) translation within the configured half-ranges and applies it
# to the whole scene (start + trajectory + peg) so absolute screen
# positions vary trial-to-trial without disturbing the relative
# bounce geometry.
proc mp_pulsed_set_freefall {peg_x peg_y fall_height jitter_x_range jitter_y_range random_position} {
    set ::mp_pulsed::freefall_peg_x            $peg_x
    set ::mp_pulsed::freefall_peg_y            $peg_y
    set ::mp_pulsed::freefall_fall_height      $fall_height
    set ::mp_pulsed::freefall_jitter_x_range   $jitter_x_range
    set ::mp_pulsed::freefall_jitter_y_range   $jitter_y_range
    set ::mp_pulsed::freefall_random_position  $random_position
    if {!$random_position} {
        set ::mp_pulsed::freefall_jitter_x_actual 0.0
        set ::mp_pulsed::freefall_jitter_y_actual 0.0
    }
    mp_pulsed_build_trajectory
    mp_pulsed_apply_marker
}
proc mp_pulsed_get_freefall {} {
    dict create peg_x 0.0 peg_y -2.0 fall_height 0.0 \
        jitter_x_range 2.0 jitter_y_range 0.5 random_position 0
}

proc mp_pulsed_set_duration {duration} {
    set ::mp_pulsed::duration $duration
    mp_pulsed_build_trajectory
}
proc mp_pulsed_get_duration {} { dict create duration 1.5 }

# Synthetic bounce: phase ∈ {none, peak, trough, custom}; pulse_index
# selects which pulse to align to in peak/trough modes; t_custom is
# only used when phase=custom; (dvx, dvy) is the velocity change in
# dva/sec applied at the bounce instant. Set phase=none to disable.
proc mp_pulsed_set_bounce {phase pulse_index t_custom angle_deg restitution jitter marker_mode random_sign} {
    set ::mp_pulsed::bounce_phase       $phase
    set ::mp_pulsed::bounce_pulse_index $pulse_index
    set ::mp_pulsed::bounce_t_custom    $t_custom
    set ::mp_pulsed::bounce_angle_deg   $angle_deg
    set ::mp_pulsed::bounce_restitution $restitution
    set ::mp_pulsed::bounce_jitter      $jitter
    set ::mp_pulsed::bounce_marker_mode $marker_mode
    set ::mp_pulsed::bounce_random_sign $random_sign
    # When random_sign is off, snap the sign back to +1 so the
    # configured angle takes effect verbatim. Turning random_sign on
    # doesn't reroll yet -- that happens at the next reset/record.
    if {!$random_sign} { set ::mp_pulsed::bounce_sign 1 }
    mp_pulsed_build_trajectory
    mp_pulsed_apply_marker
}
proc mp_pulsed_get_bounce {} {
    dict create phase none pulse_index 3 t_custom 0.5 \
        angle_deg 60.0 restitution 1.0 jitter 0.0 \
        marker_mode hidden random_sign 0
}

# Update the bounce marker's position from the current bounce_x,y and
# its visibility from bounce_marker_mode. Called whenever the bounce
# is reconfigured. Safe to call before the marker exists (silently
# noops); idempotent. translateObj sets an absolute position (no
# accumulation), so repeated calls just overwrite the previous one.
# With marker_mode=persistent the dot is always on; with
# marker_mode=hidden it's off.
proc mp_pulsed_apply_marker {} {
    set mk $::mp_pulsed::bounce_marker_id
    if {$mk eq ""} { return }
    # Use the geometry-corrected peg position so the rendered marker
    # is tangent to the ball at the contact instant, not concentric
    # with the ball's center.
    set bx $::mp_pulsed::bounce_marker_x
    set by $::mp_pulsed::bounce_marker_y
    catch {translateObj $mk $bx $by 0.0}
    set show 0
    if {$::mp_pulsed::bounce_phase ne "none"
        && $::mp_pulsed::bounce_marker_mode eq "persistent"} {
        set show 1
    }
    catch {setVisible $mk $show}
}

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
    dict create target_lifetime 0.5 bg_lifetime 0.08
}

# Surround dot speed (dva/sec). Also sets the trough target for inside
# dots, so the trough-state interpolation in the per-frame driver lands
# on a state that exactly matches the surround. Lower = quieter
# background; higher = more energetic flicker. Independent of
# trajectory speed.
proc mp_pulsed_set_surround {surround_speed_dva_sec} {
    set ::mp_pulsed::surround_speed_dva_sec $surround_speed_dva_sec
    catch {
        motionpatch_speed dots_bg \
            [mp_pulsed_speed_from_deg_sec $surround_speed_dva_sec]
    }
}
proc mp_pulsed_get_surround {} {
    dict create surround_speed_dva_sec 3.0
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
} -adjusters {pulsed_actions pulsed_mode pulsed_preset pulsed_density pulsed_pulse pulsed_arc pulsed_sweep pulsed_freefall pulsed_duration pulsed_bounce pulsed_shape pulsed_lifetimes pulsed_surround pulsed_luminance pulsed_transform} \
  -label "Motion Pulsed"

workspace::adjuster pulsed_actions {
    drop      {action "Drop / Replay (↓)"}
    reset     {action "Reset (↑)"}
    record    {action "Record (design + per-frame log)"}
    write_dgs {action "Write dgs to /tmp"}
} -target {} -proc mp_pulsed_trigger -label "Actions"

workspace::adjuster pulsed_mode {
    mode {choice {continuous pulsed} continuous "Mode"}
} -target {} -proc mp_pulsed_set_mode -getter mp_pulsed_get_mode \
  -label "Mode (continuous vs pulsed)"

workspace::adjuster pulsed_preset {
    preset {choice {sweep arc freefall} arc "Trajectory Preset"}
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

workspace::adjuster pulsed_freefall {
    peg_x          {float -10.0 10.0 0.5  0.0 "Peg X (dva)"}
    peg_y          {float -10.0 10.0 0.5 -2.0 "Peg Y (dva)"}
    fall_height    {float   0.0 15.0 0.5  0.0 "Fall Height (dva, 0=auto)"}
    jitter_x_range {float   0.0  6.0 0.25 2.0 "Position Jitter X (±dva)"}
    jitter_y_range {float   0.0  3.0 0.25 0.5 "Position Jitter Y (±dva)"}
    random_position {bool   0     "Randomize Position Per Trial"}
} -target {} -proc mp_pulsed_set_freefall -getter mp_pulsed_get_freefall \
  -label "Freefall (auto-aligned)"

workspace::adjuster pulsed_duration {
    duration {float 0.2 5.0 0.1 1.5 "Duration (sec)"}
} -target {} -proc mp_pulsed_set_duration -getter mp_pulsed_get_duration \
  -label "Trial Duration"

# Synthetic bounce: tests whether motion-energy at the bounce instant
# is necessary for trajectory updating. phase=peak aligns the velocity
# discontinuity to a pulse center (motion-energy available); phase=
# trough aligns it to the midpoint between pulses (no motion-energy).
# Compare the two for the methods-figure manipulation. phase=none
# disables. Predicted dissociation: at peak, post-bounce predictions
# track the new trajectory; at trough, predictions extrapolate the
# old direction until the next pulse arrives.
workspace::adjuster pulsed_bounce {
    phase       {choice {none peak trough custom} none "Bounce Phase"}
    pulse_index {int 0 24 1 3 "Pulse Index (peak/trough)"}
    t_custom    {float 0.0 5.0 0.05 0.5 "Custom Bounce Time (sec)"}
    angle_deg   {float 0.0 180.0 5.0 60.0 "Deflection Angle (deg)"}
    restitution {float 0.0 1.5 0.05 1.0 "Restitution (speed scale)"}
    jitter      {float 0.0 2.0 0.05 0.0  "Trajectory Jitter (dva)"}
    marker_mode {choice {hidden persistent} hidden "Bounce Marker"}
    random_sign {bool 0 "Randomize deflection sign per trial"}
} -target {} -proc mp_pulsed_set_bounce -getter mp_pulsed_get_bounce \
  -label "Synthetic Bounce"

workspace::adjuster pulsed_shape {
    shape_size {float 0.02 0.5 0.01 0.08 "Aperture Size (fraction of patch)"}
} -target {} -proc mp_pulsed_set_shape_size -getter mp_pulsed_get_shape_size \
  -label "Aperture Size"

workspace::adjuster pulsed_lifetimes {
    target_lifetime {float 0.05 2.0 0.05 0.5   "Ball Lifetime (seconds)"}
    bg_lifetime     {float 0.01 1.0 0.01 0.08 "Surround Lifetime (seconds)"}
} -target {} -proc mp_pulsed_set_lifetimes -getter mp_pulsed_get_lifetimes \
  -label "Dot Lifetimes"

workspace::adjuster pulsed_surround {
    surround_speed_dva_sec {float 0.0 15.0 0.5 3.0 "Surround Speed (dva/sec)"}
} -target {} -proc mp_pulsed_set_surround -getter mp_pulsed_get_surround \
  -label "Surround Flicker Speed"

workspace::adjuster pulsed_luminance {
    ball_lum     {float 0.0 1.0 0.05 0.8 "Ball Luminance"}
    surround_lum {float 0.0 1.0 0.05 0.8 "Surround Luminance"}
} -target {} -proc mp_pulsed_set_luminance -getter mp_pulsed_get_luminance \
  -label "Per-Region Luminance"

workspace::adjuster pulsed_transform -template scale -target patch \
  -label "Scene Size"
