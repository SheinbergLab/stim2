# examples/motionpatch/mp_planko.tcl
# Planko trajectory -> motion-defined dot field
# Demonstrates: a real Box2D ball-drop trajectory driving a motionpatch in
# two visualization modes, plus matched controls for comparison against
# less-realistic motion profiles.
#
# Modes:
#   trajectory_aperture -- aperture follows the ball (x,y) along the
#                          real trajectory; inside dots coherent in
#                          instantaneous velocity direction & magnitude;
#                          outside dots flicker. The textured ball is
#                          visible because the aperture moves AND the
#                          inside motion energy matches its velocity.
#   trajectory_static   -- aperture pinned at center; inside dots
#                          coherent in instantaneous velocity direction
#                          & magnitude. Same momentary motion energy as
#                          the trajectory_aperture condition, but with
#                          no positional translation of the stimulus.
#   matched_speed       -- speed magnitude profile from the trajectory,
#                          but direction held constant (downward).
#   matched_direction   -- direction profile from the trajectory, but
#                          speed held constant.
#   shuffled            -- (vx,vy) samples time-shuffled. Same marginal
#                          velocity distribution, temporal structure
#                          destroyed.
#   aperture_flicker    -- aperture follows (x,y) but inside dots are
#                          incoherent flicker matching the surround
#                          statistics. The ball is segmentable ONLY
#                          via a luminance offset (lum_offset > 0);
#                          isolates first-order positional motion
#                          (translation of a luminance blob through
#                          noise) with no internal motion signal.
#                          With lum_offset = 0 the ball is invisible
#                          (true null / sanity check).
#
# Coordinate / speed conventions follow the prf motionpatch protocol
# (systems/ess/prf/motionpatch). All experimenter-facing values are in
# degrees-visual-angle (dva) and dva/sec; the setup translates to
# motionpatch's internal patch-local-units-per-frame at build time:
#
#   metagroup is scaled by patch_size_dva, so dots in [-0.5, 0.5] span
#       exactly patch_size_dva on screen
#   maskoffset (patch-local) = pos_dva / patch_size_dva
#   motionpatch_speed (patch-local/frame) =
#       v_dva_per_sec / (patch_size_dva * refresh_rate_hz)
#   ndots = dot_density_per_dva2 * patch_size_dva^2
#
# Controls:
#   Down Arrow - drop ball (replay trajectory once)
#   Up Arrow   - generate new board (resimulate)

load_Impro
package require box2d

# ============================================================
# STIM CODE
# ============================================================

proc mp_planko_make_circle_tex {size} {
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
    set tex [shaderImageCreate $pix $size $size linear]
    return $tex
}

# Test world-map texture: a thick rectangular frame around the edges of
# the patch plus a small filled square in the center. Used as a stage-1
# sanity check for the world-map sampler; mp_planko_make_plank_tex
# replaces it for the experiment-grade demos.
proc mp_planko_make_testworld_tex {size} {
    set depth 4
    set img [img_create -width $size -height $size -depth $depth]

    set border [expr {int($size * 0.08)}]
    dl_local ox [dl_flist 0 $size $size 0]
    dl_local oy [dl_flist 0 0 $size $size]
    set p1 [img_drawPolygonFast $img $ox $oy 255 255 255 255]
    set xi0 $border
    set xi1 [expr {$size - $border}]
    dl_local ix [dl_flist $xi0 $xi1 $xi1 $xi0]
    dl_local iy [dl_flist $xi0 $xi0 $xi1 $xi1]
    set p2 [img_drawPolygonFast $p1 $ix $iy 0 0 0 0]

    set cs [expr {int($size * 0.15)}]
    set cx0 [expr {($size - $cs) / 2}]
    set cx1 [expr {$cx0 + $cs}]
    dl_local sx [dl_flist $cx0 $cx1 $cx1 $cx0]
    dl_local sy [dl_flist $cx0 $cx0 $cx1 $cx1]
    set p3 [img_drawPolygonFast $p2 $sx $sy 255 255 255 255]

    dl_local pix [img_imgtolist $p3]
    img_delete $img $p1 $p2 $p3
    set tex [shaderImageCreate $pix $size $size linear]
    return $tex
}

# Rasterize the accepted planks into an alpha texture used as tex1 by
# both motionpatches. Static per-trial: we generate it once when a
# board is accepted, then it stays fixed in the patch frame for the
# whole drop. If/when planks become interactive (pinball paddles
# moving with subject input, per-frame world updates), this is the
# place to graduate to a GPU FBO render or Blend2D pipeline. For
# planko's 10-rectangle worlds, regenerated once per trial, Impro is
# fast enough.
#
# Coordinate mapping: world dva (x, y) -> texture pixel (px, py)
#   px = (x + patch_size/2) / patch_size * size
#   py = (patch_size/2 - y) / patch_size * size   (y-flip; CImg's
#                                                  pixel 0 is top, but
#                                                  the shader samples
#                                                  texcoord.t flipped,
#                                                  so high y_world ends
#                                                  up at top of patch)
proc mp_planko_world_to_pixel {x_dva y_dva size} {
    set ps $::mp_planko::patch_size
    set px [expr {($x_dva + $ps / 2.0) / $ps * $size}]
    set py [expr {($ps / 2.0 - $y_dva) / $ps * $size}]
    return [list $px $py]
}

proc mp_planko_make_plank_tex {size} {
    set depth 4
    set img [img_create -width $size -height $size -depth $depth]
    set polys [list]
    set cur $img

    # Each layer is rasterized into its own RGBA channel via
    # img_drawPolygonChannel, which uses CImg's per-channel shared
    # views to leave non-target channels untouched. Layer order is
    # therefore irrelevant -- adding a third or fourth overlapping
    # layer (e.g. choice zones, paddles) just adds another set of
    # calls with a different mask, no sequencing care needed.
    #
    # Channel assignment for this demo:
    #   A (mask 8) = planks
    #   B (mask 4) = frame
    # R and G are reserved for future layers.

    # Frame: outer rectangle fills B=255 across the whole image, then
    # the inset rectangle clears the interior B back to 0, leaving a
    # clean rectangular ring. Both writes target only the B channel.
    dl_local ox [dl_flist 0 $size $size 0]
    dl_local oy [dl_flist 0 0 $size $size]
    set cur [img_drawPolygonChannel $cur $ox $oy 0 0 255 0 4]
    lappend polys $cur

    set border [expr {int($size * 0.04)}]
    set xi0 $border
    set xi1 [expr {$size - $border}]
    dl_local ix [dl_flist $xi0 $xi1 $xi1 $xi0]
    dl_local iy [dl_flist $xi0 $xi0 $xi1 $xi1]
    set cur [img_drawPolygonChannel $cur $ix $iy 0 0 0 0 4]
    lappend polys $cur

    # Planks: alpha-channel only.
    set n [llength $::mp_planko::plank_tx]
    for {set i 0} {$i < $n} {incr i} {
        set tx  [lindex $::mp_planko::plank_tx    $i]
        set ty  [lindex $::mp_planko::plank_ty    $i]
        set sx  [lindex $::mp_planko::plank_sx    $i]
        set sy  [lindex $::mp_planko::plank_sy    $i]
        set ang [lindex $::mp_planko::plank_angle $i]

        set hx [expr {$sx / 2.0}]
        set hy [expr {$sy / 2.0}]
        set ca [expr {cos($ang)}]
        set sa [expr {sin($ang)}]

        set xs {}
        set ys {}
        foreach pt {-1 1 1 -1} qt {-1 -1 1 1} {
            set lx [expr {$pt * $hx}]
            set ly [expr {$qt * $hy}]
            set wx [expr {$tx + $ca * $lx - $sa * $ly}]
            set wy [expr {$ty + $sa * $lx + $ca * $ly}]
            lassign [mp_planko_world_to_pixel $wx $wy $size] px py
            lappend xs $px
            lappend ys $py
        }
        dl_local pxl [dl_flist {*}$xs]
        dl_local pyl [dl_flist {*}$ys]
        set cur [img_drawPolygonChannel $cur $pxl $pyl 0 0 0 255 8]
        lappend polys $cur
    }

    dl_local pix [img_imgtolist $cur]
    img_delete $img {*}$polys
    set tex [shaderImageCreate $pix $size $size linear]
    return $tex
}

# ---------- Box2D simulation (planko-style) ----------

proc mp_planko_make_world {} {
    set n        $::mp_planko::nplanks
    set xrange   $::mp_planko::xrange
    set yrange   $::mp_planko::yrange
    set xrange_2 [expr {$xrange / 2.0}]
    set yrange_2 [expr {$yrange / 2.0}]

    set g [dg_create]
    dl_set $g:name [dl_paste [dl_repeat [dl_slist plank] $n] [dl_fromto 0 $n]]
    dl_set $g:shape [dl_repeat [dl_slist Box] $n]
    dl_set $g:type [dl_repeat 0 $n]
    dl_set $g:tx [dl_sub [dl_mult $xrange [dl_urand $n]] $xrange_2]
    dl_set $g:ty [dl_sub [dl_mult $yrange [dl_urand $n]] $yrange_2]
    dl_set $g:sx [dl_repeat $::mp_planko::plank_width $n]
    dl_set $g:sy [dl_repeat 0.5 $n]
    dl_set $g:angle [dl_mult 2 $::pi [dl_urand $n]]
    dl_set $g:restitution [dl_repeat $::mp_planko::restitution $n]

    # Walls: floor + left + right. Containing the ball means every
    # trajectory terminates at the floor and never drifts off-screen.
    set wt $::mp_planko::wall_thickness
    set floor_y $::mp_planko::catcher_y
    set wall_h  [expr {abs($::mp_planko::ball_start_y - $floor_y) + 2.0}]
    set wall_cy [expr {($::mp_planko::ball_start_y + $floor_y) / 2.0}]

    set w [dg_create]
    dl_set $w:name [dl_slist floor wall_l wall_r]
    dl_set $w:shape [dl_repeat [dl_slist Box] 3]
    dl_set $w:type [dl_repeat 0 3]
    dl_set $w:tx [dl_flist 0.0 [expr {-$xrange_2 - $wt/2.0}] \
                                [expr { $xrange_2 + $wt/2.0}]]
    dl_set $w:ty [dl_flist $floor_y $wall_cy $wall_cy]
    dl_set $w:sx [dl_flist [expr {$xrange + 2.0*$wt}] $wt $wt]
    dl_set $w:sy [dl_flist $wt $wall_h $wall_h]
    dl_set $w:angle [dl_zeros 3.0]
    dl_set $w:restitution [dl_repeat $::mp_planko::wall_restitution 3]

    dg_append $g $w
    dg_delete $w

    set b [dg_create]
    dl_set $b:name [dl_slist ball]
    dl_set $b:shape [dl_slist Circle]
    dl_set $b:type 0
    dl_set $b:tx [dl_flist 0]
    dl_set $b:ty [dl_flist $::mp_planko::ball_start_y]
    dl_set $b:sx [dl_flist $::mp_planko::ball_radius]
    dl_set $b:sy [dl_flist $::mp_planko::ball_radius]
    dl_set $b:angle [dl_flist 0.0]
    dl_set $b:restitution [dl_flist $::mp_planko::restitution]

    dg_append $g $b
    dg_delete $b
    return $g
}

proc mp_planko_build_sim {dg} {
    set world [box2d::createWorld]
    set n [dl_length $dg:name]
    set ball ""
    for {set i 0} {$i < $n} {incr i} {
        foreach v {name shape type tx ty sx sy angle restitution} {
            set $v [dl_get $dg:$v $i]
        }
        if {$shape eq "Box"} {
            set body [box2d::createBox $world $name $type $tx $ty $sx $sy $angle]
        } else {
            set body [box2d::createCircle $world $name $type $tx $ty $sx]
        }
        box2d::setRestitution $world $body $restitution
        if {$name eq "ball"} { set ball $body }
    }
    return [list $world $ball]
}

proc mp_planko_run_sim {world ball} {
    set step [expr {[screen_set FrameDuration] / 1000.0}]
    box2d::setBodyType $world $ball 2

    set ts {}
    set xs {}
    set ys {}
    set contacts {}
    set simtime $::mp_planko::sim_duration

    # Settle detection: stop once the ball has been close to floor
    # AND nearly stationary for a small consecutive window. Bounded
    # trajectories with walls always terminate this way well before
    # sim_duration, keeping clips short and confined.
    set settle_frames 0
    set settle_thresh_v 0.5  ;# dva/sec
    set settle_thresh_y [expr {$::mp_planko::catcher_y \
                                + $::mp_planko::wall_thickness \
                                + $::mp_planko::ball_radius + 0.5}]

    set last_x ""
    set last_y ""
    for {set t 0} {$t < $simtime} {set t [expr {$t + $step}]} {
        box2d::step $world $step
        if {[box2d::getContactBeginEventCount $world]} {
            lappend contacts [box2d::getContactBeginEvents $world]
        }
        lassign [box2d::getBodyInfo $world $ball] tx ty a
        lappend ts $t
        lappend xs $tx
        lappend ys $ty

        if {$last_x ne ""} {
            set vx [expr {($tx - $last_x) / $step}]
            set vy [expr {($ty - $last_y) / $step}]
            set v  [expr {hypot($vx, $vy)}]
            if {$ty < $settle_thresh_y && $v < $settle_thresh_v} {
                incr settle_frames
                if {$settle_frames > 8} { break }
            } else {
                set settle_frames 0
            }
        }
        set last_x $tx
        set last_y $ty
    }
    return [list $ts $xs $ys $step $contacts]
}

# Count unique plank bodies that the ball contacted during the sim.
proc mp_planko_count_plank_hits {contacts} {
    set seen {}
    foreach frame $contacts {
        foreach event $frame {
            foreach body $event {
                if {[string match "plank*" $body] && $body ni $seen} {
                    lappend seen $body
                }
            }
        }
    }
    return [llength $seen]
}

# Compute (vx, vy) per sample by central differences, in dva/sec.
proc mp_planko_compute_velocities {ts xs ys} {
    set n [llength $ts]
    set vxs {}
    set vys {}
    for {set i 0} {$i < $n} {incr i} {
        if {$i == 0} {
            set i0 0; set i1 1
        } elseif {$i == $n - 1} {
            set i0 [expr {$n - 2}]; set i1 [expr {$n - 1}]
        } else {
            set i0 [expr {$i - 1}]; set i1 [expr {$i + 1}]
        }
        set dt [expr {[lindex $ts $i1] - [lindex $ts $i0]}]
        if {$dt <= 0} { set dt 1.0 }
        lappend vxs [expr {([lindex $xs $i1] - [lindex $xs $i0]) / $dt}]
        lappend vys [expr {([lindex $ys $i1] - [lindex $ys $i0]) / $dt}]
    }
    return [list $vxs $vys]
}

proc mp_planko_simulate_trajectory {} {
    set max_retries $::mp_planko::max_world_retries
    set min_planks  $::mp_planko::min_planks
    set ts {}; set xs {}; set ys {}; set dt 0.0; set nhit 0
    set accepted_dg ""
    for {set attempt 0} {$attempt < $max_retries} {incr attempt} {
        box2d::destroy all
        if {$accepted_dg ne ""} { dg_delete $accepted_dg; set accepted_dg "" }
        set dg [mp_planko_make_world]
        lassign [mp_planko_build_sim $dg] world ball
        lassign [mp_planko_run_sim $world $ball] ts xs ys dt contacts
        set nhit [mp_planko_count_plank_hits $contacts]
        box2d::destroy all
        if {$nhit >= $min_planks} { set accepted_dg $dg; break }
        dg_delete $dg
    }
    set ::mp_planko::traj_nhit $nhit

    # Extract plank geometry from the accepted world for the world-map
    # rasterizer. Walls and the ball are skipped; only entries whose
    # name starts with "plank" become world-map geometry.
    set ::mp_planko::plank_tx    {}
    set ::mp_planko::plank_ty    {}
    set ::mp_planko::plank_sx    {}
    set ::mp_planko::plank_sy    {}
    set ::mp_planko::plank_angle {}
    if {$accepted_dg ne ""} {
        set ndg [dl_length $accepted_dg:name]
        for {set i 0} {$i < $ndg} {incr i} {
            set nm [dl_get $accepted_dg:name $i]
            if {![string match plank* $nm]} continue
            lappend ::mp_planko::plank_tx    [dl_get $accepted_dg:tx    $i]
            lappend ::mp_planko::plank_ty    [dl_get $accepted_dg:ty    $i]
            lappend ::mp_planko::plank_sx    [dl_get $accepted_dg:sx    $i]
            lappend ::mp_planko::plank_sy    [dl_get $accepted_dg:sy    $i]
            lappend ::mp_planko::plank_angle [dl_get $accepted_dg:angle $i]
        }
        dg_delete $accepted_dg
    }

    lassign [mp_planko_compute_velocities $ts $xs $ys] vxs vys

    set ::mp_planko::traj_t  $ts
    set ::mp_planko::traj_x  $xs
    set ::mp_planko::traj_y  $ys
    set ::mp_planko::traj_vx $vxs
    set ::mp_planko::traj_vy $vys
    set ::mp_planko::traj_dt $dt
    set ::mp_planko::traj_n  [llength $ts]

    # Time-shuffled (vx,vy) for the shuffled mode.
    set order [list]
    for {set i 0} {$i < [llength $vxs]} {incr i} { lappend order $i }
    set src $order
    for {set i [expr {[llength $src] - 1}]} {$i > 0} {incr i -1} {
        set j [expr {int(rand() * ($i + 1))}]
        set tmp [lindex $src $i]
        set src [lreplace $src $i $i [lindex $src $j]]
        set src [lreplace $src $j $j $tmp]
    }
    set ::mp_planko::shuffle_idx $src
}

# ---------- per-frame driver ----------

proc mp_planko_index_for_time {tsec} {
    set dt $::mp_planko::traj_dt
    set n  $::mp_planko::traj_n
    if {$dt <= 0 || $n <= 0} { return 0 }
    set idx [expr {int($tsec / $dt)}]
    if {$idx < 0}      { return 0 }
    if {$idx >= $n}    { return [expr {$n - 1}] }
    return $idx
}

# Convert dva/sec -> motionpatch_speed (patch-local units / frame),
# matching the prf motionpatch protocol convention:
#   speed = v_deg_sec / (patch_size_dva * refresh_rate_hz)
proc mp_planko_speed_from_deg_sec {v_deg_sec} {
    set ps $::mp_planko::patch_size
    set rr $::mp_planko::refresh_rate
    if {$ps <= 0 || $rr <= 0} { return 0.0 }
    return [expr {$v_deg_sec / ($ps * $rr)}]
}

proc mp_planko_update {} {
    set t [expr {$::StimTime / 1000.0}]
    set dt [expr {$t - $::mp_planko::last_t}]
    if {$dt < 0.0 || $dt > 0.1} { set dt 0.016 }
    if {!$::mp_planko::playing} { set dt 0.0 }
    set ::mp_planko::last_t $t

    if {!$::mp_planko::dropping} { return }

    set ::mp_planko::play_t [expr {$::mp_planko::play_t + $dt}]
    set tplay $::mp_planko::play_t
    if {$tplay > [expr {$::mp_planko::traj_dt * ($::mp_planko::traj_n - 1)}]} {
        set ::mp_planko::dropping 0
    }
    set i [mp_planko_index_for_time $tplay]

    set x  [lindex $::mp_planko::traj_x  $i]
    set y  [lindex $::mp_planko::traj_y  $i]
    set vx [lindex $::mp_planko::traj_vx $i]
    set vy [lindex $::mp_planko::traj_vy $i]

    if {$::mp_planko::mode eq "shuffled"} {
        set j [lindex $::mp_planko::shuffle_idx $i]
        if {$j ne ""} {
            set vx [lindex $::mp_planko::traj_vx $j]
            set vy [lindex $::mp_planko::traj_vy $j]
        }
    }

    set ps $::mp_planko::patch_size
    set ox [expr {$x / $ps}]
    set oy [expr {$y / $ps}]

    set vmag [expr {hypot($vx, $vy)}]
    if {$vmag < 1e-6} {
        set dir 0.0
    } else {
        set dir [expr {atan2($vy, $vx)}]
    }
    set v_clamped $vmag
    if {$v_clamped > $::mp_planko::max_speed_deg_sec} {
        set v_clamped $::mp_planko::max_speed_deg_sec
    }
    set speed [mp_planko_speed_from_deg_sec $v_clamped]

    switch -- $::mp_planko::mode {
        trajectory_aperture {
            motionpatch_maskoffset dots_target $ox $oy
            motionpatch_maskoffset dots_bg     $ox $oy
            motionpatch_direction  dots_target $dir
            motionpatch_speed      dots_target $speed
        }
        aperture_flicker {
            # Aperture translates along the trajectory; inside dots
            # are flicker noise matching the surround. Segmentation
            # depends entirely on lum_offset.
            motionpatch_maskoffset dots_target $ox $oy
            motionpatch_maskoffset dots_bg     $ox $oy
        }
        trajectory_static {
            motionpatch_maskoffset dots_target 0.0 0.0
            motionpatch_maskoffset dots_bg     0.0 0.0
            motionpatch_direction  dots_target $dir
            motionpatch_speed      dots_target $speed
        }
        matched_speed {
            motionpatch_maskoffset dots_target 0.0 0.0
            motionpatch_maskoffset dots_bg     0.0 0.0
            motionpatch_direction  dots_target [expr {-3.14159265 / 2.0}]
            motionpatch_speed      dots_target $speed
        }
        matched_direction {
            motionpatch_maskoffset dots_target 0.0 0.0
            motionpatch_maskoffset dots_bg     0.0 0.0
            motionpatch_direction  dots_target $dir
            motionpatch_speed      dots_target \
                [mp_planko_speed_from_deg_sec $::mp_planko::const_speed_deg_sec]
        }
        shuffled {
            motionpatch_maskoffset dots_target 0.0 0.0
            motionpatch_maskoffset dots_bg     0.0 0.0
            motionpatch_direction  dots_target $dir
            motionpatch_speed      dots_target $speed
        }
    }
}

# ---------- mode application ----------

proc mp_planko_apply_mode {mode} {
    set ::mp_planko::mode $mode
    set bg_life $::mp_planko::bg_lifetime
    set bg_coh  $::mp_planko::bg_coherence
    set bg_rad  [expr {$::mp_planko::bg_direction_deg * 3.14159265 / 180.0}]
    set bg_sp   [mp_planko_speed_from_deg_sec $::mp_planko::bg_speed_deg_sec]

    if {$mode eq "aperture_flicker"} {
        # Match surround stats so segmentation depends on lum_offset.
        # Target inherits bg coherence/direction/speed so the inside-
        # vs-outside boundary stays statistically invisible without
        # luminance offset.
        motionpatch_coherence dots_target $bg_coh
        motionpatch_lifetime  dots_target $bg_life
        motionpatch_direction dots_target $bg_rad
        motionpatch_speed     dots_target $bg_sp
    } else {
        motionpatch_coherence dots_target 1.0
        motionpatch_lifetime  dots_target 30
    }
    motionpatch_coherence dots_bg     $bg_coh
    motionpatch_lifetime  dots_bg     $bg_life
    motionpatch_direction dots_bg     $bg_rad
    motionpatch_speed     dots_bg     $bg_sp

    if {$mode ne "trajectory_aperture" && $mode ne "aperture_flicker"} {
        motionpatch_maskoffset dots_target 0.0 0.0
        motionpatch_maskoffset dots_bg     0.0 0.0
    }

    mp_planko_apply_luminance
}

# Apply current ::mp_planko::lum_offset symmetrically around 0.8:
# inside gets +offset/2, outside gets -offset/2. lum_offset = 0 means
# motion-only definition (luminance-matched, ball invisible without
# motion); > 0 makes the aperture brighter than surround (visible as a
# luminance blob even with no internal motion).
proc mp_planko_apply_luminance {} {
    set base 0.8
    set off  $::mp_planko::lum_offset
    set lin  [expr {$base + $off / 2.0}]
    set lout [expr {$base - $off / 2.0}]
    motionpatch_color dots_target $lin  $lin  $lin  1.0
    motionpatch_color dots_bg     $lout $lout $lout 1.0
}

# ---------- setup ----------

# patch_size_dva = total dva spanned by the metagroup (must contain the
#   trajectory's xrange/yrange with margin)
# dot_density   = dots per dva^2 (per patch)
#
# Aperture diameter is auto-bound to the ball diameter: shape_size =
# (2 * ball_radius_dva) / patch_size_dva. The shape_size adjuster is
# kept as a manual override (e.g. to deliberately enlarge the aperture
# for context-leakage controls); ↑ reset re-syncs it to the ball.
# Position the aperture at the trajectory start (zero velocity) so a
# fresh trial visibly begins with the ball at its launch point rather
# than wherever the last drop ended. Idempotent and safe before the
# motionpatches exist (catch absorbs the missing-objname errors).
proc mp_planko_position_at_start {} {
    set ps $::mp_planko::patch_size
    if {$ps <= 0 || $::mp_planko::traj_n == 0} { return }
    set x0 [lindex $::mp_planko::traj_x 0]
    set y0 [lindex $::mp_planko::traj_y 0]
    if {$x0 eq "" || $y0 eq ""} { return }
    set ox [expr {$x0 / $ps}]
    set oy [expr {$y0 / $ps}]
    catch {
        motionpatch_maskoffset dots_target $ox $oy
        motionpatch_maskoffset dots_bg     $ox $oy
        motionpatch_speed      dots_target 0.0
    }
}

# Generate a fresh world-map texture from the current plank list and
# rebind it on both motionpatches. The previous shaderImage texture is
# orphaned (no shaderImageDelete API) but the leak is small (~1 MB at
# 512x512 RGBA) and bounded by the number of resets per session.
proc mp_planko_refresh_world_tex {} {
    if {[info commands motionpatch_setSampler] eq ""} { return }
    set tex   [mp_planko_make_plank_tex $::mp_planko::world_tex_size]
    set texID [shaderImageID $tex]
    catch {
        motionpatch_setSampler dots_target $texID 1
        motionpatch_setSampler dots_bg     $texID 1
    }
}

proc mp_planko_match_aperture_to_ball {} {
    set d [expr {2.0 * $::mp_planko::ball_radius}]
    set sz [expr {$d / $::mp_planko::patch_size}]
    set ::mp_planko::shape_size $sz
    if {[info commands motionpatch_maskscale] ne ""} {
        catch {
            motionpatch_maskscale dots_target $sz
            motionpatch_maskscale dots_bg     $sz
        }
    }
    return $sz
}

proc mp_planko_setup {patch_size_dva dot_density} {
    glistInit 1
    resetObjList
    shaderImageReset

    namespace eval ::mp_planko {
        variable mode             trajectory_aperture
        variable playing          1
        variable dropping         0
        variable last_t           0.0
        variable play_t           0.0

        variable nplanks            10
        variable plank_width        3.0
        variable ball_radius        0.5
        variable restitution        0.2
        variable xrange             16.0
        variable yrange             12.0
        variable catcher_y          -7.5
        variable ball_start_y       8.0
        variable sim_duration       6.0
        variable wall_thickness     0.5
        variable wall_restitution   0.2
        variable min_planks         3
        variable max_world_retries  100
        variable traj_nhit          0

        variable patch_size       20.0
        variable refresh_rate     60.0
        variable max_speed_deg_sec   30.0
        variable const_speed_deg_sec 5.0
        variable shape_size       0.15
        variable lum_offset       0.0
        variable bg_lifetime      8
        variable bg_speed_deg_sec 5.0
        variable bg_coherence     0.0
        variable bg_direction_deg 90.0
        variable world_mode_bg     0
        variable world_mode_target 0
        variable world_dim         0.25

        variable traj_t           {}
        variable traj_x           {}
        variable traj_y           {}
        variable traj_vx          {}
        variable traj_vy          {}
        variable traj_dt          0.0
        variable traj_n           0
        variable shuffle_idx      {}

        variable plank_tx         {}
        variable plank_ty         {}
        variable plank_sx         {}
        variable plank_sy         {}
        variable plank_angle      {}
        variable world_tex_size   512

        # Saved setup args so reset can re-invoke setup cleanly.
        variable last_patch_size  13.0
        variable last_dot_density 24.0
    }

    set ::mp_planko::patch_size       $patch_size_dva
    set ::mp_planko::last_patch_size  $patch_size_dva
    set ::mp_planko::last_dot_density $dot_density

    # Bind aperture size to ball diameter (overrides the namespace default).
    mp_planko_match_aperture_to_ball

    # Refresh rate from screen settings (Hz). Falls back to 60 if
    # FrameDuration is unavailable / zero.
    set fd [screen_set FrameDuration]
    if {$fd > 0} {
        set ::mp_planko::refresh_rate [expr {1000.0 / $fd}]
    } else {
        set ::mp_planko::refresh_rate 60.0
    }

    # The mask is an alpha filter applied at the fragment shader stage,
    # so dots are distributed across the entire patch and clipped at
    # render time. Both patches therefore use the same total ndots =
    # dot_density * patch_area; the visibly-rendered density inside the
    # aperture and outside the aperture is then both equal to
    # dot_density (uniform across the patch).
    set nDots [expr {int($dot_density * $patch_size_dva * $patch_size_dva)}]
    if {$nDots < 100} { set nDots 100 }

    mp_planko_simulate_trajectory

    set texSize 256
    set tex   [mp_planko_make_circle_tex $texSize]
    set texID [shaderImageID $tex]

    set worldTex   [mp_planko_make_plank_tex $::mp_planko::world_tex_size]
    set worldTexID [shaderImageID $worldTex]

    set color  0.8
    set ptSize 3.0
    set initialSpeed [mp_planko_speed_from_deg_sec \
                          $::mp_planko::const_speed_deg_sec]

    set mg [metagroup]
    objName $mg patch

    set mp_bg [motionpatch $nDots $initialSpeed 30]
    objName $mp_bg dots_bg
    motionpatch_pointsize $mp_bg $ptSize
    motionpatch_color $mp_bg $color $color $color 1.0
    motionpatch_masktype $mp_bg 0
    motionpatch_coherence $mp_bg 0.0
    motionpatch_lifetime $mp_bg 2
    motionpatch_direction $mp_bg 0.0
    motionpatch_speed $mp_bg $initialSpeed
    motionpatch_setSampler $mp_bg $texID 0
    motionpatch_samplermaskmode $mp_bg 2
    motionpatch_maskscale $mp_bg $::mp_planko::shape_size
    motionpatch_setSampler $mp_bg $worldTexID 1
    # Layer A (alpha) = planks. world_mode_bg drives this via the
    # backward-compat motionpatch_worldmaskmode command.
    motionpatch_worldmaskmode $mp_bg $::mp_planko::world_mode_bg
    motionpatch_worlddim $mp_bg $::mp_planko::world_dim
    motionpatch_worldcolor $mp_bg 1.0 0.5 0.2 1.0
    # Layer B (blue channel) = frame. Tinted yellow so it's visually
    # distinct from the planks. Demonstrates that two channels of the
    # same world-map texture can be styled independently.
    motionpatch_layermode  $mp_bg B 2
    motionpatch_layercolor $mp_bg B 0.95 0.85 0.2 1.0
    metagroupAdd $mg $mp_bg

    set mp_tg [motionpatch $nDots $initialSpeed 30]
    objName $mp_tg dots_target
    motionpatch_pointsize $mp_tg $ptSize
    motionpatch_color $mp_tg $color $color $color 1.0
    motionpatch_masktype $mp_tg 0
    motionpatch_coherence $mp_tg 1.0
    motionpatch_lifetime $mp_tg 30
    motionpatch_direction $mp_tg 0.0
    motionpatch_speed $mp_tg $initialSpeed
    motionpatch_setSampler $mp_tg $texID 0
    motionpatch_samplermaskmode $mp_tg 1
    motionpatch_maskscale $mp_tg $::mp_planko::shape_size
    # Target (inside-aperture / "ball") world mode is independent of
    # surround. Default 0 = ball on top of world; non-zero = ball is
    # also affected by the world map (e.g., gets occluded behind
    # planks). Useful for tracking/pursuit-during-occlusion studies.
    motionpatch_setSampler $mp_tg $worldTexID 1
    # Target alpha layer (planks) -- on by default 0 so the ball
    # renders on top. Set world_mode_target non-zero for occlusion.
    motionpatch_worldmaskmode $mp_tg $::mp_planko::world_mode_target
    motionpatch_worlddim $mp_tg $::mp_planko::world_dim
    motionpatch_worldcolor $mp_tg 1.0 0.5 0.2 1.0
    # Target B layer (frame) is left at mode 0 -- the frame should
    # only modulate the surround, not the ball, so the ball passes
    # cleanly across the framed boundary.
    metagroupAdd $mg $mp_tg

    addPreScript $mp_bg mp_planko_update

    # Metagroup span = patch_size_dva. dots in [-0.5,0.5] span this.
    scaleObj $mg $::mp_planko::patch_size $::mp_planko::patch_size

    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1

    mp_planko_apply_mode $::mp_planko::mode
    mp_planko_position_at_start
    redraw
}

# ---------- actions ----------

proc mp_planko_trigger {action} {
    switch -- $action {
        drop {
            set ::mp_planko::play_t   0.0
            set ::mp_planko::dropping 1
        }
        reset {
            mp_planko_simulate_trajectory
            mp_planko_match_aperture_to_ball
            mp_planko_refresh_world_tex
            set ::mp_planko::dropping 0
            mp_planko_position_at_start
        }
    }
    return
}

# ---------- adjusters ----------

proc mp_planko_set_mode {mode} { mp_planko_apply_mode $mode }
proc mp_planko_get_mode {} { dict create mode trajectory_aperture }

proc mp_planko_set_world {nplanks plank_width ball_radius min_planks} {
    set ::mp_planko::nplanks     $nplanks
    set ::mp_planko::plank_width $plank_width
    set ::mp_planko::ball_radius $ball_radius
    set ::mp_planko::min_planks  $min_planks
}
proc mp_planko_get_world {} {
    dict create nplanks 10 plank_width 3.0 ball_radius 0.5 min_planks 3
}

# Speed parameters in dva/sec, matching prf protocol convention.
# max clamps the instantaneous trajectory speed (rare large-spike
# kinematic glitches won't blow out the stimulus).
proc mp_planko_set_speeds {max_deg_sec const_deg_sec} {
    set ::mp_planko::max_speed_deg_sec   $max_deg_sec
    set ::mp_planko::const_speed_deg_sec $const_deg_sec
}
proc mp_planko_get_speeds {} {
    dict create max_deg_sec 30.0 const_deg_sec 5.0
}

proc mp_planko_set_luminance {lum_offset} {
    set ::mp_planko::lum_offset $lum_offset
    mp_planko_apply_luminance
}
proc mp_planko_get_luminance {} { dict create lum_offset 0.0 }

proc mp_planko_set_bg_lifetime {bg_lifetime} {
    set ::mp_planko::bg_lifetime $bg_lifetime
    mp_planko_apply_mode $::mp_planko::mode
}
proc mp_planko_get_bg_lifetime {} { dict create bg_lifetime 8 }

# Surround motion controls. With coherence=0 (default) the surround is
# pure flicker noise; raising coherence makes the surround stream in
# bg_direction_deg at bg_speed_deg_sec, which can substantially
# improve ball visibility because the ball's instantaneous velocity
# stands out against a coherent baseline (relative-motion contrast).
# E.g. coherence=1.0 + direction=90 (upward) makes a falling ball
# pop visually because |v_ball - v_bg| is larger than |v_ball|.
proc mp_planko_set_bg_motion {speed coherence direction} {
    set ::mp_planko::bg_speed_deg_sec $speed
    set ::mp_planko::bg_coherence     $coherence
    set ::mp_planko::bg_direction_deg $direction
    mp_planko_apply_mode $::mp_planko::mode
}
proc mp_planko_get_bg_motion {} {
    dict create speed 5.0 coherence 0.0 direction 90.0
}

# World-map (tex1) modulation. mode_bg controls whether the SURROUND
# (mp_bg) is modulated by the world map; mode_target controls whether
# the BALL (mp_tg) is modulated. Ball-on-top:    mode_target = 0
# Ball-occluded: mode_target = same as mode_bg.
# Modes per channel: 0 = off, 1 = dim, 2 = tint, 3 = hide.
proc mp_planko_set_worldmap {mode_bg mode_target dim} {
    set ::mp_planko::world_mode_bg     $mode_bg
    set ::mp_planko::world_mode_target $mode_target
    set ::mp_planko::world_dim         $dim
    catch {
        motionpatch_worldmaskmode dots_bg     $mode_bg
        motionpatch_worldmaskmode dots_target $mode_target
        motionpatch_worlddim      dots_target $dim
        motionpatch_worlddim      dots_bg     $dim
    }
}
proc mp_planko_get_worldmap {} {
    dict create mode_bg 0 mode_target 0 dim 0.25
}

proc mp_planko_set_shape_size {shape_size} {
    set ::mp_planko::shape_size $shape_size
    motionpatch_maskscale dots_target $shape_size
    motionpatch_maskscale dots_bg     $shape_size
}
proc mp_planko_get_shape_size {} { dict create shape_size 0.15 }

proc mp_planko_set_softness {softness} {
    motionpatch_masksoftness dots_target $softness
    motionpatch_masksoftness dots_bg     $softness
}
proc mp_planko_get_softness {} { dict create softness 0.0 }

proc mp_planko_set_freeze {frozen} {
    if {$frozen} {
        motionpatch_speed dots_target 0.0
        motionpatch_speed dots_bg     0.0
        motionpatch_refreshPositions dots_target
        motionpatch_refreshPositions dots_bg
        set ::mp_planko::playing 0
    } else {
        set ::mp_planko::playing 1
    }
}
proc mp_planko_get_freeze {} { dict create frozen 0 }

# ---------- keyboard ----------

proc onDownArrow {} { mp_planko_trigger drop }
proc onUpArrow   {} { mp_planko_trigger reset }

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup mp_planko_setup {
    patch_size_dva {float 8.0 32.0 1.0 13.0 "Patch Size (dva)"}
    dot_density    {float 0.5 100.0 0.5 24.0 "Dot Density (dots/dva^2)"}
} -adjusters {planko_actions planko_mode planko_world planko_speeds planko_luminance planko_lifetime planko_bg_motion planko_worldmap planko_shape planko_softness planko_freeze planko_transform} \
  -label "Motion Planko"

workspace::adjuster planko_actions {
    drop  {action "Drop Ball (↓)"}
    reset {action "New Trajectory (↑)"}
} -target {} -proc mp_planko_trigger \
  -label "Actions"

workspace::adjuster planko_mode {
    mode {choice {trajectory_aperture trajectory_static aperture_flicker matched_speed matched_direction shuffled} trajectory_aperture "Mode"}
} -target {} -proc mp_planko_set_mode -getter mp_planko_get_mode \
  -label "Visualization Mode"

workspace::adjuster planko_world {
    nplanks      {int 3 30 1 10 "Number of Planks"}
    plank_width  {float 1.0 6.0 0.5 3.0 "Plank Width (dva)"}
    ball_radius  {float 0.2 1.5 0.1 0.5 "Ball Radius (dva)"}
    min_planks   {int 0 10 1 3 "Min Plank Hits (acceptance)"}
} -target {} -proc mp_planko_set_world -getter mp_planko_get_world \
  -label "World (apply by ↑ reset)"

workspace::adjuster planko_speeds {
    max_deg_sec   {float 5.0 80.0 1.0 30.0 "Max Trajectory Speed (dva/sec)"}
    const_deg_sec {float 0.5 20.0 0.5 5.0  "Constant Speed (dva/sec)"}
} -target {} -proc mp_planko_set_speeds -getter mp_planko_get_speeds \
  -label "Speed (dva/sec)"

workspace::adjuster planko_luminance {
    lum_offset {float -0.4 0.4 0.05 0.0 "Luminance Offset (in - out)"}
} -target {} -proc mp_planko_set_luminance -getter mp_planko_get_luminance \
  -label "Luminance Offset"

workspace::adjuster planko_lifetime {
    bg_lifetime {int 1 30 1 8 "Background Lifetime (frames)"}
} -target {} -proc mp_planko_set_bg_lifetime -getter mp_planko_get_bg_lifetime \
  -label "Background Lifetime"

# Surround motion. coherence 0 = pure flicker noise (default; matches
# previous demo behavior). coherence > 0 makes the surround stream in
# direction at speed (dva/sec); the ball is then visible by relative-
# motion contrast against this baseline. Direction 90 = upward,
# 270 = downward, 0 = rightward, 180 = leftward.
workspace::adjuster planko_bg_motion {
    speed     {float 0.0 30.0 0.5 5.0 "Surround Speed (dva/sec)"}
    coherence {float 0.0 1.0 0.05 0.0 "Surround Coherence"}
    direction {float 0.0 360.0 15.0 90.0 "Surround Direction (deg)"}
} -target {} -proc mp_planko_set_bg_motion -getter mp_planko_get_bg_motion \
  -label "Surround Motion"

# Stage-1 test of the second sampler. Mode 0 leaves the world map
# inert; mode 1 dims dots that fall over the test pattern (frame +
# central square); mode 2 tints them with the world color; mode 3
# hides them. Once this works, the test texture is replaced by a
# plank rasterization from world_dg.
workspace::adjuster planko_worldmap {
    mode_bg     {choice {0 1 2 3} 0 "Surround Mode (0=off 1=dim 2=tint 3=hide)"}
    mode_target {choice {0 1 2 3} 0 "Ball Mode (0=on top 1=dim 2=tint 3=hide)"}
    dim         {float 0.0 1.0 0.05 0.25 "Dim Factor (mode 1)"}
} -target {} -proc mp_planko_set_worldmap -getter mp_planko_get_worldmap \
  -label "World Map (Test Pattern)"

workspace::adjuster planko_shape {
    shape_size {float 0.05 0.5 0.01 0.15 "Aperture Size (fraction of patch)"}
} -target {} -proc mp_planko_set_shape_size -getter mp_planko_get_shape_size \
  -label "Aperture Size"

workspace::adjuster planko_softness {
    softness {float 0.0 1.0 0.02 0.0 "Edge Softness"}
} -target {} -proc mp_planko_set_softness -getter mp_planko_get_softness \
  -label "Aperture Softness"

workspace::adjuster planko_freeze {
    frozen {choice {0 1} 0 "Frozen"}
} -target {} -proc mp_planko_set_freeze -getter mp_planko_get_freeze \
  -label "Freeze / Play"

workspace::adjuster planko_transform -template scale -target patch \
  -label "Scene Size"
