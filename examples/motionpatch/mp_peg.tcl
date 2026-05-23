# examples/motionpatch/mp_peg.tcl
# Pulsed-peg pursuit demo: a motion-defined ball on a free-fall or
# ballistic-arc trajectory onto a round peg, with the bounce buried
# inside a coherence trough so the collision is never seen.
#
# This is the STAGE-1 TUNING INSTRUMENT for the "cryptic motion /
# pulsed formless dotfield" pursuit experiment. It is deliberately
# scoped: round peg only, two trajectory presets, and the handful of
# knobs whose regimes must be pinned down before any port to
# dserv/stim2. Everything that mp_pulsed.tcl carried for other
# purposes (sweep preset, internal-rotation perturbation, the
# matched/shuffled control modes) is intentionally absent here.
#
# Relationship to mp_sim:
#   The stimulus is described as an mp_sim *spec dict*. mp_sim::
#   compile_spec turns the spec into a per-frame state timeline dg;
#   the per-frame driver here simply indexes that timeline into the
#   motionpatches. The envelope math and the peak/trough bounce-time
#   resolution therefore live in the mp_sim package -- one source of
#   truth shared with the headless simulator. The demo does NOT run
#   mp_sim::sweep / ensemble (Layer B); that is for offline dotfield
#   verification. But Record writes the spec dict out, so a regime
#   tuned here can be handed to mp_sim::sweep later without
#   re-specifying anything.
#
# Trajectory:
#   The peg physics (free-fall or arc onto a round peg, with an
#   offset-driven deflection) is computed demo-side and supplied to
#   the spec as a trajectory {kind callback}. mp_sim's bounce block
#   handles only the *direction* discontinuity and its peak/trough
#   timing; the callback supplies the full kinematic *position* path
#   including the deflection. The two must agree on bounce_t -- see
#   mp_peg_build_spec.
#
# Modes:
#   continuous -- flat envelope; coherence held at base throughout.
#                 Pursuit-gain baseline.
#   pulsed     -- trapezoidal pulse train; between plateaus the ball
#                 fades to invisibility (motion-defined only). The
#                 bounce is placed inside one trough.
#
# Trajectory presets:
#   freefall -- ball released above the peg, falls straight down,
#               glances the peg on one side. The signed lateral
#               offset is the diagnostic variable: small offset =>
#               thin margin (near the unstable equilibrium), large
#               offset => robust outcome. Sample offsets in a RING
#               around the degenerate centre, never on it.
#   arc      -- ballistic parabola onto the peg flank. Incoming angle
#               and speed vary trial-to-trial so the what-you-saw ->
#               where-it-goes mapping is trial-unique.
#
# Controls:
#   Down Arrow - drop ball (replay trajectory once)
#   Up Arrow   - new trial (reroll per-trial randomness, rebuild)

load_Impro
package require mp_sim

# ============================================================
# STIM CODE
# ============================================================

proc mp_peg_make_circle_tex {size} {
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

# ============================================================
# TRAJECTORY: demo-side peg physics
# ============================================================
#
# Both presets produce a frame-rate-sampled list of (t, x, y) in dva,
# together with the bounce time and the pre/post-bounce directions.
# The position path is what the spec's callback trajectory returns;
# bounce_t / pre_dir / post_dir feed mp_sim's bounce block so the
# envelope's trough is aligned to the same instant.
#
# Geometry: the ball free-falls (or arcs) toward a round peg of
# radius r_peg. The signed lateral offset `offset` is the horizontal
# distance between the ball-centre vertical and the peg centre at the
# height of contact. A glancing collision deflects the ball away from
# the peg on the side it passed; the deflection angle is derived from
# the offset (impact parameter), not set by a free knob -- so the
# logged post-bounce direction is the genuine physical answer.

# Map a signed impact offset to a post-bounce deflection. For a round
# peg, an offset of 0 is the unstable equilibrium (no defined side);
# |offset| >= r_eff is a clean miss. In between, the contact normal
# tilts with the offset and the incoming velocity reflects about it.
# We use the standard elastic-glance model: the deflection angle grows
# with the impact parameter b = offset / r_eff as asin(b)-like, capped.
proc mp_peg_deflection {offset r_eff in_dir} {
    if {$r_eff <= 1e-9} { return $in_dir }
    set b [expr {$offset / $r_eff}]
    if {$b >  1.0} { set b  1.0 }
    if {$b < -1.0} { set b -1.0 }
    # Contact-normal tilt relative to the incoming direction. asin(b)
    # gives 0 at a dead-centre hit and +/-90 deg at a grazing miss.
    set normal_tilt [expr {asin($b)}]
    # Reflection about the tilted normal: outgoing direction is rotated
    # by 2*normal_tilt away from the incoming direction, toward the
    # side the ball passed. Sign of offset carries the side.
    return [expr {$in_dir + 2.0 * $normal_tilt}]
}

# Build the trajectory for the current preset. Writes:
#   ::mp_peg::traj_t / traj_x / traj_y   per-frame lists (dva)
#   ::mp_peg::traj_dt / traj_n
#   ::mp_peg::bounce_t                   contact time (sec)
#   ::mp_peg::bounce_x / bounce_y        peg centre (dva)
#   ::mp_peg::pre_dir / post_dir         pre/post-bounce direction (rad)
#   ::mp_peg::offset_actual              signed offset used this trial
proc mp_peg_build_trajectory {} {
    set step 0.01667
    set T    $::mp_peg::duration
    set ib_hint [mp_peg_target_bounce_index]

    set r_ball [expr {0.5 * $::mp_peg::shape_size * $::mp_peg::patch_size}]
    set r_peg  $::mp_peg::peg_radius
    set r_eff  [expr {$r_ball + $r_peg}]

    set offset $::mp_peg::offset_actual
    set g      $::mp_peg::gravity

    set ts {} ; set xs {} ; set ys {} ; set vxs {} ; set vys {}

    switch -- $::mp_peg::preset {
        freefall {
            # Contact time: the trough centre we want the bounce in,
            # minus the lead. Fall height auto-derived so the ball
            # reaches the peg at exactly that instant.
            set tb [mp_peg_target_bounce_time]
            if {$tb <= 0.0 || $tb >= $T} { set tb [expr {$T * 0.5}] }
            set g_abs [expr {abs($g)}]
            if {$g_abs < 1e-3} { set g_abs 10.0 }
            set h_fall [expr {0.5 * $g_abs * $tb * $tb}]

            # Peg sits at the configured position (+ per-trial jitter).
            set peg_x [expr {$::mp_peg::peg_x + $::mp_peg::jitter_x_actual}]
            set peg_y [expr {$::mp_peg::peg_y + $::mp_peg::jitter_y_actual}]
            # Ball falls down a vertical line offset horizontally from
            # the peg centre by `offset`. Contact when the ball centre
            # is r_eff above the peg centre.
            set ball_line_x [expr {$peg_x + $offset}]
            set y_contact   [expr {$peg_y + $r_eff}]
            set y0 [expr {$y_contact + $h_fall}]

            set in_dir [expr {-3.14159265358979 / 2.0}] ;# straight down
            set v_contact [expr {$g_abs * $tb}]
            set out_dir   [mp_peg_deflection $offset $r_eff $in_dir]
            for {set t 0} {$t <= $T + 1e-6} {set t [expr {$t + $step}]} {
                if {$t < $tb} {
                    set yy [expr {$y0 - 0.5 * $g_abs * $t * $t}]
                    # Velocity: straight down, magnitude g_abs*t (dva/sec).
                    set vx 0.0
                    set vy [expr {-$g_abs * $t}]
                    lappend ts $t ; lappend xs $ball_line_x ; lappend ys $yy
                    lappend vxs $vx ; lappend vys $vy
                } else {
                    # post-bounce: launch from contact along out_dir,
                    # then continue to accelerate under gravity.
                    set tau  [expr {$t - $tb}]
                    set vx0o [expr {$v_contact * cos($out_dir)}]
                    set vy0o [expr {$v_contact * sin($out_dir)}]
                    set xx [expr {$ball_line_x + $vx0o * $tau}]
                    set yy [expr {$y_contact + $vy0o * $tau \
                                  - 0.5 * $g_abs * $tau * $tau}]
                    set vx $vx0o
                    set vy [expr {$vy0o - $g_abs * $tau}]
                    lappend ts $t ; lappend xs $xx ; lappend ys $yy
                    lappend vxs $vx ; lappend vys $vy
                }
            }
            set ::mp_peg::bounce_t  $tb
            set ::mp_peg::bounce_x  $peg_x
            set ::mp_peg::bounce_y  $peg_y
            set ::mp_peg::pre_dir   $in_dir
            set ::mp_peg::post_dir  $out_dir
        }
        arc {
            # Ballistic arc onto the peg flank. The ball is launched so
            # that, ignoring the peg, it would pass the peg centre with
            # horizontal offset `offset` at the target bounce time.
            set tb [mp_peg_target_bounce_time]
            if {$tb <= 0.0 || $tb >= $T} { set tb [expr {$T * 0.5}] }
            set g_abs [expr {abs($g)}]
            if {$g_abs < 1e-3} { set g_abs 10.0 }

            set peg_x [expr {$::mp_peg::peg_x + $::mp_peg::jitter_x_actual}]
            set peg_y [expr {$::mp_peg::peg_y + $::mp_peg::jitter_y_actual}]
            set y_contact [expr {$peg_y + $r_eff}]
            set x_contact [expr {$peg_x + $offset}]

            # Launch point and velocity: from arc_x0/arc_y0, solve for
            # the (vx0, vy0) that delivers the ball to (x_contact,
            # y_contact) at t = tb under gravity g_abs.
            set x0 $::mp_peg::arc_x0
            set y0 $::mp_peg::arc_y0
            set vx0 [expr {($x_contact - $x0) / $tb}]
            set vy0 [expr {(($y_contact - $y0) + 0.5 * $g_abs * $tb * $tb) / $tb}]
            # Incoming velocity at contact (pre-bounce).
            set v_in_x $vx0
            set v_in_y [expr {$vy0 - $g_abs * $tb}]
            set v_in   [expr {hypot($v_in_x, $v_in_y)}]
            set in_dir [expr {atan2($v_in_y, $v_in_x)}]
            set out_dir [mp_peg_deflection $offset $r_eff $in_dir]
            set vx_out [expr {$v_in * cos($out_dir)}]
            set vy_out [expr {$v_in * sin($out_dir)}]

            for {set t 0} {$t <= $T + 1e-6} {set t [expr {$t + $step}]} {
                if {$t < $tb} {
                    set xx [expr {$x0 + $vx0 * $t}]
                    set yy [expr {$y0 + $vy0 * $t - 0.5 * $g_abs * $t * $t}]
                    set vx $vx0
                    set vy [expr {$vy0 - $g_abs * $t}]
                    lappend ts $t ; lappend xs $xx ; lappend ys $yy
                    lappend vxs $vx ; lappend vys $vy
                } else {
                    set tau [expr {$t - $tb}]
                    set xx [expr {$x_contact + $vx_out * $tau}]
                    set yy [expr {$y_contact + $vy_out * $tau \
                                  - 0.5 * $g_abs * $tau * $tau}]
                    set vx $vx_out
                    set vy [expr {$vy_out - $g_abs * $tau}]
                    lappend ts $t ; lappend xs $xx ; lappend ys $yy
                    lappend vxs $vx ; lappend vys $vy
                }
            }
            set ::mp_peg::bounce_t  $tb
            set ::mp_peg::bounce_x  $peg_x
            set ::mp_peg::bounce_y  $peg_y
            set ::mp_peg::pre_dir   $in_dir
            set ::mp_peg::post_dir  $out_dir
        }
        default { error "mp_peg: unknown preset '$::mp_peg::preset'" }
    }

    set ::mp_peg::traj_t  $ts
    set ::mp_peg::traj_x  $xs
    set ::mp_peg::traj_y  $ys
    set ::mp_peg::traj_vx $vxs
    set ::mp_peg::traj_vy $vys
    set ::mp_peg::traj_dt $step
    set ::mp_peg::traj_n  [llength $ts]
}

# ============================================================
# PULSE TRAIN + BOUNCE TIMING
# ============================================================
#
# Pulse centres are computed here (so jitter can be applied per trial)
# and handed to the spec as an explicit `centers` list. The bounce
# trough is one chosen inter-pulse gap; the bounce is placed at the
# centre of that gap minus `trough_lead_ms` -- the single most
# delicate knob, scrubbed live.

# Even pulse centres with per-trial jitter. The pulse bracketing the
# bounce trough is jittered less (bounce_trough_jitter_ms) so that
# trough_lead_ms stays meaningful relative to a stable trough centre.
proc mp_peg_pulse_centers {} {
    set N $::mp_peg::n_snapshots
    set T $::mp_peg::duration
    set k $::mp_peg::bounce_pulse_index
    set centers {}
    for {set i 0} {$i < $N} {incr i} {
        set c [expr {$T * ($i + 0.5) / $N}]
        # Pulses i==k and i==k+1 bracket the bounce trough.
        if {$i == $k || $i == [expr {$k + 1}]} {
            set j $::mp_peg::bounce_trough_jitter_ms
        } else {
            set j $::mp_peg::pulse_jitter_ms
        }
        set jit [expr {(rand() * 2.0 - 1.0) * $j / 1000.0}]
        lappend centers [expr {$c + $jit}]
    }
    return $centers
}

# Centre of the bounce trough = midpoint of pulses k and k+1.
proc mp_peg_target_bounce_time {} {
    set centers $::mp_peg::pulse_centers
    set k $::mp_peg::bounce_pulse_index
    if {[llength $centers] < 2} { return [expr {$::mp_peg::duration * 0.5}] }
    if {$k > [expr {[llength $centers] - 2}]} {
        set k [expr {[llength $centers] - 2}]
    }
    set ta [lindex $centers $k]
    set tb [lindex $centers [expr {$k + 1}]]
    set mid [expr {0.5 * ($ta + $tb)}]
    return [expr {$mid - $::mp_peg::trough_lead_ms / 1000.0}]
}

proc mp_peg_target_bounce_index {} {
    return $::mp_peg::bounce_pulse_index
}

# ============================================================
# SPEC CONSTRUCTION
# ============================================================
#
# Assemble the mp_sim spec dict from the current knobs + the trajectory
# just built. This dict is BOTH the thing compile_spec consumes to
# drive rendering AND the design artifact written by Record.
proc mp_peg_build_spec {} {
    set T  $::mp_peg::duration
    set dt $::mp_peg::traj_dt

    # --- envelope ---
    if {$::mp_peg::mode eq "pulsed"} {
        set envelope [dict create \
            kind        trapezoid_train \
            centers     $::mp_peg::pulse_centers \
            plateau_dur [expr {$::mp_peg::plateau_ms / 1000.0}] \
            ease_dur    [expr {$::mp_peg::ease_ms    / 1000.0}] \
            base_coh    $::mp_peg::base_coh]
    } else {
        set envelope [dict create kind flat base_coh $::mp_peg::base_coh]
    }

    # --- trajectory: callback into the demo-side path ---
    # mp_sim's callback trajectory calls {*}$proc $t and our callback
    # returns {x y vx vy}: position in dva, velocity in dva/sec.
    # compile_spec derives the coherent-state speed/direction from that
    # velocity, so the timeline's speed column is in dva/sec and the
    # endpoint speeds below MUST also be in dva/sec for the trough
    # tween to be unit-consistent. The per-frame driver converts the
    # timeline speed dva/sec -> patch-local units before motionpatch.
    set trajectory [dict create kind callback proc mp_peg_traj_at]

    # --- bounce: direction step, peak/trough resolution ---
    # We hand mp_sim explicit custom timing (we already know bounce_t
    # from the trajectory build) so the envelope's trough and the
    # position path agree exactly. pre_dir/post_dir are the physical
    # directions; mp_sim overwrites its direction tween with the step.
    set bounce [dict create \
        phase    custom \
        t_custom $::mp_peg::bounce_t \
        pre_dir  $::mp_peg::pre_dir \
        post_dir $::mp_peg::post_dir]

    # --- endpoints: ball vs surround dot statistics ---
    # Speeds in dva/sec (see trajectory note above). target.speed is a
    # fallback only -- because the callback trajectory carries motion,
    # compile_spec uses the trajectory's own per-frame speed for the
    # coherent state and target.speed is not actually rendered; we set
    # it to the speed clamp as an honest nominal value. surround.speed
    # IS used: it is the trough target the envelope tweens toward.
    set endpoints [dict create \
        target [dict create \
            coh   $::mp_peg::base_coh \
            speed $::mp_peg::max_speed_deg_sec \
            dir   $::mp_peg::pre_dir \
            life  $::mp_peg::target_lifetime] \
        surround [dict create \
            coh   0.0 \
            speed $::mp_peg::surround_speed_dva_sec \
            dir   0.0 \
            life  $::mp_peg::bg_lifetime]]

    # --- callbacks: mark the frames where the ball is effectively
    #     invisible (coherence below the visibility threshold). Used
    #     offline to verify the bounce frame sits inside a trough; the
    #     live driver ignores them. ---
    set callbacks [list [dict create \
        name   trough_enter \
        threshold [expr {$::mp_peg::vis_threshold * $::mp_peg::base_coh}] \
        direction falling \
        proc   mp_peg_noop_callback] \
        [dict create \
        name   trough_exit \
        threshold [expr {$::mp_peg::vis_threshold * $::mp_peg::base_coh}] \
        direction rising \
        proc   mp_peg_noop_callback]]

    set spec [dict create \
        meta       [dict create duration $T dt $dt \
                        patch_size_dva $::mp_peg::patch_size] \
        endpoints  $endpoints \
        envelope   $envelope \
        trajectory $trajectory \
        bounce     $bounce \
        callbacks  $callbacks]

    # Stash design ground-truth alongside the spec so the written
    # artifact is self-describing for offline pursuit analysis.
    dict set spec design [dict create \
        preset          $::mp_peg::preset \
        offset_actual   $::mp_peg::offset_actual \
        bounce_t        $::mp_peg::bounce_t \
        bounce_x        $::mp_peg::bounce_x \
        bounce_y        $::mp_peg::bounce_y \
        pre_dir         $::mp_peg::pre_dir \
        post_dir        $::mp_peg::post_dir \
        pulse_centers   $::mp_peg::pulse_centers \
        trough_lead_ms  $::mp_peg::trough_lead_ms \
        peg_visible     [expr {$::mp_peg::peg_mode eq "visible"}] \
        n_snapshots     $::mp_peg::n_snapshots]

    return $spec
}

proc mp_peg_noop_callback {name frame_idx t value} { return }

# Trajectory callback used by the spec: look up demo-side path at t.
# mp_sim::eval_trajectory calls this once per frame at compile time.
# Returns {x y vx vy} -- position in dva, velocity in dva/sec. Supplying
# velocity (rather than letting compile_spec finite-difference the
# position) gives an exact speed at the bounce frame, where a centred
# difference would otherwise smear the velocity discontinuity.
proc mp_peg_traj_at {t} {
    set dt $::mp_peg::traj_dt
    set n  $::mp_peg::traj_n
    if {$dt <= 0 || $n <= 0} { return {0.0 0.0 0.0 0.0} }
    set i [expr {int($t / $dt)}]
    if {$i < 0} { set i 0 }
    if {$i >= $n} { set i [expr {$n - 1}] }
    return [list [lindex $::mp_peg::traj_x  $i] \
                 [lindex $::mp_peg::traj_y  $i] \
                 [lindex $::mp_peg::traj_vx $i] \
                 [lindex $::mp_peg::traj_vy $i]]
}

# dva/sec -> motionpatch patch-local units/sec. double() forces float
# arithmetic (see mp_pulsed.tcl for the Tcl integer-division gotcha).
proc mp_peg_speed_pu {v} {
    set ps $::mp_peg::patch_size
    if {$ps <= 0} { return 0.0 }
    return [expr {double($v) / double($ps)}]
}

# ============================================================
# TRIAL ASSEMBLY
# ============================================================
#
# Reroll per-trial randomness, rebuild the trajectory, recompile the
# spec into a timeline. Called on reset (Up Arrow) and at record start.
proc mp_peg_new_trial {} {
    mp_peg_seed_trial
    set ::mp_peg::pulse_centers [mp_peg_pulse_centers]
    mp_peg_build_trajectory
    mp_peg_recompile
}

# Per-trial random elements: signed offset sampled from the ring
# [offset_min, offset_max] (magnitude) with random side; optional
# scene-position jitter applied uniformly to ball line and peg.
proc mp_peg_seed_trial {} {
    set lo $::mp_peg::offset_min
    set hi $::mp_peg::offset_max
    set mag  [expr {$lo + rand() * ($hi - $lo)}]
    set side [expr {(rand() < 0.5) ? -1.0 : 1.0}]
    set ::mp_peg::offset_actual [expr {$side * $mag}]

    if {$::mp_peg::random_position} {
        set rx $::mp_peg::jitter_x_range
        set ry $::mp_peg::jitter_y_range
        set ::mp_peg::jitter_x_actual [expr {(rand() * 2.0 - 1.0) * $rx}]
        set ::mp_peg::jitter_y_actual [expr {(rand() * 2.0 - 1.0) * $ry}]
    } else {
        set ::mp_peg::jitter_x_actual 0.0
        set ::mp_peg::jitter_y_actual 0.0
    }
}

# Compile the current spec into a timeline dg. The timeline is the
# per-frame plan the driver reads; we keep its name in the namespace.
proc mp_peg_recompile {} {
    set spec [mp_peg_build_spec]
    set ::mp_peg::spec $spec
    catch {dg_delete $::mp_peg::timeline}
    set ::mp_peg::timeline \
        [mp_sim::compile_spec $spec -gname mp_peg_timeline]
}

# ============================================================
# PER-FRAME DRIVER
# ============================================================
#
# The timeline holds per-frame coherence / mask_offset / direction /
# speed / lifetime. We index it by play time and push the values into
# the two motionpatches. The trough state is made statistically
# identical to the surround by driving dots_target speed and lifetime
# from the timeline (which mp_sim already tweened by the envelope),
# not just coherence.
proc mp_peg_index_for_time {tsec} {
    set tl $::mp_peg::timeline
    if {$tl eq "" || ![dg_exists $tl]} { return 0 }
    set dt [dl_get $tl:dt 0]
    set n  [dl_get $tl:n_frames 0]
    if {$dt <= 0 || $n <= 0} { return 0 }
    set idx [expr {int($tsec / $dt)}]
    if {$idx < 0} { return 0 }
    if {$idx >= $n} { return [expr {$n - 1}] }
    return $idx
}

proc mp_peg_update {} {
    set t [expr {$::StimTime / 1000.0}]
    set dt [expr {$t - $::mp_peg::last_t}]
    if {$dt < 0.0 || $dt > 0.1} { set dt 0.016 }
    set ::mp_peg::last_t $t

    if {!$::mp_peg::dropping} { return }

    set ::mp_peg::play_t [expr {$::mp_peg::play_t + $dt}]
    set tplay $::mp_peg::play_t

    set tl $::mp_peg::timeline
    if {$tl eq "" || ![dg_exists $tl]} { return }
    set T [dl_get $tl:duration 0]

    if {$tplay > $T} {
        set ::mp_peg::dropping 0
        # Collapse the ball into the surround so it doesn't freeze
        # visibly at the terminal (often high-speed) state.
        set surr [mp_peg_speed_pu $::mp_peg::surround_speed_dva_sec]
        motionpatch_coherence dots_target 0.0
        motionpatch_speed     dots_target $surr
        motionpatch_lifetime  dots_target $::mp_peg::bg_lifetime
        if {$::mp_peg::recording} {
            set ::mp_peg::recording 0
            catch {motionpatch_logEnd dots_target}
            catch {motionpatch_logEnd dots_bg}
            catch {motionpatch_logExport dots_target \
                       $::mp_peg::record_target_name}
            catch {motionpatch_logExport dots_bg \
                       $::mp_peg::record_bg_name}
        }
        return
    }

    set i [mp_peg_index_for_time $tplay]

    set ox  [dl_get $tl:mask_offset_x $i]
    set oy  [dl_get $tl:mask_offset_y $i]
    set dir [dl_get $tl:direction     $i]
    set coh [dl_get $tl:coherence     $i]
    set sp  [dl_get $tl:speed         $i]
    set life [dl_get $tl:lifetime_s   $i]

    # mask_offset and speed from the timeline are in dva and dva/sec
    # (the trajectory callback returns dva positions + dva/sec
    # velocities, and compile_spec derives the speed column from those).
    # motionpatch wants patch-local units, so divide both by patch_size.
    set ps $::mp_peg::patch_size
    set mox [expr {double($ox) / double($ps)}]
    set moy [expr {double($oy) / double($ps)}]
    set sp_pu [expr {double($sp) / double($ps)}]

    motionpatch_maskoffset dots_target $mox $moy
    motionpatch_maskoffset dots_bg     $mox $moy
    motionpatch_direction  dots_target $dir
    motionpatch_speed      dots_target $sp_pu
    motionpatch_lifetime   dots_target $life
    motionpatch_coherence  dots_target $coh
}

# ============================================================
# TRIGGERS / KEYBOARD
# ============================================================

proc mp_peg_trigger {what} {
    switch -- $what {
        drop {
            set ::mp_peg::play_t   0.0
            set ::mp_peg::last_t   [expr {$::StimTime / 1000.0}]
            set ::mp_peg::dropping 1
        }
        reset {
            set ::mp_peg::dropping 0
            mp_peg_new_trial
            mp_peg_preposition
            redraw
        }
        record {
            mp_peg_new_trial
            mp_peg_preposition
            # Write the spec dict as the design artifact, then arm the
            # per-frame logs and drop.
            mp_peg_write_spec $::mp_peg::record_design_name
            catch {motionpatch_logBegin dots_target}
            catch {motionpatch_logBegin dots_bg}
            set ::mp_peg::recording 1
            set ::mp_peg::play_t   0.0
            set ::mp_peg::last_t   [expr {$::StimTime / 1000.0}]
            set ::mp_peg::dropping 1
        }
        write_dgs {
            foreach nm [list $::mp_peg::record_design_name \
                             $::mp_peg::record_target_name \
                             $::mp_peg::record_bg_name] {
                if {[dg_exists $nm]} { dg_write $nm /tmp/$nm.dgz }
            }
        }
    }
}

# Pre-position the aperture at the trajectory start with the ball
# invisible (coherence 0, plus speed/lifetime collapsed onto the
# surround statistics so no motion-defined boundary is visible) until
# "drop". This mirrors the trial-end collapse block in mp_peg_update.
proc mp_peg_preposition {} {
    if {$::mp_peg::traj_n <= 0} { return }
    set x0 [lindex $::mp_peg::traj_x 0]
    set y0 [lindex $::mp_peg::traj_y 0]
    set ps $::mp_peg::patch_size
    set mox [expr {double($x0) / double($ps)}]
    set moy [expr {double($y0) / double($ps)}]
    motionpatch_maskoffset dots_target $mox $moy
    motionpatch_maskoffset dots_bg     $mox $moy
    set surr [mp_peg_speed_pu $::mp_peg::surround_speed_dva_sec]
    motionpatch_coherence dots_target 0.0
    motionpatch_speed     dots_target $surr
    motionpatch_lifetime  dots_target $::mp_peg::bg_lifetime
    mp_peg_apply_peg_marker
}

proc onDownArrow {} { mp_peg_trigger drop }
proc onUpArrow   {} { mp_peg_trigger reset }

# ============================================================
# PEG MARKER
# ============================================================
#
# The peg is drawn at the canonical (un-jittered-offset) peg centre on
# EVERY trial regardless of which side the ball passes, so its
# location never predicts the deflection. Visibility is gated by
# peg_mode: "visible" => model has the obstacle; "absent" => the
# control with nothing to simulate.
proc mp_peg_apply_peg_marker {} {
    set mk $::mp_peg::peg_marker_id
    if {$mk eq ""} { return }
    if {$::mp_peg::peg_mode eq "visible"} {
        set px [expr {$::mp_peg::peg_x + $::mp_peg::jitter_x_actual}]
        set py [expr {$::mp_peg::peg_y + $::mp_peg::jitter_y_actual}]
        translateObj $mk $px $py
        setVisible $mk 1
    } else {
        setVisible $mk 0
    }
}

# ============================================================
# DESIGN ARTIFACT
# ============================================================
#
# Write the spec dict into a one-row dg so it persists in stim2's
# dgTable and can be dg_write'n. The spec is stored as a string column
# (the dict is small); offline code does `dict get` after reading it
# back. This is the artifact mp_sim::sweep / compile_spec can re-consume.
proc mp_peg_write_spec {gname} {
    catch {dg_delete $gname}
    set g [dg_create $gname]
    dl_set $g:spec [dl_slist $::mp_peg::spec]
    # Also break out the design sub-dict into flat columns for quick
    # inspection without re-parsing the spec.
    set d [dict get $::mp_peg::spec design]
    foreach k {preset offset_actual bounce_t bounce_x bounce_y \
               pre_dir post_dir trough_lead_ms peg_visible n_snapshots} {
        set v [dict get $d $k]
        if {[string is double -strict $v]} {
            dl_set $g:$k [dl_flist $v]
        } else {
            dl_set $g:$k [dl_slist $v]
        }
    }
    dl_set $g:pulse_centers [dl_flist {*}[dict get $d pulse_centers]]
    return $g
}

# ============================================================
# SETUP
# ============================================================

proc mp_peg_setup {patch_size_dva dot_density} {
    glistInit 1
    resetObjList
    shaderImageReset

    namespace eval ::mp_peg {
        variable mode             continuous
        variable preset           freefall
        variable dropping         0
        variable last_t           0.0
        variable play_t           0.0

        # --- geometry ---
        variable patch_size      13.0
        variable shape_size       0.08
        variable peg_radius       0.18   ;# dva
        variable peg_x            0.0
        variable peg_y           -2.0
        variable max_speed_deg_sec 30.0
        variable gravity        -10.0    ;# dva/sec^2

        # --- arc launch point ---
        variable arc_x0          -5.0
        variable arc_y0           4.0

        # --- offset / difficulty (the graded axis) ---
        # Signed offset is sampled per trial from the RING
        # [offset_min, offset_max] in magnitude, with random side.
        # offset_min must stay > 0: never sample the degenerate centre.
        variable offset_min       0.15
        variable offset_max       0.55
        variable offset_actual    0.30

        # --- per-trial scene-position jitter ---
        variable random_position  0
        variable jitter_x_range    2.0
        variable jitter_y_range    0.5
        variable jitter_x_actual   0.0
        variable jitter_y_actual   0.0

        # --- pulse train ---
        variable n_snapshots      7
        variable plateau_ms      60.0    ;# coherent plateau width
        variable ease_ms         40.0    ;# raised-cosine rise/fall
        variable pulse_jitter_ms 25.0    ;# filler-pulse phase jitter
        # The two pulses bracketing the bounce trough are jittered less
        # so trough_lead_ms stays meaningful against a stable trough.
        variable bounce_trough_jitter_ms 5.0
        variable base_coh         1.0

        # --- bounce trough (the critical panel) ---
        variable bounce_pulse_index 3   ;# trough = gap between k, k+1
        # Bounce time = trough centre - trough_lead_ms. Scrub this to
        # place the blank relative to contact: too early => approach
        # geometry not yet resolved (floor); too late => deflection
        # onset is seen (ceiling).
        variable trough_lead_ms   0.0

        # --- dot statistics ---
        variable target_lifetime  0.5   ;# sec, peak-pulse value
        variable bg_lifetime       0.08  ;# sec, surround / trough value
        variable surround_speed_dva_sec 3.0
        variable ball_lum          0.8
        variable surround_lum      0.8

        # --- visibility threshold for the verification callbacks ---
        # Coherence below vis_threshold*base_coh counts as "ball
        # effectively invisible". Offline, the bounce frame must sit
        # between a trough_enter and a trough_exit crossing.
        variable vis_threshold     0.10

        # --- peg visibility ---
        variable peg_mode          visible  ;# visible | absent
        variable peg_marker_id     ""

        # --- trajectory storage ---
        variable traj_t  {}
        variable traj_x  {}
        variable traj_y  {}
        variable traj_vx {}
        variable traj_vy {}
        variable traj_dt 0.0
        variable traj_n  0
        variable bounce_t  0.0
        variable bounce_x  0.0
        variable bounce_y  0.0
        variable pre_dir   0.0
        variable post_dir  0.0
        variable pulse_centers {}

        # --- compiled spec / timeline ---
        variable spec     {}
        variable timeline ""

        # --- recording ---
        variable recording          0
        variable record_design_name mp_peg_design
        variable record_target_name mp_peg_log_target
        variable record_bg_name     mp_peg_log_bg

        variable duration 1.5
    }
    set ::mp_peg::patch_size $patch_size_dva

    # First trial.
    mp_peg_new_trial

    set nDots [expr {int($dot_density * $patch_size_dva * $patch_size_dva)}]
    if {$nDots < 100} { set nDots 100 }

    set tex   [mp_peg_make_circle_tex 256]
    set texID [shaderImageID $tex]

    set initialSpeed [mp_peg_speed_pu $::mp_peg::surround_speed_dva_sec]
    set ptSize 3.0

    set mg [metagroup]
    objName $mg patch

    set bgL $::mp_peg::surround_lum
    set tgL $::mp_peg::ball_lum

    set mp_bg [motionpatch $nDots $initialSpeed 0.5]
    objName $mp_bg dots_bg
    motionpatch_pointsize $mp_bg $ptSize
    motionpatch_color $mp_bg $bgL $bgL $bgL 1.0
    motionpatch_masktype $mp_bg 0
    motionpatch_coherence $mp_bg 0.0
    motionpatch_lifetime $mp_bg $::mp_peg::bg_lifetime
    motionpatch_direction $mp_bg 0.0
    motionpatch_speed $mp_bg $initialSpeed
    motionpatch_setSampler $mp_bg $texID 0
    motionpatch_samplermaskmode $mp_bg 2
    motionpatch_maskscale $mp_bg $::mp_peg::shape_size
    metagroupAdd $mg $mp_bg

    set mp_tg [motionpatch $nDots $initialSpeed 0.5]
    objName $mp_tg dots_target
    motionpatch_pointsize $mp_tg $ptSize
    motionpatch_color $mp_tg $tgL $tgL $tgL 1.0
    motionpatch_masktype $mp_tg 0
    motionpatch_coherence $mp_tg 0.0
    motionpatch_lifetime $mp_tg $::mp_peg::target_lifetime
    motionpatch_direction $mp_tg 0.0
    motionpatch_speed $mp_tg $initialSpeed
    motionpatch_setSampler $mp_tg $texID 0
    motionpatch_samplermaskmode $mp_tg 1
    motionpatch_maskscale $mp_tg $::mp_peg::shape_size
    metagroupAdd $mg $mp_tg

    addPreScript $mp_bg mp_peg_update

    scaleObj $mg $::mp_peg::patch_size $::mp_peg::patch_size
    glistAddObject $mg 0

    # Fixation spot (yellow ring, black centre) for the enforced-
    # fixation condition.
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

    # Peg marker -- always created, visibility toggled by peg_mode.
    set mk [polygon]
    polycirc $mk 1
    polycolor $mk 0.9 0.2 0.7
    scaleObj $mk [expr {2.0 * $::mp_peg::peg_radius}]
    set ::mp_peg::peg_marker_id $mk
    glistAddObject $mk 0

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1

    mp_peg_preposition
    redraw
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
#
# Knob setters: each writes a namespace var, then rebuilds. Most knobs
# only need a recompile on the *next* trial; we recompile immediately
# so the demo shows the change at once. Trial-random elements are NOT
# rerolled by adjuster changes -- only by reset/record.

proc mp_peg_set_mode {mode} {
    set ::mp_peg::mode $mode
    mp_peg_recompile ; mp_peg_preposition ; redraw
}
proc mp_peg_get_mode {} { dict create mode continuous }

proc mp_peg_set_preset {preset} {
    set ::mp_peg::preset $preset
    mp_peg_new_trial ; mp_peg_preposition ; redraw
}
proc mp_peg_get_preset {} { dict create preset freefall }

proc mp_peg_set_pulse {n_snapshots plateau_ms ease_ms pulse_jitter_ms} {
    set ::mp_peg::n_snapshots     $n_snapshots
    set ::mp_peg::plateau_ms      $plateau_ms
    set ::mp_peg::ease_ms         $ease_ms
    set ::mp_peg::pulse_jitter_ms $pulse_jitter_ms
    mp_peg_new_trial ; mp_peg_preposition ; redraw
}
proc mp_peg_get_pulse {} {
    dict create n_snapshots 7 plateau_ms 60.0 ease_ms 40.0 \
                pulse_jitter_ms 25.0
}

proc mp_peg_set_bounce {bounce_pulse_index trough_lead_ms \
                        bounce_trough_jitter_ms peg_mode} {
    set ::mp_peg::bounce_pulse_index      $bounce_pulse_index
    set ::mp_peg::trough_lead_ms          $trough_lead_ms
    set ::mp_peg::bounce_trough_jitter_ms $bounce_trough_jitter_ms
    set ::mp_peg::peg_mode                $peg_mode
    mp_peg_new_trial ; mp_peg_preposition ; redraw
}
proc mp_peg_get_bounce {} {
    dict create bounce_pulse_index 3 trough_lead_ms 0.0 \
                bounce_trough_jitter_ms 5.0 peg_mode visible
}

proc mp_peg_set_offset {offset_min offset_max} {
    set ::mp_peg::offset_min $offset_min
    set ::mp_peg::offset_max $offset_max
    mp_peg_new_trial ; mp_peg_preposition ; redraw
}
proc mp_peg_get_offset {} { dict create offset_min 0.15 offset_max 0.55 }

proc mp_peg_set_geometry {peg_radius peg_x peg_y gravity max_speed_deg_sec} {
    set ::mp_peg::peg_radius        $peg_radius
    set ::mp_peg::peg_x             $peg_x
    set ::mp_peg::peg_y             $peg_y
    set ::mp_peg::gravity           $gravity
    set ::mp_peg::max_speed_deg_sec $max_speed_deg_sec
    mp_peg_new_trial ; mp_peg_preposition ; redraw
}
proc mp_peg_get_geometry {} {
    dict create peg_radius 0.18 peg_x 0.0 peg_y -2.0 gravity -10.0 \
                max_speed_deg_sec 30.0
}

proc mp_peg_set_position_jitter {random_position jitter_x_range jitter_y_range} {
    set ::mp_peg::random_position $random_position
    set ::mp_peg::jitter_x_range  $jitter_x_range
    set ::mp_peg::jitter_y_range  $jitter_y_range
    mp_peg_new_trial ; mp_peg_preposition ; redraw
}
proc mp_peg_get_position_jitter {} {
    dict create random_position 0 jitter_x_range 2.0 jitter_y_range 0.5
}

proc mp_peg_set_dots {target_lifetime bg_lifetime surround_speed_dva_sec} {
    set ::mp_peg::target_lifetime         $target_lifetime
    set ::mp_peg::bg_lifetime             $bg_lifetime
    set ::mp_peg::surround_speed_dva_sec  $surround_speed_dva_sec
    # Push to live dots_bg patch -- it isn't driven per-frame by
    # mp_peg_update, so without this the bg keeps the speed/lifetime
    # set once at setup and the trough invariant (target trough state
    # == surround state) silently breaks when these knobs are scrubbed.
    catch {
        motionpatch_speed    dots_bg [mp_peg_speed_pu $surround_speed_dva_sec]
        motionpatch_lifetime dots_bg $bg_lifetime
    }
    mp_peg_recompile ; mp_peg_preposition ; redraw
}
proc mp_peg_get_dots {} {
    dict create target_lifetime 0.5 bg_lifetime 0.08 \
                surround_speed_dva_sec 3.0
}

proc mp_peg_set_luminance {ball_lum surround_lum} {
    set ::mp_peg::ball_lum     $ball_lum
    set ::mp_peg::surround_lum $surround_lum
    catch {
        motionpatch_color dots_target $ball_lum $ball_lum $ball_lum 1.0
        motionpatch_color dots_bg $surround_lum $surround_lum $surround_lum 1.0
    }
    redraw
}
proc mp_peg_get_luminance {} { dict create ball_lum 0.8 surround_lum 0.8 }

proc mp_peg_set_duration {duration} {
    set ::mp_peg::duration $duration
    mp_peg_new_trial ; mp_peg_preposition ; redraw
}
proc mp_peg_get_duration {} { dict create duration 1.5 }

proc mp_peg_set_shape_size {shape_size} {
    set ::mp_peg::shape_size $shape_size
    catch {
        motionpatch_maskscale dots_target $shape_size
        motionpatch_maskscale dots_bg     $shape_size
    }
    mp_peg_new_trial ; mp_peg_preposition ; redraw
}
proc mp_peg_get_shape_size {} { dict create shape_size 0.08 }

workspace::reset

workspace::setup mp_peg_setup {
    patch_size_dva {float 8.0 32.0 1.0 13.0 "Patch Size (dva)"}
    dot_density    {float 0.5 100.0 0.5 24.0 "Dot Density (dots/dva^2)"}
} -adjusters {peg_actions peg_mode peg_preset peg_pulse peg_bounce \
              peg_offset peg_geometry peg_position_jitter peg_dots \
              peg_luminance peg_duration peg_shape peg_transform} \
  -label "Motion Peg (pulsed pursuit)"

workspace::adjuster peg_actions {
    drop      {action "Drop / Replay (↓)"}
    reset     {action "New Trial (↑)"}
    record    {action "Record (spec + per-frame log)"}
    write_dgs {action "Write dgs to /tmp"}
} -target {} -proc mp_peg_trigger -label "Actions"

workspace::adjuster peg_mode {
    mode {choice {continuous pulsed} continuous "Mode"}
} -target {} -proc mp_peg_set_mode -getter mp_peg_get_mode \
  -label "Mode (continuous vs pulsed)"

workspace::adjuster peg_preset {
    preset {choice {freefall arc} freefall "Trajectory Preset"}
} -target {} -proc mp_peg_set_preset -getter mp_peg_get_preset \
  -label "Trajectory Preset"

# Pulse train. plateau_ms / ease_ms set trough FLOOR and WIDTH
# independently (trapezoid_train), unlike a single Gaussian sigma.
workspace::adjuster peg_pulse {
    n_snapshots     {int 2 25 1 7 "Number of Snapshots (N)"}
    plateau_ms      {float 5.0 300.0 5.0 60.0 "Plateau Width (ms)"}
    ease_ms         {float 5.0 200.0 5.0 40.0 "Ease Rise/Fall (ms)"}
    pulse_jitter_ms {float 0.0 100.0 5.0 25.0 "Filler Pulse Jitter (ms)"}
} -target {} -proc mp_peg_set_pulse -getter mp_peg_get_pulse \
  -label "Pulse Train"

# THE CRITICAL PANEL. trough_lead_ms places the blank relative to
# contact; scrub it to find the window between floor and ceiling.
workspace::adjuster peg_bounce {
    bounce_pulse_index      {int 0 23 1 3 "Bounce Trough Index (gap k..k+1)"}
    trough_lead_ms          {float -150.0 150.0 5.0 0.0 "Trough Lead (ms before contact)"}
    bounce_trough_jitter_ms {float 0.0 50.0 1.0 5.0 "Bounce-Trough Pulse Jitter (ms)"}
    peg_mode                {choice {visible absent} visible "Peg"}
} -target {} -proc mp_peg_set_bounce -getter mp_peg_get_bounce \
  -label "Bounce Trough (critical)"

# Difficulty axis. Offset sampled per trial from the ring
# [min, max]; min MUST stay > 0 to avoid the degenerate equilibrium.
workspace::adjuster peg_offset {
    offset_min {float 0.02 2.0 0.01 0.15 "Offset Ring Min (dva)"}
    offset_max {float 0.05 3.0 0.05 0.55 "Offset Ring Max (dva)"}
} -target {} -proc mp_peg_set_offset -getter mp_peg_get_offset \
  -label "Offset / Difficulty"

workspace::adjuster peg_geometry {
    peg_radius        {float 0.05 1.0 0.01 0.18 "Peg Radius (dva)"}
    peg_x             {float -8.0 8.0 0.5 0.0 "Peg X (dva)"}
    peg_y             {float -8.0 8.0 0.5 -2.0 "Peg Y (dva)"}
    gravity           {float -40.0 -1.0 0.5 -10.0 "Gravity (dva/sec^2)"}
    max_speed_deg_sec {float 5.0 80.0 1.0 30.0 "Max Speed Clamp (dva/sec)"}
} -target {} -proc mp_peg_set_geometry -getter mp_peg_get_geometry \
  -label "Peg Geometry"

workspace::adjuster peg_position_jitter {
    random_position {bool 0 "Randomize Scene Position Per Trial"}
    jitter_x_range  {float 0.0 6.0 0.25 2.0 "Position Jitter X (±dva)"}
    jitter_y_range  {float 0.0 3.0 0.25 0.5 "Position Jitter Y (±dva)"}
} -target {} -proc mp_peg_set_position_jitter \
  -getter mp_peg_get_position_jitter -label "Scene Position Jitter"

workspace::adjuster peg_dots {
    target_lifetime        {float 0.05 2.0 0.05 0.5 "Ball Lifetime (sec)"}
    bg_lifetime            {float 0.01 1.0 0.01 0.08 "Surround Lifetime (sec)"}
    surround_speed_dva_sec {float 0.0 15.0 0.5 3.0 "Surround Speed (dva/sec)"}
} -target {} -proc mp_peg_set_dots -getter mp_peg_get_dots \
  -label "Dot Statistics"

workspace::adjuster peg_luminance {
    ball_lum     {float 0.0 1.0 0.05 0.8 "Ball Luminance"}
    surround_lum {float 0.0 1.0 0.05 0.8 "Surround Luminance"}
} -target {} -proc mp_peg_set_luminance -getter mp_peg_get_luminance \
  -label "Per-Region Luminance"

workspace::adjuster peg_duration {
    duration {float 0.4 5.0 0.1 1.5 "Duration (sec)"}
} -target {} -proc mp_peg_set_duration -getter mp_peg_get_duration \
  -label "Trial Duration"

workspace::adjuster peg_shape {
    shape_size {float 0.02 0.5 0.01 0.08 "Aperture Size (fraction of patch)"}
} -target {} -proc mp_peg_set_shape_size -getter mp_peg_get_shape_size \
  -label "Aperture Size"

workspace::adjuster peg_transform -template scale -target patch \
  -label "Scene Size"
