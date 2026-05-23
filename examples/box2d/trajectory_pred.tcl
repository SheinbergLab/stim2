# examples/box2d/trajectory_pred.tcl
# Trajectory Prediction / Control
#
# Monkey-task demo built on Box2D. A ball is launched under gravity
# (optionally deflecting off an inverted-V plank). The trajectory is
# pre-simulated for rejection sampling, then re-run with live physics.
#
# Three task modes, each a workspace variant sharing all stimulus
# machinery so the physics/geometry is identical across conditions:
#
#   Catch  -- launch is given; a catch strip appears after the analyze
#     epoch at a per-trial-variable Y. The subject brings up a single
#     open-faced catcher (press below the strip), drags it, and releases
#     to commit. The ball plays back; "caught" = a catcher-floor contact.
#
#   2AFC   -- launch is given; a pair of catcher plates straddles the
#     landing point; the subject reports LEFT or RIGHT.
#
#   Control-- the catcher is given (sampled at a reachable spot); the
#     subject adjusts the launch ANGLE to hit it, then commits. Same
#     catch detection + graded feedback as Catch.
#
# ------------------------------------------------------------------
# CODE LAYOUT
#   STATE              namespace variables, grouped by concern
#   SHARED CORE        helpers used by 2+ modes (sim, visuals, catcher,
#                      detection, sampler, scene building, commit)
#   MODE DISPATCH      tpred_hook + the thin shared callbacks; every
#                      input callback dispatches to tpred_<mode>_<hook>
#   MODE: 2AFC         self-contained: tpred_2afc_<hook> procs
#   MODE: CATCH        self-contained: tpred_catch_<hook> procs
#   MODE: CONTROL      self-contained: tpred_control_<hook> procs
#   ADJUSTERS          workspace set/get procs
#   WORKSPACE          variant + adjuster registration
#
# Adding a future mode = one new MODE section + one workspace variant;
# nothing in SHARED CORE or MODE DISPATCH needs to change.
# ------------------------------------------------------------------
#
# Controls (Catch):   mouse press-drag-release; ←/→ nudge; ↓ drop; ↑ new
# Controls (2AFC):    ← left; → right; ↑ new
# Controls (Control): mouse drag to aim; ←/→ nudge angle; ↓ launch; ↑ new

# ============================================================
# STATE
# ============================================================

namespace eval tpred {
    package require box2d

    # --- current mode ---
    variable mode        catch   ;# "catch" | "2afc" | "control"

    # --- shared scene handles ---
    variable bworld      ""
    variable ball        ""
    variable launch_cue  ""
    variable world_group ""
    variable world_dg    ""

    # --- trial-flow flags (shared) ---
    variable committed   0        ;# ball has been launched
    variable trial_start_ms ""    ;# stamped on the first tick

    # --- sampled trial values (shared) ---
    variable angle_deg         45.0   ;# launch angle above horizontal
    variable launch_dir_sign   1      ;# +1 = rightward, -1 = leftward
    variable angle_display_deg 45.0   ;# launch-vector direction for the cue
    variable launch_vx   0.0
    variable launch_vy   0.0
    variable ball_x0     -6.0
    variable ball_y0     0.0
    variable catch_strip_y 0.0        ;# the catch line for this trial
    variable land_x      0.0          ;# pre-sim landing point ("answer")
    variable land_y      0.0
    variable land_time   0.0

    # --- movable catcher (Catch + Control) ---
    variable catcher_parts   {}       ;# {floor left right}
    variable catcher_cx      0.0
    variable catcher_visible 0
    variable dragging        0
    variable caught          -1       ;# -1 pending, 0 miss, 1 catch
    variable catch_offset    ""       ;# ball_x - catcher_cx at floor contact
    variable prev_ball_y     ""       ;# previous-frame ball y, for miss test

    # --- 2AFC catcher plates ---
    variable catcher_L_parts {}
    variable catcher_R_parts {}
    variable correct_side    L
    variable cL_cx           0.0
    variable cR_cx           0.0
    variable response        ""       ;# "", "L", or "R"
    variable landed          0        ;# ball reached the catch line

    # --- Catch-mode strip ---
    variable catch_strip_obj      ""
    variable catch_strip_revealed 0

    # --- Control-mode state ---
    variable control_angle_deg    45.0  ;# subject's current chosen angle
    variable control_nudge_step    3.0  ;# degrees per arrow-key press
    variable control_catcher_x_min 0.0
    variable control_catcher_x_max 5.0
    variable control_catcher_y_min -3.0
    variable control_catcher_y_max 0.0

    # --- world geometry (workspace units) ---
    variable xrange      18.0
    variable yrange      14.0
    variable catcher_y   -5.5         ;# 2AFC: fixed catch line
    variable gravity_y   -10.0

    # --- launch parameters ---
    variable angle_min   25.0
    variable angle_max   75.0
    variable launch_speed 9.0
    variable ball_radius 0.35

    # --- start position (2AFC / Catch sampler) ---
    variable start_x_min -7.5
    variable start_x_max -5.5
    variable start_y_min  -2.0
    variable start_y_max   2.0
    variable randomize_start_y 0
    variable bidirectional 1

    # --- Catch-strip sampling + timing ---
    variable catch_strip_y_min -3.0
    variable catch_strip_y_max  2.0
    variable analyze_duration   1.5   ;# seconds before the strip auto-reveals
    variable hide_cue_on_reveal 0     ;# hide the launch cue at strip reveal

    # --- catcher geometry (shared) ---
    variable catcher_width  1.8
    variable catcher_wall_h 0.6       ;# low walls keep edge ricochets gentle
    variable catcher_wall_w 0.15
    variable catcher_floor_h 0.15
    variable catcher_restitution 0.1  ;# low: caught balls settle
    variable catcher_gap    0.4       ;# 2AFC: inner gap between the pair

    # --- inverted-V plank (Catch / 2AFC) ---
    # The apex is NOT set directly -- it is derived per trial so the
    # plank reliably sits in the ball's path: the sampler anchors it on
    # the free trajectory at plank_anchor_frac of the way to the catch
    # line, shifts it sideways by plank_apex_offset (random side) so the
    # ball strikes an ARM rather than the tip, and down by
    # plank_apex_vgap. apex_angle_deg is the full angle between arms
    # (smaller = steeper V = stronger deflection).
    variable plank_enabled        0
    variable plank_anchor_frac    0.5
    variable plank_apex_offset    0.8
    variable plank_apex_vgap      0.3
    variable plank_apex_angle_deg 90.0
    variable plank_arm_length     2.5
    variable plank_thickness      0.15
    variable plank_restitution    0.4
    variable plank_apex_x         0.0   ;# derived per trial
    variable plank_apex_y         -1.0  ;# derived per trial

    # --- vertical-wall blockers (Blockers mode) ---
    # Tall, thin static rectangles sprinkled in the field. Without
    # friction, deflection comes from SIDE-face hits: the wall's normal
    # is horizontal, so low restitution kills the ball's horizontal
    # velocity and it drops nearly straight down from impact height.
    # No "relevant" blocker is enforced -- relevance is emergent. Some
    # trials will have the ball thread through cleanly (all decoys),
    # others will have one or more deflections.
    variable n_blockers              4
    variable randomize_n_blockers    1
    variable blocker_random_set      {0 4 8}
    variable blocker_w               0.2
    variable blocker_h               2.5
    variable blocker_restitution     0.15
    variable blocker_field_x_min    -6.0
    variable blocker_field_x_max     6.0
    variable blocker_field_y_min    -4.0
    variable blocker_field_y_max     3.0
    variable blocker_min_sep         0.4   ;# margin between AABBs
    variable blocker_start_exclusion 1.5   ;# keep clear of launch point
    variable blocker_max_contacts    8     ;# reject trials above this
    variable blocker_list            {}    ;# per-trial: list of {cx cy}

    # --- wind zone (Wind mode) ---
    # A single localized region where a horizontal force acts on the ball
    # while it is inside. The force magnitude is sampled per trial from
    # N(wind_force_mean, wind_force_sigma) and the direction is randomized
    # if wind_bidirectional. The visualization shows mean (bold arrow) +
    # variance (faded arrows at +/-1 sigma), so the subject must integrate
    # over an uncertain force when picking the catcher position. Same
    # rejection-sampler structure as blockers: sample force, sample zone,
    # sim, accept iff catchable.
    variable wind_zone_w             4.0
    variable wind_zone_h             4.0
    variable wind_zone_field_x_min  -4.0
    variable wind_zone_field_x_max   4.0
    variable wind_zone_field_y_min  -3.0
    variable wind_zone_field_y_max   3.0
    variable wind_force_mean         8.0   ;# N (applied while ball is inside)
    variable wind_force_sigma        2.0   ;# stdev around mean
    variable wind_bidirectional      1
    variable wind_start_exclusion    2.0   ;# min distance from launch point
    variable wind_zone_cx            0.0   ;# per-trial
    variable wind_zone_cy            0.0   ;# per-trial
    variable wind_force_x            0.0   ;# per-trial (signed)
    variable wind_direction_sign     1     ;# per-trial: +1 right, -1 left
    variable wind_active             0     ;# per-frame: ball inside zone?

    # --- fixation ---
    variable fix_radius  0.18
    variable fix_visible 1

    # --- world transform ---
    variable world_scale 0.7

    # --- sim parameters ---
    variable sim_max_time 4.0
    variable max_attempts 300
}

# ============================================================
# SHARED CORE -- coordinate mapping
# ============================================================

# Map the current cursor position into scene coordinates. getMouseWorld
# (a stim2 builtin) returns the cursor in world degrees, mapped via
# glfwGetWindowSize so it is correct under Retina/framebuffer scaling.
# The scene metagroup is drawn scaled by world_scale, so we undo that.
proc tpred_mouse_to_scene {} {
    lassign [getMouseWorld] wx wy
    set s $tpred::world_scale
    if { $s == 0 } { set s 1.0 }
    return [list [expr {$wx / $s}] [expr {$wy / $s}]]
}

# ============================================================
# SHARED CORE -- plank geometry
# ============================================================

# Per-arm box geometry for the current inverted-V plank.
# Returns: { r_cx r_cy r_ang  l_cx l_cy l_ang  arm_length arm_thickness }
proc tpred_plank_geom {} {
    set apex_x  $tpred::plank_apex_x
    set apex_y  $tpred::plank_apex_y
    set L       $tpred::plank_arm_length
    set thick   $tpred::plank_thickness
    set half    [expr {$tpred::plank_apex_angle_deg * $::pi / 360.0}]
    set halfL   [expr {$L / 2.0}]

    # Right arm: from apex pointing toward (sin half, -cos half).
    set rdx [expr { sin($half)}]
    set rdy [expr {-cos($half)}]
    set r_cx [expr {$apex_x + $halfL * $rdx}]
    set r_cy [expr {$apex_y + $halfL * $rdy}]
    set r_ang [expr {atan2($rdy, $rdx)}]

    # Left arm: mirror across the vertical through the apex.
    set ldx [expr {-sin($half)}]
    set ldy [expr {-cos($half)}]
    set l_cx [expr {$apex_x + $halfL * $ldx}]
    set l_cy [expr {$apex_y + $halfL * $ldy}]
    set l_ang [expr {atan2($ldy, $ldx)}]

    return [list $r_cx $r_cy $r_ang $l_cx $l_cy $l_ang $L $thick]
}

# ============================================================
# SHARED CORE -- trajectory simulation
# ============================================================

# Simulate one launch in a throwaway world and report what happened.
#   with_plank : 1 to include the inverted-V plank (from the current
#                tpred::plank_apex_* values); 0 for the free trajectory.
# Returns a dict:
#   ok        - 0 if the ball left the visible field
#   crossed   - 1 if the ball crossed the catch line going DOWN
#   land_x/y/t- crossing point and time (valid when crossed)
#   hit_plank - 1 if the ball contacted a plank arm (with_plank only)
#   path_x/y  - per-step ball position lists (used to anchor the plank)
proc tpred_sim_trajectory { sx sy vx vy line_y step with_plank } {
    set xr2 [expr {$tpred::xrange / 2.0}]
    set yr2 [expr {$tpred::yrange / 2.0}]
    set rest_y [expr {$line_y + $tpred::catcher_floor_h/2.0 + $tpred::ball_radius}]

    set world [box2d::createWorld]

    # Plank built BEFORE the ball so it exists when the sim steps.
    if { $with_plank && $tpred::plank_enabled } {
        lassign [tpred_plank_geom] r_cx r_cy r_ang l_cx l_cy l_ang arm_L arm_T
        set rb [box2d::createBox $world plank_r 0 $r_cx $r_cy $arm_L $arm_T $r_ang]
        set lb [box2d::createBox $world plank_l 0 $l_cx $l_cy $arm_L $arm_T $l_ang]
        box2d::setRestitution $world $rb $tpred::plank_restitution
        box2d::setRestitution $world $lb $tpred::plank_restitution
    }

    set body [box2d::createCircle $world ball 2 $sx $sy $tpred::ball_radius]
    box2d::setLinearVelocity $world $body $vx $vy

    set ok 1
    set crossed 0
    set hit_plank 0
    set land_x 0.0; set land_y 0.0; set land_t 0.0
    set path_x {}; set path_y {}
    set prev_by $sy
    for { set t 0.0 } { $t < $tpred::sim_max_time } { set t [expr {$t + $step}] } {
        box2d::step $world $step
        if { $with_plank && !$hit_plank } {
            if { [box2d::getContactBeginEventCount $world] > 0 } {
                foreach c [box2d::getContactBeginEvents $world] {
                    if { [lsearch $c plank_r] >= 0 || [lsearch $c plank_l] >= 0 } {
                        set hit_plank 1
                        break
                    }
                }
            }
        }
        lassign [box2d::getBodyInfo $world $body] bx by _
        lappend path_x $bx
        lappend path_y $by

        # Reject if the ball leaves the visible field.
        if { $bx < -$xr2 || $bx > $xr2 || $by > $yr2 || $by < -$yr2 } {
            set ok 0
            break
        }
        # First DOWNWARD crossing of the catch line.
        if { !$crossed && $prev_by > $rest_y && $by <= $rest_y } {
            set crossed 1
            set land_x $bx; set land_y $by; set land_t $t
            break
        }
        set prev_by $by
    }
    box2d::destroy $world

    return [dict create ok $ok crossed $crossed \
                land_x $land_x land_y $land_y land_t $land_t \
                hit_plank $hit_plank path_x $path_x path_y $path_y]
}

# Solvability check: re-simulate the trajectory with a catcher centered
# at cx on catch line line_y, and confirm the ball gets a clean floor
# (catcher_b) contact. Identical catcher geometry/restitution to the
# live world. Returns 1 if catchable, 0 otherwise.
proc tpred_sim_catch_test { sx sy vx vy cx line_y step with_plank } {
    set xr2 [expr {$tpred::xrange / 2.0}]
    set yr2 [expr {$tpred::yrange / 2.0}]

    set world [box2d::createWorld]

    if { $with_plank && $tpred::plank_enabled } {
        lassign [tpred_plank_geom] r_cx r_cy r_ang l_cx l_cy l_ang arm_L arm_T
        set rb [box2d::createBox $world plank_r 0 $r_cx $r_cy $arm_L $arm_T $r_ang]
        set lb [box2d::createBox $world plank_l 0 $l_cx $l_cy $arm_L $arm_T $l_ang]
        box2d::setRestitution $world $rb $tpred::plank_restitution
        box2d::setRestitution $world $lb $tpred::plank_restitution
    }

    lassign [tpred_catcher_geom $cx $line_y] \
        fx fy fw fh  lx ly lw lh  rx ry rw rh
    set fb [box2d::createBox $world catcher_b 0 $fx $fy $fw $fh 0]
    set cl [box2d::createBox $world catcher_l 0 $lx $ly $lw $lh 0]
    set cr [box2d::createBox $world catcher_r 0 $rx $ry $rw $rh 0]
    foreach b [list $fb $cl $cr] {
        box2d::setRestitution $world $b $tpred::catcher_restitution
    }

    set body [box2d::createCircle $world ball 2 $sx $sy $tpred::ball_radius]
    box2d::setLinearVelocity $world $body $vx $vy

    set caught 0
    set prev_by $sy
    for { set t 0.0 } { $t < $tpred::sim_max_time } { set t [expr {$t + $step}] } {
        box2d::step $world $step
        if { [box2d::getContactBeginEventCount $world] > 0 } {
            foreach c [box2d::getContactBeginEvents $world] {
                if { [lsearch $c catcher_b] >= 0 } { set caught 1; break }
            }
        }
        if { $caught } break
        lassign [box2d::getBodyInfo $world $body] bx by _
        # Bail on out-of-bounds, or once the ball has fallen past the
        # catch line (a miss -- no catcher_b contact will follow).
        if { $bx < -$xr2 || $bx > $xr2 || $by < -$yr2 || $by > $yr2 } break
        if { $prev_by > $line_y && $by <= $line_y } break
        set prev_by $by
    }
    box2d::destroy $world
    return $caught
}

# Launch-centric sampler (used by 2AFC + Catch). Fills tpred:: state and
# returns 1 on success, 0 if no acceptable trajectory was found.
#
# The catch line is fixed catcher_y (2AFC) or sampled per attempt from
# [catch_strip_y_min, catch_strip_y_max] (Catch). With the plank enabled,
# placement is trajectory-anchored: sim the FREE trajectory, anchor the
# apex on it, then re-sim WITH the plank; accept only if the bounced
# ball hits an arm, stays in bounds, crosses the catch line, and (Catch)
# is solvable.
proc tpred_sample_launch_trial {} {
    set step  [expr {[screen_set FrameDuration] / 1000.0}]
    if { $step <= 0 } { set step 0.01667 }

    for { set attempt 0 } { $attempt < $tpred::max_attempts } { incr attempt } {
        # Sample launch angle and direction.
        set ang [expr {$tpred::angle_min + \
                       rand() * ($tpred::angle_max - $tpred::angle_min)}]
        if { $tpred::bidirectional } {
            set sign [expr {rand() < 0.5 ? -1 : 1}]
        } else {
            set sign 1
        }
        set rad [expr {$ang * $::pi / 180.0}]
        set vx  [expr {$sign * $tpred::launch_speed * cos($rad)}]
        set vy  [expr {$tpred::launch_speed * sin($rad)}]

        # Sample start x (mirrored to the opposite side for leftward launch).
        set sx_mag_min [expr {abs($tpred::start_x_min)}]
        set sx_mag_max [expr {abs($tpred::start_x_max)}]
        if { $sx_mag_min > $sx_mag_max } {
            set tmp $sx_mag_min; set sx_mag_min $sx_mag_max; set sx_mag_max $tmp
        }
        set sx_mag [expr {$sx_mag_min + rand() * ($sx_mag_max - $sx_mag_min)}]
        set sx     [expr {-$sign * $sx_mag}]

        if { $tpred::randomize_start_y } {
            set sy [expr {$tpred::start_y_min + \
                          rand() * ($tpred::start_y_max - $tpred::start_y_min)}]
        } else {
            set sy 0.0
        }

        # Catch line for this attempt.
        if { $tpred::mode eq "catch" } {
            set line_y [expr {$tpred::catch_strip_y_min + \
                rand() * ($tpred::catch_strip_y_max - $tpred::catch_strip_y_min)}]
        } else {
            set line_y $tpred::catcher_y
        }

        if { $tpred::plank_enabled } {
            # Pass 1: free trajectory, used to anchor the plank.
            set free [tpred_sim_trajectory $sx $sy $vx $vy $line_y $step 0]
            if { ![dict get $free ok] || ![dict get $free crossed] } { continue }

            set pxs [dict get $free path_x]
            set pys [dict get $free path_y]
            set n [llength $pxs]
            if { $n < 2 } { continue }
            set ai [expr {int(($n - 1) * $tpred::plank_anchor_frac)}]
            if { $ai < 0 }       { set ai 0 }
            if { $ai > $n - 1 }  { set ai [expr {$n - 1}] }
            set anchor_x [lindex $pxs $ai]
            set anchor_y [lindex $pys $ai]

            # Random side: which arm the ball is aimed at. Offsetting the
            # apex from the path makes the ball strike an arm, not the
            # tip; the bounce direction is whatever the physics produces.
            set pside [expr {rand() < 0.5 ? -1 : 1}]
            set tpred::plank_apex_x \
                [expr {$anchor_x - $pside * $tpred::plank_apex_offset}]
            set tpred::plank_apex_y \
                [expr {$anchor_y - $tpred::plank_apex_vgap}]

            # Pass 2: the real bounced trajectory.
            set res [tpred_sim_trajectory $sx $sy $vx $vy $line_y $step 1]
            if { ![dict get $res ok] || ![dict get $res crossed] } { continue }
            if { ![dict get $res hit_plank] } { continue }
        } else {
            set res [tpred_sim_trajectory $sx $sy $vx $vy $line_y $step 0]
            if { ![dict get $res ok] || ![dict get $res crossed] } { continue }
        }

        set land_x [dict get $res land_x]
        set land_y [dict get $res land_y]
        set land_t [dict get $res land_t]

        # Catch mode: the trial must be solvable -- a catcher centered at
        # the crossing point must produce a clean floor contact.
        if { $tpred::mode eq "catch" } {
            if { ![tpred_sim_catch_test $sx $sy $vx $vy $land_x $line_y \
                       $step $tpred::plank_enabled] } {
                continue
            }
        }

        # Accept.
        set tpred::angle_deg         $ang
        set tpred::launch_dir_sign   $sign
        set tpred::angle_display_deg [expr {atan2($vy, $vx) * 180.0 / $::pi}]
        set tpred::launch_vx   $vx
        set tpred::launch_vy   $vy
        set tpred::ball_x0     $sx
        set tpred::ball_y0     $sy
        set tpred::catch_strip_y $line_y
        set tpred::land_x      $land_x
        set tpred::land_y      $land_y
        set tpred::land_time   $land_t
        return 1
    }
    return 0
}

# ============================================================
# SHARED CORE -- visual primitives
# ============================================================

proc tpred_make_rect {} { return [polygon] }

proc tpred_make_circle {} {
    set c [polygon]
    polycirc $c 1
    return $c
}

# Plain (visual-only) rectangle, no physics body.
proc tpred_visual_rect { cx cy w h color } {
    set r [tpred_make_rect]
    scaleObj $r [expr {1.0*$w}] [expr {1.0*$h}]
    translateObj $r $cx $cy
    polycolor $r {*}$color
    return $r
}

# Box2D-backed static box: real body + linked visual. Used for the
# inverted-V plank arms and the movable catcher parts. setObjMatrix is
# required to enable the obj's use_matrix flag so the visual picks up
# Box2D's per-frame matrix writes.
proc tpred_create_body_box { bworld name tx ty sx sy angle_rad color } {
    set body [Box2D_createBox $bworld $name 0 $tx $ty $sx $sy $angle_rad]
    set b    [tpred_make_rect]
    scaleObj $b [expr {1.0*$sx}] [expr {1.0*$sy}]
    polycolor $b {*}$color
    set angle_deg [expr {$angle_rad * 180.0 / $::pi}]
    set m [dl_tcllist [mat4_createTranslationAngle $tx $ty $angle_deg]]
    setObjMatrix $b {*}$m
    Box2D_linkObj $bworld $body $b
    setObjProp $b body   $body
    setObjProp $b bworld $bworld
    return $b
}

# Box2D-backed static circle for the ball. Created static so it sits
# still until commit; tpred_commit_ball flips it to dynamic.
proc tpred_create_body_circle { bworld name tx ty radius color } {
    set body [Box2D_createCircle $bworld $name 0 $tx $ty $radius]
    set c    [tpred_make_circle]
    scaleObj $c [expr {2.0*$radius}] [expr {2.0*$radius}]
    polycolor $c {*}$color
    set m [dl_tcllist [mat4_createTranslationAngle $tx $ty 0.0]]
    setObjMatrix $c {*}$m
    Box2D_linkObj $bworld $body $c
    setObjProp $c body   $body
    setObjProp $c bworld $bworld
    return $c
}

# A simple arrow polygon (triangle) pointing along +x in its local frame.
proc tpred_build_arrow { cx cy angle_deg length width color } {
    set a [polygon]
    polyverts $a \
        [dl_flist 0.0 $length 0.0] \
        [dl_flist [expr {-$width/2.0}] 0.0 [expr {$width/2.0}]]
    polytype $a triangles
    polycolor $a {*}$color
    translateObj $a $cx $cy
    rotateObj $a $angle_deg 0 0 1
    return $a
}

# Small fixation cross.
proc tpred_build_fix {} {
    set mg [metagroup]
    set r $tpred::fix_radius
    set h [polygon]
    polyverts $h [dl_flist [expr {-$r}] $r [expr {-$r}] $r] \
                 [dl_flist [expr {-$r*0.2}] [expr {-$r*0.2}] \
                           [expr {$r*0.2}]  [expr {$r*0.2}]]
    polytype $h triangle_strip
    polycolor $h 1 1 1
    metagroupAdd $mg $h

    set v [polygon]
    polyverts $v [dl_flist [expr {-$r*0.2}] [expr {-$r*0.2}] \
                           [expr {$r*0.2}]  [expr {$r*0.2}]] \
                 [dl_flist [expr {-$r}] $r [expr {-$r}] $r]
    polytype $v triangle_strip
    polycolor $v 1 1 1
    metagroupAdd $mg $v
    return $mg
}

# ============================================================
# SHARED CORE -- movable catcher (Catch + Control)
# ============================================================

# Geometry of the three catcher boxes (floor + 2 walls) for a catcher
# centered at cx on catch line y. Shared by the live catcher and the
# pre-sim solvability check so both use identical geometry. Returns:
#   { fx fy fw fh   lx ly lw lh   rx ry rw rh }
proc tpred_catcher_geom { cx y } {
    set w  $tpred::catcher_width
    set wh $tpred::catcher_wall_h
    set ww $tpred::catcher_wall_w
    set fh $tpred::catcher_floor_h
    set half_w [expr {$w / 2.0}]
    set wall_y [expr {$y - $fh/2.0 + $wh/2.0}]
    return [list \
        $cx $y $w $fh \
        [expr {$cx - $half_w + $ww/2.0}] $wall_y $ww $wh \
        [expr {$cx + $half_w - $ww/2.0}] $wall_y $ww $wh]
}

# Pre-create the open-faced catcher (floor + 2 walls) as static Box2D
# bodies, parked off-screen and hidden. tpred_place_catcher_parts moves
# it; tpred_show_catcher reveals it.
proc tpred_create_movable_catcher {} {
    set bworld $tpred::bworld
    set w  $tpred::catcher_width
    set wh $tpred::catcher_wall_h
    set ww $tpred::catcher_wall_w
    set fh $tpred::catcher_floor_h
    set color { 0.82 0.82 0.88 1.0 }
    set park_y -1000.0

    set fobj [tpred_create_body_box $bworld catcher_b 0 $park_y $w  $fh 0 $color]
    set lobj [tpred_create_body_box $bworld catcher_l 0 $park_y $ww $wh 0 $color]
    set robj [tpred_create_body_box $bworld catcher_r 0 $park_y $ww $wh 0 $color]
    set tpred::catcher_parts [list $fobj $lobj $robj]
    foreach p $tpred::catcher_parts {
        Box2D_setRestitution $bworld [setObjProp $p body] $tpred::catcher_restitution
        metagroupAdd $tpred::world_group $p
        setVisible $p 0
    }
}

# Clamp a candidate catcher center x to keep the whole catcher on-screen.
proc tpred_clamp_catcher_x { cx } {
    set lim [expr {$tpred::xrange/2.0 - $tpred::catcher_width/2.0}]
    if { $cx < -$lim } { set cx -$lim }
    if { $cx >  $lim } { set cx  $lim }
    return $cx
}

# Position the three catcher bodies (and linked visuals) so the floor
# center sits at (cx, catch_strip_y).
proc tpred_place_catcher_parts { cx } {
    lassign [tpred_catcher_geom $cx $tpred::catch_strip_y] \
        fx fy fw fh  lx ly lw lh  rx ry rw rh
    lassign $tpred::catcher_parts fobj lobj robj
    Box2D_updateTransform $tpred::bworld [setObjProp $fobj body] $fx $fy
    Box2D_updateTransform $tpred::bworld [setObjProp $lobj body] $lx $ly
    Box2D_updateTransform $tpred::bworld [setObjProp $robj body] $rx $ry
}

proc tpred_show_catcher {} {
    foreach p $tpred::catcher_parts { setVisible $p 1 }
    set tpred::catcher_visible 1
}

# ============================================================
# SHARED CORE -- catch detection + graded feedback (Catch + Control)
# ============================================================

# PostScript behavior for Catch + Control. A catcher-floor contact is a
# catch; the ball crossing the catch line going down, or leaving the
# field, without a floor contact is a miss. catch_offset (ball_x minus
# catcher_cx at contact) drives the graded feedback.
proc tpred_check_catch { bworld } {
    if { !$tpred::committed } return
    if { $tpred::caught != -1 } return
    set body [setObjProp $tpred::ball body]
    lassign [Box2D_getBodyInfo $bworld $body] bx by _

    # Floor contact = catch.
    if { [Box2D_getContactBeginEventCount $bworld] > 0 } {
        foreach c [Box2D_getContactBeginEvents $bworld] {
            if { [lsearch $c catcher_b] >= 0 } {
                set tpred::caught 1
                set tpred::catch_offset [expr {$bx - $tpred::catcher_cx}]
                tpred_show_catch_feedback
                return
            }
        }
    }

    # Miss: ball crosses the catch line going DOWN, or leaves the field.
    # Tracking the previous-frame y (not a fixed threshold) handles catch
    # lines sampled above the launch height -- the ball may pass up
    # through the line first; only the later downward crossing is a miss.
    set xr2 [expr {$tpred::xrange / 2.0}]
    set yr2 [expr {$tpred::yrange / 2.0}]
    set line_y $tpred::catch_strip_y
    set crossed_down [expr {$tpred::prev_ball_y ne "" \
        && $tpred::prev_ball_y > $line_y && $by <= $line_y}]
    set out_of_bounds [expr {$bx < -$xr2 || $bx > $xr2 || $by < -$yr2}]
    if { $crossed_down || $out_of_bounds } {
        set tpred::caught 0
        set tpred::catch_offset ""
        Box2D_setBodyType $bworld $body 1
        Box2D_setLinearVelocity $bworld $body 0 0
        tpred_show_catch_feedback
        return
    }
    set tpred::prev_ball_y $by
}

# Graded feedback: green at dead-center -> yellow toward the edges -> red
# on a miss. Mirrors the graded-reward scheme planned for the real task.
proc tpred_show_catch_feedback {} {
    if { $tpred::caught == 1 } {
        set half_w [expr {$tpred::catcher_width / 2.0}]
        if { $half_w > 0 } {
            set frac [expr {abs($tpred::catch_offset) / $half_w}]
        } else {
            set frac 1.0
        }
        if { $frac > 1.0 } { set frac 1.0 }
        set color [list \
            [expr {0.2 + $frac * 0.75}] \
            [expr {0.9 - $frac * 0.05}] \
            [expr {0.3 - $frac * 0.10}]]
    } else {
        set color { 1.0 0.25 0.25 }
    }
    foreach p $tpred::catcher_parts { polycolor $p {*}$color }
}

# ============================================================
# SHARED CORE -- scene building
# ============================================================

# Shared scene root: world + metagroup. Per-mode build hooks add the
# rest (catchers/strip/cue/ball/plank) in their preferred draw order.
proc tpred_build_scene_common {} {
    resetObjList
    glistInit 1

    set bworld [Box2D]
    set tpred::bworld $bworld
    glistAddObject $bworld 0

    set grp [metagroup]
    objName $grp tpred_group
    set tpred::world_group $grp

    return [list $bworld $grp]
}

# Inverted-V plank arms (no-op if the plank is disabled). Built from the
# per-trial-derived apex; the live geometry matches the pre-sim exactly.
proc tpred_add_plank { bworld grp } {
    if { !$tpred::plank_enabled } return
    lassign [tpred_plank_geom] r_cx r_cy r_ang l_cx l_cy l_ang arm_L arm_T
    set plank_color { 0.85 0.85 0.9 1.0 }
    set pr [tpred_create_body_box $bworld plank_r $r_cx $r_cy $arm_L $arm_T $r_ang $plank_color]
    Box2D_setRestitution $bworld [setObjProp $pr body] $tpred::plank_restitution
    metagroupAdd $grp $pr
    set pl [tpred_create_body_box $bworld plank_l $l_cx $l_cy $arm_L $arm_T $l_ang $plank_color]
    Box2D_setRestitution $bworld [setObjProp $pl body] $tpred::plank_restitution
    metagroupAdd $grp $pl
}

# Wind zone (a single rectangular region). Built as:
#   - a translucent colored rectangle (the zone footprint)
#   - a bold arrow at the center indicating the mean direction/magnitude
#   - two faded arrows at +/-1 sigma showing the uncertainty range
# The physical force is a sensor box at the same footprint; sensor begin
# events flip wind_active=1 (force applied per frame), end events flip
# wind_active=0.
proc tpred_add_wind_zone { bworld grp } {
    set cx $tpred::wind_zone_cx
    set cy $tpred::wind_zone_cy
    set w  $tpred::wind_zone_w
    set h  $tpred::wind_zone_h

    # Translucent zone footprint (very salient cyan).
    set fill [tpred_visual_rect $cx $cy $w $h { 0.2 0.7 0.95 0.30 }]
    metagroupAdd $grp $fill
    # Outline (slightly brighter edge).
    set edge_w 0.08
    set edge_color { 0.4 0.85 1.0 0.85 }
    set top [tpred_visual_rect $cx [expr {$cy + $h/2.0}] $w $edge_w $edge_color]
    set bot [tpred_visual_rect $cx [expr {$cy - $h/2.0}] $w $edge_w $edge_color]
    set lft [tpred_visual_rect [expr {$cx - $w/2.0}] $cy $edge_w $h $edge_color]
    set rgt [tpred_visual_rect [expr {$cx + $w/2.0}] $cy $edge_w $h $edge_color]
    foreach o [list $top $bot $lft $rgt] { metagroupAdd $grp $o }

    # Arrows: faded for +/-1 sigma, bold for mean. All horizontal.
    set sgn  $tpred::wind_direction_sign
    set mean $tpred::wind_force_mean
    set sig  $tpred::wind_force_sigma
    set angle_deg [expr {$sgn > 0 ? 0.0 : 180.0}]

    # Scale arrow length to mean magnitude (tunable).
    set scale 0.10
    set len_lo [expr {max(0.1, ($mean - $sig)) * $scale}]
    set len_hi [expr {     ($mean + $sig)  * $scale}]
    set len_mn [expr {$mean                * $scale}]
    set arrow_w 0.4

    if { $sig > 0 } {
        set a_lo [tpred_build_arrow $cx $cy $angle_deg $len_lo $arrow_w \
                      { 1.0 0.95 0.5 0.30 }]
        metagroupAdd $grp $a_lo
        set a_hi [tpred_build_arrow $cx $cy $angle_deg $len_hi $arrow_w \
                      { 1.0 0.95 0.5 0.30 }]
        metagroupAdd $grp $a_hi
    }
    set a_mn [tpred_build_arrow $cx $cy $angle_deg $len_mn $arrow_w \
                  { 1.0 0.85 0.15 1.0 }]
    metagroupAdd $grp $a_mn

    # No physical body for the zone: the per-frame tick does a direct
    # ball-position check against the visualized AABB and applies the
    # force when inside. Keeps live and sandbox sims agreeing without
    # depending on the sensor-event path.
}

# Vertical-wall blockers (no-op if the list is empty). Tall thin static
# rectangles whose SIDE faces deflect the ball: a side hit with low
# restitution kills horizontal velocity and the ball drops nearly
# straight down from impact height. Geometry/restitution mirror the
# sandbox sim, so live and sampled outcomes agree.
proc tpred_add_blockers { bworld grp } {
    set color { 0.7 0.5 0.35 1.0 }
    set i 0
    foreach b $tpred::blocker_list {
        lassign $b cx cy
        set obj [tpred_create_body_box $bworld "blocker_$i" \
                     $cx $cy $tpred::blocker_w $tpred::blocker_h 0 $color]
        Box2D_setRestitution $bworld [setObjProp $obj body] \
            $tpred::blocker_restitution
        metagroupAdd $grp $obj
        incr i
    }
}

# Launch cue + ball + fixation. The cue is built at angle_display_deg
# (the launch-vector direction, set by whichever sampler ran).
proc tpred_add_cue_ball_fix { bworld grp } {
    # Launch arrow (direction cue). ~3:1 length:width at the default
    # launch speed -- reads as a direction marker, not a long pointer.
    set arrow_len   [expr {$tpred::launch_speed * 0.167}]
    set arrow_width 0.5
    set cue [tpred_build_arrow \
                 $tpred::ball_x0 $tpred::ball_y0 \
                 $tpred::angle_display_deg $arrow_len $arrow_width \
                 { 1.0 0.85 0.2 1.0 }]
    objName $cue tpred_launch_cue
    set tpred::launch_cue $cue
    metagroupAdd $grp $cue

    # Ball (static now; flipped to dynamic on commit).
    set ball_obj [tpred_create_body_circle \
                      $bworld ball \
                      $tpred::ball_x0 $tpred::ball_y0 $tpred::ball_radius \
                      { 0.2 0.85 1.0 1.0 }]
    objName $ball_obj tpred_ball
    set tpred::ball $ball_obj
    metagroupAdd $grp $ball_obj

    # Fixation on top.
    if { $tpred::fix_visible } {
        set fx [tpred_build_fix]
        objName $fx tpred_fix
        metagroupAdd $grp $fx
    }
}

proc tpred_finish_scene {} {
    glistAddObject $tpred::world_group 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    setBackground 25 25 30
    tpred_set_scale $tpred::world_scale
    redraw
}

# Reset all per-trial flags. Run before the mode's sample hook.
proc tpred_reset_flags {} {
    set tpred::committed            0
    set tpred::trial_start_ms       ""
    set tpred::catcher_parts        {}
    set tpred::catcher_visible      0
    set tpred::dragging             0
    set tpred::caught               -1
    set tpred::catch_offset         ""
    set tpred::prev_ball_y          ""
    set tpred::catcher_L_parts      {}
    set tpred::catcher_R_parts      {}
    set tpred::response             ""
    set tpred::landed               0
    set tpred::catch_strip_obj      ""
    set tpred::catch_strip_revealed 0
    set tpred::wind_active          0
}

# ============================================================
# SHARED CORE -- commit
# ============================================================

# Flip the ball to dynamic and apply the launch velocity. The live
# world's per-frame step takes it from there. Every mode commits through
# here; the launch velocity is whatever the mode last wrote into
# tpred::launch_vx/vy (sampled for 2AFC/Catch, subject-chosen for Control).
proc tpred_commit_ball {} {
    if { $tpred::committed } return
    set tpred::committed 1
    if { $tpred::launch_cue ne "" } { setVisible $tpred::launch_cue 0 }
    set body [setObjProp $tpred::ball body]
    Box2D_setBodyType $tpred::bworld $body 2
    Box2D_setLinearVelocity $tpred::bworld $body \
        $tpred::launch_vx $tpred::launch_vy
    redraw
}

# ============================================================
# MODE DISPATCH
# ============================================================
#
# Every shared input callback dispatches to tpred_<mode>_<hook>. A mode
# implements only the hooks it needs; missing hooks are silently no-ops.
#
# Hooks:
#   sample            -> 1/0   sample a trial into tpred:: state
#   build bworld grp           add this mode's objects to the scene
#   tick  bworld               per-frame PreScript work
#   post  bworld               per-step PostScript work
#   on_press / on_release      mouse button (left)
#   on_left / on_right         arrow keys
#   on_down                    down arrow
#   action <name>              workspace action buttons

# Call tpred_<mode>_<hook> if it exists; return its result, else "".
proc tpred_hook { hook args } {
    set p "tpred_${tpred::mode}_${hook}"
    if { [llength [info commands $p]] } {
        return [uplevel #0 [list $p {*}$args]]
    }
    return ""
}

# Build (or rebuild) a trial for the given mode.
proc tpred_setup { {mode catch} } {
    set tpred::mode $mode

    if { $tpred::world_dg ne "" && [dg_exists $tpred::world_dg] } {
        dg_delete $tpred::world_dg
    }
    set tpred::world_dg ""
    tpred_reset_flags

    set ok [tpred_hook sample]
    if { $ok eq "" || !$ok } {
        error "tpred_setup: mode '$mode' failed to sample a valid trial"
    }

    lassign [tpred_build_scene_common] bworld grp
    tpred_hook build $bworld $grp

    # Shared per-frame + per-step scripts; modes implement tick/post.
    addPreScript  $bworld [list tpred_tick $bworld]
    addPostScript $bworld [list tpred_post $bworld]

    tpred_finish_scene
}

# Workspace entry points (one per variant).
proc tpred_setup_catch    {} { tpred_setup catch    }
proc tpred_setup_2afc     {} { tpred_setup 2afc     }
proc tpred_setup_control  {} { tpred_setup control  }
proc tpred_setup_blockers {} { tpred_setup blockers }
proc tpred_setup_wind     {} { tpred_setup wind     }

# Per-frame / per-step dispatch.
proc tpred_tick { bworld } { tpred_hook tick $bworld }
proc tpred_post { bworld } { tpred_hook post $bworld }

# Shared input callbacks.
proc onMousePress   {} { tpred_hook on_press   }
proc onMouseRelease {} { tpred_hook on_release }
proc onLeftArrow    {} { tpred_hook on_left    }
proc onRightArrow   {} { tpred_hook on_right   }
proc onDownArrow    {} { tpred_hook on_down    }
proc onUpArrow      {} { tpred_setup $tpred::mode }   ;# new trial (universal)

# Workspace action buttons. "new" is universal; the rest dispatch.
proc tpred_trigger { action } {
    if { $action eq "new" } {
        tpred_setup $tpred::mode
    } else {
        tpred_hook action $action
    }
    return
}

# ============================================================
# MODE: 2AFC
# ============================================================
#
# Launch is sampled; a pair of catcher plates straddles the landing
# point; the subject reports LEFT or RIGHT (arrow key or action button).

proc tpred_2afc_sample {} {
    if { ![tpred_sample_launch_trial] } { return 0 }
    tpred_2afc_place_catchers
    return 1
}

proc tpred_2afc_build { bworld grp } {
    # Catcher pair + catch-line reference (built first = drawn behind).
    set cat_color { 0.75 0.75 0.78 1.0 }
    set tpred::catcher_L_parts [tpred_2afc_build_plate $tpred::cL_cx L $cat_color]
    foreach p $tpred::catcher_L_parts { metagroupAdd $grp $p }
    set tpred::catcher_R_parts [tpred_2afc_build_plate $tpred::cR_cx R $cat_color]
    foreach p $tpred::catcher_R_parts { metagroupAdd $grp $p }
    set line [tpred_visual_rect 0 \
                  [expr {$tpred::catcher_y - $tpred::catcher_floor_h/2.0 - 0.04}] \
                  $tpred::xrange 0.04 { 0.35 0.35 0.4 1.0 }]
    metagroupAdd $grp $line

    tpred_add_plank $bworld $grp
    tpred_add_cue_ball_fix $bworld $grp
}

proc tpred_2afc_post { bworld } { tpred_2afc_check_landing $bworld }

proc tpred_2afc_on_left  {} { tpred_2afc_respond L }
proc tpred_2afc_on_right {} { tpred_2afc_respond R }

proc tpred_2afc_action { a } {
    switch $a {
        left  { tpred_2afc_respond L }
        right { tpred_2afc_respond R }
    }
}

# Place the catcher pair so one plate always contains land_x.
proc tpred_2afc_place_catchers {} {
    set w   $tpred::catcher_width
    set gap $tpred::catcher_gap
    set lx  $tpred::land_x

    if { rand() < 0.5 } {
        set tpred::correct_side L
    } else {
        set tpred::correct_side R
    }

    set half_w  [expr {$w / 2.0}]
    set inset   [expr {$tpred::catcher_wall_w * 1.5}]
    set jitter_max [expr {$half_w - $inset}]
    if { $jitter_max < 0 } { set jitter_max 0 }
    set jitter [expr {(2.0 * rand() - 1.0) * $jitter_max}]

    if { $tpred::correct_side eq "L" } {
        set tpred::cL_cx [expr {$lx - $jitter}]
        set tpred::cR_cx [expr {$tpred::cL_cx + $w + $gap}]
    } else {
        set tpred::cR_cx [expr {$lx - $jitter}]
        set tpred::cL_cx [expr {$tpred::cR_cx - $w - $gap}]
    }
}

# 2AFC catcher footprint (bottom plate only). With realtime physics,
# side walls would be visual-only, so we draw just the floor segment;
# the L/R decision is read from the ball's x at the catch line.
proc tpred_2afc_build_plate { cx tag color } {
    set b [tpred_visual_rect $cx $tpred::catcher_y \
               $tpred::catcher_width $tpred::catcher_floor_h $color]
    objName $b "catch_${tag}_b"
    return [list $b]
}

# PostScript: freeze the ball at the catch line, color the chosen plate.
proc tpred_2afc_check_landing { bworld } {
    if { $tpred::landed } return
    if { !$tpred::committed } return
    set body [setObjProp $tpred::ball body]
    lassign [Box2D_getBodyInfo $bworld $body] bx by _

    set rest_y [expr {$tpred::catcher_y \
                      + $tpred::catcher_floor_h/2.0 + $tpred::ball_radius}]
    if { $by <= $rest_y } {
        set tpred::landed 1
        Box2D_setBodyType $bworld $body 1
        Box2D_setLinearVelocity $bworld $body 0 0
        Box2D_updateTransform $bworld $body $bx $rest_y
        tpred_2afc_show_feedback
    }
}

proc tpred_2afc_show_feedback {} {
    if { $tpred::response eq "" } return
    set correct [expr {$tpred::response eq $tpred::correct_side}]
    if { $correct } {
        set color { 0.2 0.9 0.3 }
    } else {
        set color { 1.0 0.25 0.25 }
    }
    if { $tpred::response eq "L" } {
        foreach p $tpred::catcher_L_parts { polycolor $p {*}$color }
    } else {
        foreach p $tpred::catcher_R_parts { polycolor $p {*}$color }
    }
}

proc tpred_2afc_respond { side } {
    if { $tpred::committed } return
    if { $side ne "L" && $side ne "R" } return
    set tpred::response $side
    tpred_commit_ball
}

# ============================================================
# MODE: CATCH
# ============================================================
#
# Launch is sampled. After the analyze epoch the catch strip appears at
# a per-trial-variable Y. The subject presses below the strip to bring
# up a single open-faced catcher, drags it, and releases to commit.

proc tpred_catch_sample {} { return [tpred_sample_launch_trial] }

proc tpred_catch_build { bworld grp } {
    # Catch strip (hidden until reveal), drawn behind the scene.
    set strip [tpred_catch_build_strip]
    objName $strip tpred_strip
    set tpred::catch_strip_obj $strip
    metagroupAdd $grp $strip
    setVisible $strip 0

    tpred_add_plank $bworld $grp
    tpred_add_cue_ball_fix $bworld $grp

    # Movable catcher: pre-created hidden + parked off-screen so its
    # bodies can't interfere until the subject places it.
    tpred_create_movable_catcher
}

proc tpred_catch_post { bworld } { tpred_check_catch $bworld }

# Per-frame: strip-reveal timer + catcher drag tracking. Polling the
# cursor here -- frame-rate-bounded, in the normal render path -- avoids
# a high-frequency cursor callback perturbing stim2's main loop.
proc tpred_catch_tick { bworld } {
    # Strip-reveal timer (the real task gets this from a dserv datapoint).
    if { !$tpred::catch_strip_revealed } {
        if { $tpred::trial_start_ms eq "" } {
            set tpred::trial_start_ms $::StimTime
        } else {
            set elapsed [expr {($::StimTime - $tpred::trial_start_ms) / 1000.0}]
            if { $elapsed >= $tpred::analyze_duration } {
                tpred_reveal_strip
            }
        }
    }
    # Drag tracking. The glist is dynamic, so repositioning the catcher
    # bodies here is picked up by the next frame -- no redraw needed.
    if { $tpred::dragging } {
        lassign [tpred_mouse_to_scene] mx my
        set tpred::catcher_cx [tpred_clamp_catcher_x $mx]
        tpred_place_catcher_parts $tpred::catcher_cx
    }
}

proc tpred_catch_on_press {} {
    if { !$tpred::catch_strip_revealed } return
    if { $tpred::committed } return
    lassign [tpred_mouse_to_scene] mx my
    # Press must be below the catch strip (so the hand never occludes
    # the catcher, which rides on the strip).
    if { $my >= $tpred::catch_strip_y } return
    if { !$tpred::catcher_visible } { tpred_show_catcher }
    set tpred::catcher_cx [tpred_clamp_catcher_x $mx]
    tpred_place_catcher_parts $tpred::catcher_cx
    set tpred::dragging 1
    redraw
}

proc tpred_catch_on_release {} {
    if { !$tpred::dragging } return
    set tpred::dragging 0
    tpred_commit_ball
}

proc tpred_catch_on_left  {} { tpred_catch_nudge -0.25 }
proc tpred_catch_on_right {} { tpred_catch_nudge  0.25 }
proc tpred_catch_on_down  {} { tpred_commit_ball }

proc tpred_catch_action { a } {
    switch $a {
        reveal { tpred_reveal_strip }
        drop   { tpred_commit_ball }
    }
}

# Nudge the catcher (keyboard fallback for the touch drag).
proc tpred_catch_nudge { dx } {
    if { $tpred::committed } return
    if { !$tpred::catch_strip_revealed } return
    if { !$tpred::catcher_visible } {
        tpred_show_catcher
        set tpred::catcher_cx 0.0
    }
    set tpred::catcher_cx [tpred_clamp_catcher_x \
        [expr {$tpred::catcher_cx + $dx}]]
    tpred_place_catcher_parts $tpred::catcher_cx
    redraw
}

# Catch strip: faint band from catch_strip_y up by one wall height, plus
# a bright catch line at catch_strip_y where the catcher floor sits.
proc tpred_catch_build_strip {} {
    set y $tpred::catch_strip_y
    set h $tpred::catcher_wall_h
    set w $tpred::xrange
    set mg [metagroup]
    set fill [tpred_visual_rect 0 [expr {$y + $h/2.0}] $w $h { 0.5 0.7 1.0 0.10 }]
    metagroupAdd $mg $fill
    set ln [tpred_visual_rect 0 $y $w 0.06 { 0.6 0.85 1.0 0.9 }]
    metagroupAdd $mg $ln
    return $mg
}

# Reveal the catch strip. ESS would call this directly from a trial
# state; the demo drives it from the analyze-duration timer in the tick.
proc tpred_reveal_strip {} {
    if { $tpred::catch_strip_revealed } return
    if { $tpred::catch_strip_obj eq "" } return
    set tpred::catch_strip_revealed 1
    setVisible $tpred::catch_strip_obj 1
    if { $tpred::hide_cue_on_reveal && $tpred::launch_cue ne "" } {
        setVisible $tpred::launch_cue 0
    }
    redraw
}

# ============================================================
# MODE: CONTROL
# ============================================================
#
# The catcher is sampled at a reachable position. The subject adjusts
# the launch ANGLE -- drag the cue arrow, or arrow keys -- and commits
# (down arrow / Launch). Same catch detection + graded feedback as Catch.

proc tpred_control_sample {} { return [tpred_sample_control_trial] }

proc tpred_control_build { bworld grp } {
    # Target catcher: visible from the start at its sampled position
    # (built first = drawn behind the ball).
    tpred_create_movable_catcher
    tpred_show_catcher
    tpred_place_catcher_parts $tpred::catcher_cx

    # No plank in Control mode. The cue is the interactive element.
    tpred_add_cue_ball_fix $bworld $grp
}

proc tpred_control_post { bworld } { tpred_check_catch $bworld }

# Per-frame: while dragging, aim the cue at the cursor.
proc tpred_control_tick { bworld } {
    if { $tpred::dragging } {
        lassign [tpred_mouse_to_scene] mx my
        tpred_control_set_angle [tpred_control_angle_from_cursor $mx $my]
    }
}

proc tpred_control_on_press {} {
    if { $tpred::committed } return
    lassign [tpred_mouse_to_scene] mx my
    tpred_control_set_angle [tpred_control_angle_from_cursor $mx $my]
    set tpred::dragging 1
}

proc tpred_control_on_release {} { set tpred::dragging 0 }

proc tpred_control_on_left  {} { tpred_control_nudge_angle [expr {-$tpred::control_nudge_step}] }
proc tpred_control_on_right {} { tpred_control_nudge_angle $tpred::control_nudge_step }
proc tpred_control_on_down  {} { tpred_control_launch }

proc tpred_control_action { a } {
    switch $a { launch { tpred_control_launch } }
}

# Angle of the cursor about the launch point, in degrees.
proc tpred_control_angle_from_cursor { mx my } {
    return [expr {atan2($my - $tpred::ball_y0, $mx - $tpred::ball_x0) \
                  * 180.0 / $::pi}]
}

# Set the chosen launch angle (clamped to the allowed range) and re-aim
# the cue arrow.
#
# Deliberately does NOT call redraw: this runs every frame from
# tpred_control_tick (a PreScript, executed inside the render pass).
# redraw posts an UPDATE_DISPLAY message, and processMessages drains the
# queue in a while-loop -- so a PreScript that calls redraw re-arms the
# queue every iteration and spins forever. The glist is dynamic, so the
# rotateObj change is picked up on the next frame anyway.
proc tpred_control_set_angle { a } {
    if { $tpred::committed } return
    if { $a < $tpred::angle_min } { set a $tpred::angle_min }
    if { $a > $tpred::angle_max } { set a $tpred::angle_max }
    set tpred::control_angle_deg $a
    if { $tpred::launch_cue ne "" } {
        rotateObj $tpred::launch_cue $a 0 0 1
    }
}

proc tpred_control_nudge_angle { da } {
    tpred_control_set_angle [expr {$tpred::control_angle_deg + $da}]
}

# Commit: launch the ball at the subject's chosen angle.
proc tpred_control_launch {} {
    if { $tpred::committed } return
    set rad [expr {$tpred::control_angle_deg * $::pi / 180.0}]
    set tpred::launch_vx [expr {$tpred::launch_speed * cos($rad)}]
    set tpred::launch_vy [expr {$tpred::launch_speed * sin($rad)}]
    tpred_commit_ball
}

# Catcher-centric sampler: sample a catcher position and confirm at
# least one launch angle in range hits it cleanly (the reachability
# check -- mirror of the Catch-mode solvability check). Plank-free.
proc tpred_sample_control_trial {} {
    set step [expr {[screen_set FrameDuration] / 1000.0}]
    if { $step <= 0 } { set step 0.01667 }

    set sx_lo [expr {min($tpred::start_x_min, $tpred::start_x_max)}]
    set sx_hi [expr {max($tpred::start_x_min, $tpred::start_x_max)}]

    for { set attempt 0 } { $attempt < $tpred::max_attempts } { incr attempt } {
        # Launch point: left-side, sampled within the start-x range.
        set sx [expr {$sx_lo + rand() * ($sx_hi - $sx_lo)}]
        set sy 0.0

        # Target catcher: sampled position.
        set cx [expr {$tpred::control_catcher_x_min + rand() * \
            ($tpred::control_catcher_x_max - $tpred::control_catcher_x_min)}]
        set cy [expr {$tpred::control_catcher_y_min + rand() * \
            ($tpred::control_catcher_y_max - $tpred::control_catcher_y_min)}]

        # Reachability: is there a launch angle in range (rightward, at
        # launch_speed) that lands the ball cleanly in this catcher?
        set reachable 0
        set asweep 16
        for { set i 0 } { $i <= $asweep } { incr i } {
            set a [expr {$tpred::angle_min + \
                ($tpred::angle_max - $tpred::angle_min) * $i / double($asweep)}]
            set rad [expr {$a * $::pi / 180.0}]
            set vx [expr {$tpred::launch_speed * cos($rad)}]
            set vy [expr {$tpred::launch_speed * sin($rad)}]
            if { [tpred_sim_catch_test $sx $sy $vx $vy $cx $cy $step 0] } {
                set reachable 1
                break
            }
        }
        if { !$reachable } { continue }

        # Accept. The subject starts at mid-range angle.
        set tpred::ball_x0       $sx
        set tpred::ball_y0       $sy
        set tpred::catcher_cx    $cx
        set tpred::catch_strip_y $cy
        set tpred::control_angle_deg \
            [expr {($tpred::angle_min + $tpred::angle_max) / 2.0}]
        set tpred::angle_display_deg $tpred::control_angle_deg
        return 1
    }
    return 0
}

# ============================================================
# MODE: BLOCKERS
# ============================================================
#
# Same trial flow as Catch mode (catcher drag, strip-reveal timing,
# graded feedback) with N (variable per trial) vertical-wall blockers
# sprinkled in the field. Relevance is emergent: the sampler does not
# enforce a "relevant" blocker, so some trials have the ball threading
# through cleanly (all decoys) and others have one or more deflections.
# The catcher is placed at the ball's actual landing x.

proc tpred_blockers_sample {} { return [tpred_sample_blockers_trial] }

proc tpred_blockers_build { bworld grp } {
    set strip [tpred_catch_build_strip]
    objName $strip tpred_strip
    set tpred::catch_strip_obj $strip
    metagroupAdd $grp $strip
    setVisible $strip 0

    tpred_add_blockers $bworld $grp
    tpred_add_cue_ball_fix $bworld $grp
    tpred_create_movable_catcher
}

# All per-frame / input / feedback behavior is identical to Catch.
proc tpred_blockers_tick       { bworld } { tpred_catch_tick $bworld }
proc tpred_blockers_post       { bworld } { tpred_check_catch $bworld }
proc tpred_blockers_on_press   {}         { tpred_catch_on_press }
proc tpred_blockers_on_release {}         { tpred_catch_on_release }
proc tpred_blockers_on_left    {}         { tpred_catch_on_left }
proc tpred_blockers_on_right   {}         { tpred_catch_on_right }
proc tpred_blockers_on_down    {}         { tpred_catch_on_down }
proc tpred_blockers_action     { a }      { tpred_catch_action $a }

# ============================================================
# MODE: WIND
# ============================================================
#
# Same catcher mechanic as Catch mode, with a single rectangular WIND
# ZONE in the field. A horizontal force is applied to the ball while it
# is inside the zone. The force magnitude is sampled per trial from a
# Gaussian (mean +/- sigma) and the direction is randomized when
# bidirectional. The visualization shows the mean direction and the
# +/-1 sigma extents, so the subject is integrating over an uncertain
# force when choosing where to place the catcher. Cleanest behavioral
# readout of probabilistic / distributional physical simulation.

proc tpred_wind_sample {} { return [tpred_sample_wind_trial] }

proc tpred_wind_build { bworld grp } {
    set strip [tpred_catch_build_strip]
    objName $strip tpred_strip
    set tpred::catch_strip_obj $strip
    metagroupAdd $grp $strip
    setVisible $strip 0

    tpred_add_wind_zone $bworld $grp
    tpred_add_cue_ball_fix $bworld $grp
    tpred_create_movable_catcher
}

# Most of Catch mode's flow applies as-is. The tick applies the wind
# force whenever the ball center is inside the zone AABB. This direct
# position check is more robust than sensor events for the demo: no
# dependence on enableSensorEvents timing, no off-by-one frames at zone
# entry/exit, easy to debug (state visible in tpred::wind_active).
proc tpred_wind_tick { bworld } {
    tpred_catch_tick $bworld
    if { !$tpred::committed } return
    set body [setObjProp $tpred::ball body]
    lassign [Box2D_getBodyInfo $bworld $body] bx by _
    set hw [expr {$tpred::wind_zone_w / 2.0}]
    set hh [expr {$tpred::wind_zone_h / 2.0}]
    if { abs($bx - $tpred::wind_zone_cx) <= $hw && \
         abs($by - $tpred::wind_zone_cy) <= $hh } {
        set tpred::wind_active 1
        Box2D_applyForce $bworld ball $tpred::wind_force_x 0.0
    } else {
        set tpred::wind_active 0
    }
}

proc tpred_wind_post { bworld } { tpred_check_catch $bworld }

# All input behavior is identical to Catch.
proc tpred_wind_on_press   {}        { tpred_catch_on_press }
proc tpred_wind_on_release {}        { tpred_catch_on_release }
proc tpred_wind_on_left    {}        { tpred_catch_on_left }
proc tpred_wind_on_right   {}        { tpred_catch_on_right }
proc tpred_wind_on_down    {}        { tpred_catch_on_down }
proc tpred_wind_action     { a }     { tpred_catch_action $a }

# Sample a wind force magnitude (signed) using a Box-Muller draw.
proc tpred_sample_wind_force {} {
    set mean $tpred::wind_force_mean
    set sig  $tpred::wind_force_sigma
    # Box-Muller for a normal sample.
    set u1 [expr {rand()}]
    set u2 [expr {rand()}]
    if { $u1 < 1e-12 } { set u1 1e-12 }
    set z [expr {sqrt(-2.0 * log($u1)) * cos(2.0 * $::pi * $u2)}]
    if { $tpred::wind_bidirectional } {
        set sign [expr {rand() < 0.5 ? -1 : 1}]
    } else {
        set sign 1
    }
    set mag [expr {$mean + $z * $sig}]
    if { $mag < 0 } { set mag 0 }   ;# don't flip sign via noise
    return [expr {$sign * $mag}]
}

# Pick a zone center inside the configured field, keeping clear of the
# launch point. Returns {cx cy}.
proc tpred_sample_wind_position { sx sy } {
    set xmin $tpred::wind_zone_field_x_min
    set xmax $tpred::wind_zone_field_x_max
    set ymin $tpred::wind_zone_field_y_min
    set ymax $tpred::wind_zone_field_y_max
    set excl $tpred::wind_start_exclusion
    for { set t 0 } { $t < 100 } { incr t } {
        set cx [expr {$xmin + rand() * ($xmax - $xmin)}]
        set cy [expr {$ymin + rand() * ($ymax - $ymin)}]
        set dx [expr {$cx - $sx}]
        set dy [expr {$cy - $sy}]
        if { ($dx*$dx + $dy*$dy) > ($excl*$excl) } {
            return [list $cx $cy]
        }
    }
    return ""
}

# Sandbox sim: integrate the ball through the world with the wind force
# applied whenever the ball is inside the zone AABB. Mirrors
# tpred_sim_trajectory_blockers in shape.
proc tpred_sim_trajectory_wind { sx sy vx vy line_y step zx zy zw zh fx } {
    set xr2 [expr {$tpred::xrange / 2.0}]
    set yr2 [expr {$tpred::yrange / 2.0}]
    set rest_y [expr {$line_y + $tpred::catcher_floor_h/2.0 + $tpred::ball_radius}]
    set hw [expr {$zw / 2.0}]
    set hh [expr {$zh / 2.0}]

    set world [box2d::createWorld]
    set body  [box2d::createCircle $world ball 2 $sx $sy $tpred::ball_radius]
    box2d::setLinearVelocity $world $body $vx $vy

    set ok 1
    set crossed 0
    set time_in_zone 0.0
    set land_x 0.0; set land_y 0.0; set land_t 0.0
    set prev_by $sy
    for { set t 0.0 } { $t < $tpred::sim_max_time } { set t [expr {$t + $step}] } {
        lassign [box2d::getBodyInfo $world $body] bx by _
        if { abs($bx - $zx) <= $hw && abs($by - $zy) <= $hh } {
            box2d::applyForce $world $body $fx 0.0
            set time_in_zone [expr {$time_in_zone + $step}]
        }
        box2d::step $world $step
        lassign [box2d::getBodyInfo $world $body] bx by _
        if { $bx < -$xr2 || $bx > $xr2 || $by > $yr2 || $by < -$yr2 } {
            set ok 0
            break
        }
        if { !$crossed && $prev_by > $rest_y && $by <= $rest_y } {
            set crossed 1
            set land_x $bx; set land_y $by; set land_t $t
            break
        }
        set prev_by $by
    }
    box2d::destroy $world

    return [dict create ok $ok crossed $crossed \
                land_x $land_x land_y $land_y land_t $land_t \
                time_in_zone $time_in_zone]
}

# Solvability check: catcher at cx on catch line, plus wind zone.
proc tpred_sim_catch_test_wind { sx sy vx vy cx line_y step zx zy zw zh fx } {
    set xr2 [expr {$tpred::xrange / 2.0}]
    set yr2 [expr {$tpred::yrange / 2.0}]
    set hw [expr {$zw / 2.0}]
    set hh [expr {$zh / 2.0}]

    set world [box2d::createWorld]

    lassign [tpred_catcher_geom $cx $line_y] \
        fx_c fy_c fw_c fh_c  lx ly lw lh  rx ry rw rh
    set fb [box2d::createBox $world catcher_b 0 $fx_c $fy_c $fw_c $fh_c 0]
    set cl [box2d::createBox $world catcher_l 0 $lx $ly $lw $lh 0]
    set cr [box2d::createBox $world catcher_r 0 $rx $ry $rw $rh 0]
    foreach b [list $fb $cl $cr] {
        box2d::setRestitution $world $b $tpred::catcher_restitution
    }

    set body [box2d::createCircle $world ball 2 $sx $sy $tpred::ball_radius]
    box2d::setLinearVelocity $world $body $vx $vy

    set caught 0
    set prev_by $sy
    for { set t 0.0 } { $t < $tpred::sim_max_time } { set t [expr {$t + $step}] } {
        lassign [box2d::getBodyInfo $world $body] bx by _
        if { abs($bx - $zx) <= $hw && abs($by - $zy) <= $hh } {
            box2d::applyForce $world $body $fx 0.0
        }
        box2d::step $world $step
        if { [box2d::getContactBeginEventCount $world] > 0 } {
            foreach c [box2d::getContactBeginEvents $world] {
                if { [lsearch $c catcher_b] >= 0 } { set caught 1; break }
            }
        }
        if { $caught } break
        lassign [box2d::getBodyInfo $world $body] bx by _
        if { $bx < -$xr2 || $bx > $xr2 || $by < -$yr2 || $by > $yr2 } break
        if { $prev_by > $line_y && $by <= $line_y } break
        set prev_by $by
    }
    box2d::destroy $world
    return $caught
}

# Full wind-mode sampler. Picks launch params, samples a wind zone and
# wind force, simulates, accepts iff the ball lands cleanly in a catcher.
proc tpred_sample_wind_trial {} {
    set step [expr {[screen_set FrameDuration] / 1000.0}]
    if { $step <= 0 } { set step 0.01667 }

    for { set attempt 0 } { $attempt < $tpred::max_attempts } { incr attempt } {
        # Launch params (mirror tpred_sample_launch_trial).
        set ang [expr {$tpred::angle_min + \
                       rand() * ($tpred::angle_max - $tpred::angle_min)}]
        if { $tpred::bidirectional } {
            set sign [expr {rand() < 0.5 ? -1 : 1}]
        } else {
            set sign 1
        }
        set rad [expr {$ang * $::pi / 180.0}]
        set vx  [expr {$sign * $tpred::launch_speed * cos($rad)}]
        set vy  [expr {$tpred::launch_speed * sin($rad)}]

        set sx_mag_min [expr {abs($tpred::start_x_min)}]
        set sx_mag_max [expr {abs($tpred::start_x_max)}]
        if { $sx_mag_min > $sx_mag_max } {
            set tmp $sx_mag_min; set sx_mag_min $sx_mag_max; set sx_mag_max $tmp
        }
        set sx_mag [expr {$sx_mag_min + rand() * ($sx_mag_max - $sx_mag_min)}]
        set sx     [expr {-$sign * $sx_mag}]

        if { $tpred::randomize_start_y } {
            set sy [expr {$tpred::start_y_min + \
                          rand() * ($tpred::start_y_max - $tpred::start_y_min)}]
        } else {
            set sy 0.0
        }

        set line_y [expr {$tpred::catch_strip_y_min + \
            rand() * ($tpred::catch_strip_y_max - $tpred::catch_strip_y_min)}]

        # Sample wind zone position + force for this attempt.
        set pos [tpred_sample_wind_position $sx $sy]
        if { $pos eq "" } { continue }
        lassign $pos zx zy
        set fx [tpred_sample_wind_force]

        set res [tpred_sim_trajectory_wind $sx $sy $vx $vy $line_y $step \
                     $zx $zy $tpred::wind_zone_w $tpred::wind_zone_h $fx]
        if { ![dict get $res ok] || ![dict get $res crossed] } { continue }

        set land_x [dict get $res land_x]
        set land_y [dict get $res land_y]
        set land_t [dict get $res land_t]

        if { ![tpred_sim_catch_test_wind $sx $sy $vx $vy $land_x $line_y $step \
                   $zx $zy $tpred::wind_zone_w $tpred::wind_zone_h $fx] } {
            continue
        }

        # Accept.
        set tpred::angle_deg         $ang
        set tpred::launch_dir_sign   $sign
        set tpred::angle_display_deg [expr {atan2($vy, $vx) * 180.0 / $::pi}]
        set tpred::launch_vx     $vx
        set tpred::launch_vy     $vy
        set tpred::ball_x0       $sx
        set tpred::ball_y0       $sy
        set tpred::catch_strip_y $line_y
        set tpred::land_x        $land_x
        set tpred::land_y        $land_y
        set tpred::land_time     $land_t
        set tpred::wind_zone_cx  $zx
        set tpred::wind_zone_cy  $zy
        set tpred::wind_force_x  $fx
        set tpred::wind_direction_sign [expr {$fx >= 0 ? 1 : -1}]
        return 1
    }
    return 0
}

# Sample N non-overlapping blocker centers inside the configured field
# rectangle. Each blocker is {cx cy} (orientation is always vertical).
# Returns the list (possibly empty for N=0), or "" if N positions could
# not be placed within the per-blocker retry budget.
proc tpred_sample_blocker_positions { n sx sy } {
    if { $n <= 0 } { return {} }
    set bw   $tpred::blocker_w
    set bh   $tpred::blocker_h
    set xmin $tpred::blocker_field_x_min
    set xmax $tpred::blocker_field_x_max
    set ymin $tpred::blocker_field_y_min
    set ymax $tpred::blocker_field_y_max
    set sep  $tpred::blocker_min_sep
    set excl $tpred::blocker_start_exclusion

    set blockers {}
    set max_tries_per 100
    for { set i 0 } { $i < $n } { incr i } {
        set placed 0
        for { set t 0 } { $t < $max_tries_per } { incr t } {
            set cx [expr {$xmin + rand() * ($xmax - $xmin)}]
            set cy [expr {$ymin + rand() * ($ymax - $ymin)}]
            # Keep clear of the launch point (would make trial impossible).
            set dxs [expr {$cx - $sx}]
            set dys [expr {$cy - $sy}]
            if { ($dxs*$dxs + $dys*$dys) < ($excl*$excl) } { continue }
            # AABB overlap (with min_sep margin) against earlier blockers.
            set ok 1
            foreach b $blockers {
                lassign $b ex ey
                if { abs($cx - $ex) < ($bw + $sep) && \
                     abs($cy - $ey) < ($bh + $sep) } {
                    set ok 0
                    break
                }
            }
            if { $ok } {
                lappend blockers [list $cx $cy]
                set placed 1
                break
            }
        }
        if { !$placed } { return "" }
    }
    return $blockers
}

# Sandbox-world simulation of the ball through a given set of blockers.
# Returns a dict with the same shape as tpred_sim_trajectory plus
# n_contacts/hit_names so the sampler can cap chained deflections.
proc tpred_sim_trajectory_blockers { sx sy vx vy line_y step blockers } {
    set xr2 [expr {$tpred::xrange / 2.0}]
    set yr2 [expr {$tpred::yrange / 2.0}]
    set rest_y [expr {$line_y + $tpred::catcher_floor_h/2.0 + $tpred::ball_radius}]

    set world [box2d::createWorld]

    set i 0
    foreach b $blockers {
        lassign $b cx cy
        set bb [box2d::createBox $world "blocker_$i" 0 \
                    $cx $cy $tpred::blocker_w $tpred::blocker_h 0]
        box2d::setRestitution $world $bb $tpred::blocker_restitution
        incr i
    }

    set body [box2d::createCircle $world ball 2 $sx $sy $tpred::ball_radius]
    box2d::setLinearVelocity $world $body $vx $vy

    set ok 1
    set crossed 0
    set hit_names {}
    set land_x 0.0; set land_y 0.0; set land_t 0.0
    set prev_by $sy
    for { set t 0.0 } { $t < $tpred::sim_max_time } { set t [expr {$t + $step}] } {
        box2d::step $world $step
        if { [box2d::getContactBeginEventCount $world] > 0 } {
            foreach c [box2d::getContactBeginEvents $world] {
                foreach name $c {
                    if { [string match "blocker_*" $name] && \
                         [lsearch $hit_names $name] < 0 } {
                        lappend hit_names $name
                    }
                }
            }
        }
        lassign [box2d::getBodyInfo $world $body] bx by _
        if { $bx < -$xr2 || $bx > $xr2 || $by > $yr2 || $by < -$yr2 } {
            set ok 0
            break
        }
        if { !$crossed && $prev_by > $rest_y && $by <= $rest_y } {
            set crossed 1
            set land_x $bx; set land_y $by; set land_t $t
            break
        }
        set prev_by $by
    }
    box2d::destroy $world

    return [dict create ok $ok crossed $crossed \
                land_x $land_x land_y $land_y land_t $land_t \
                n_contacts [llength $hit_names] hit_names $hit_names]
}

# Solvability check: simulate the bounced trajectory with both the
# blockers AND a catcher centered at cx on catch line line_y; return 1
# iff the ball lands cleanly on the catcher floor.
proc tpred_sim_catch_test_blockers { sx sy vx vy cx line_y step blockers } {
    set xr2 [expr {$tpred::xrange / 2.0}]
    set yr2 [expr {$tpred::yrange / 2.0}]

    set world [box2d::createWorld]

    set i 0
    foreach b $blockers {
        lassign $b bcx bcy
        set bb [box2d::createBox $world "blocker_$i" 0 \
                    $bcx $bcy $tpred::blocker_w $tpred::blocker_h 0]
        box2d::setRestitution $world $bb $tpred::blocker_restitution
        incr i
    }

    lassign [tpred_catcher_geom $cx $line_y] \
        fx fy fw fh  lx ly lw lh  rx ry rw rh
    set fb [box2d::createBox $world catcher_b 0 $fx $fy $fw $fh 0]
    set cl [box2d::createBox $world catcher_l 0 $lx $ly $lw $lh 0]
    set cr [box2d::createBox $world catcher_r 0 $rx $ry $rw $rh 0]
    foreach b [list $fb $cl $cr] {
        box2d::setRestitution $world $b $tpred::catcher_restitution
    }

    set body [box2d::createCircle $world ball 2 $sx $sy $tpred::ball_radius]
    box2d::setLinearVelocity $world $body $vx $vy

    set caught 0
    set prev_by $sy
    for { set t 0.0 } { $t < $tpred::sim_max_time } { set t [expr {$t + $step}] } {
        box2d::step $world $step
        if { [box2d::getContactBeginEventCount $world] > 0 } {
            foreach c [box2d::getContactBeginEvents $world] {
                if { [lsearch $c catcher_b] >= 0 } { set caught 1; break }
            }
        }
        if { $caught } break
        lassign [box2d::getBodyInfo $world $body] bx by _
        if { $bx < -$xr2 || $bx > $xr2 || $by < -$yr2 || $by > $yr2 } break
        if { $prev_by > $line_y && $by <= $line_y } break
        set prev_by $by
    }
    box2d::destroy $world
    return $caught
}

# Blockers-mode sampler. Mirrors tpred_sample_launch_trial but for the
# vertical-wall world. No "relevant" blocker is enforced: blockers are
# random in the field and the catcher is placed at whatever x the ball
# crosses the catch line at. Relevance is emergent, which is the point.
proc tpred_sample_blockers_trial {} {
    set step [expr {[screen_set FrameDuration] / 1000.0}]
    if { $step <= 0 } { set step 0.01667 }

    # Per-trial N: random from a small set, or the slider value.
    if { $tpred::randomize_n_blockers } {
        set sl $tpred::blocker_random_set
        set n  [lindex $sl [expr {int(rand() * [llength $sl])}]]
    } else {
        set n [expr {int($tpred::n_blockers)}]
    }

    for { set attempt 0 } { $attempt < $tpred::max_attempts } { incr attempt } {
        # Launch params (mirror tpred_sample_launch_trial).
        set ang [expr {$tpred::angle_min + \
                       rand() * ($tpred::angle_max - $tpred::angle_min)}]
        if { $tpred::bidirectional } {
            set sign [expr {rand() < 0.5 ? -1 : 1}]
        } else {
            set sign 1
        }
        set rad [expr {$ang * $::pi / 180.0}]
        set vx  [expr {$sign * $tpred::launch_speed * cos($rad)}]
        set vy  [expr {$tpred::launch_speed * sin($rad)}]

        set sx_mag_min [expr {abs($tpred::start_x_min)}]
        set sx_mag_max [expr {abs($tpred::start_x_max)}]
        if { $sx_mag_min > $sx_mag_max } {
            set tmp $sx_mag_min; set sx_mag_min $sx_mag_max; set sx_mag_max $tmp
        }
        set sx_mag [expr {$sx_mag_min + rand() * ($sx_mag_max - $sx_mag_min)}]
        set sx     [expr {-$sign * $sx_mag}]

        if { $tpred::randomize_start_y } {
            set sy [expr {$tpred::start_y_min + \
                          rand() * ($tpred::start_y_max - $tpred::start_y_min)}]
        } else {
            set sy 0.0
        }

        set line_y [expr {$tpred::catch_strip_y_min + \
            rand() * ($tpred::catch_strip_y_max - $tpred::catch_strip_y_min)}]

        set blockers [tpred_sample_blocker_positions $n $sx $sy]
        if { $n > 0 && $blockers eq "" } { continue }

        set res [tpred_sim_trajectory_blockers $sx $sy $vx $vy $line_y $step $blockers]
        if { ![dict get $res ok] || ![dict get $res crossed] } { continue }
        if { [dict get $res n_contacts] > $tpred::blocker_max_contacts } { continue }

        set land_x [dict get $res land_x]
        set land_y [dict get $res land_y]
        set land_t [dict get $res land_t]

        if { ![tpred_sim_catch_test_blockers $sx $sy $vx $vy $land_x $line_y \
                   $step $blockers] } {
            continue
        }

        # Accept.
        set tpred::angle_deg         $ang
        set tpred::launch_dir_sign   $sign
        set tpred::angle_display_deg [expr {atan2($vy, $vx) * 180.0 / $::pi}]
        set tpred::launch_vx     $vx
        set tpred::launch_vy     $vy
        set tpred::ball_x0       $sx
        set tpred::ball_y0       $sy
        set tpred::catch_strip_y $line_y
        set tpred::land_x        $land_x
        set tpred::land_y        $land_y
        set tpred::land_time     $land_t
        set tpred::blocker_list  $blockers
        return 1
    }
    return 0
}

# ============================================================
# ADJUSTERS (workspace-bound)
# ============================================================

proc tpred_set_launch { angle_min angle_max launch_speed } {
    if { $angle_max < $angle_min } { set angle_max $angle_min }
    set tpred::angle_min    $angle_min
    set tpred::angle_max    $angle_max
    set tpred::launch_speed $launch_speed
    return
}
proc tpred_get_launch { {target {}} } {
    dict create \
        angle_min    $tpred::angle_min \
        angle_max    $tpred::angle_max \
        launch_speed $tpred::launch_speed
}

proc tpred_set_strip { catch_strip_y_min catch_strip_y_max analyze_duration
                       hide_cue_on_reveal } {
    if { $catch_strip_y_max < $catch_strip_y_min } {
        set catch_strip_y_max $catch_strip_y_min
    }
    set tpred::catch_strip_y_min  $catch_strip_y_min
    set tpred::catch_strip_y_max  $catch_strip_y_max
    set tpred::analyze_duration   $analyze_duration
    set tpred::hide_cue_on_reveal $hide_cue_on_reveal
    return
}
proc tpred_get_strip { {target {}} } {
    dict create \
        catch_strip_y_min  $tpred::catch_strip_y_min \
        catch_strip_y_max  $tpred::catch_strip_y_max \
        analyze_duration   $tpred::analyze_duration \
        hide_cue_on_reveal $tpred::hide_cue_on_reveal
}

proc tpred_set_catch_geom { catcher_width catcher_wall_h catcher_restitution } {
    set tpred::catcher_width       $catcher_width
    set tpred::catcher_wall_h      $catcher_wall_h
    set tpred::catcher_restitution $catcher_restitution
    return
}
proc tpred_get_catch_geom { {target {}} } {
    dict create \
        catcher_width       $tpred::catcher_width \
        catcher_wall_h      $tpred::catcher_wall_h \
        catcher_restitution $tpred::catcher_restitution
}

proc tpred_set_catchers { catcher_y catcher_width catcher_gap } {
    set tpred::catcher_y     $catcher_y
    set tpred::catcher_width $catcher_width
    set tpred::catcher_gap   $catcher_gap
    return
}
proc tpred_get_catchers { {target {}} } {
    dict create \
        catcher_y     $tpred::catcher_y \
        catcher_width $tpred::catcher_width \
        catcher_gap   $tpred::catcher_gap
}

proc tpred_set_control { catcher_x_min catcher_x_max catcher_y_min catcher_y_max
                         nudge_step } {
    if { $catcher_x_max < $catcher_x_min } { set catcher_x_max $catcher_x_min }
    if { $catcher_y_max < $catcher_y_min } { set catcher_y_max $catcher_y_min }
    set tpred::control_catcher_x_min $catcher_x_min
    set tpred::control_catcher_x_max $catcher_x_max
    set tpred::control_catcher_y_min $catcher_y_min
    set tpred::control_catcher_y_max $catcher_y_max
    set tpred::control_nudge_step    $nudge_step
    return
}
proc tpred_get_control { {target {}} } {
    dict create \
        catcher_x_min $tpred::control_catcher_x_min \
        catcher_x_max $tpred::control_catcher_x_max \
        catcher_y_min $tpred::control_catcher_y_min \
        catcher_y_max $tpred::control_catcher_y_max \
        nudge_step    $tpred::control_nudge_step
}

proc tpred_set_start { randomize_start_y bidirectional } {
    set tpred::randomize_start_y $randomize_start_y
    set tpred::bidirectional     $bidirectional
    return
}
proc tpred_get_start { {target {}} } {
    dict create \
        randomize_start_y $tpred::randomize_start_y \
        bidirectional     $tpred::bidirectional
}

proc tpred_set_plank { plank_enabled anchor_frac apex_offset apex_vgap
                       apex_angle_deg arm_length plank_restitution } {
    set tpred::plank_enabled        $plank_enabled
    set tpred::plank_anchor_frac    $anchor_frac
    set tpred::plank_apex_offset    $apex_offset
    set tpred::plank_apex_vgap      $apex_vgap
    set tpred::plank_apex_angle_deg $apex_angle_deg
    set tpred::plank_arm_length     $arm_length
    set tpred::plank_restitution    $plank_restitution
    return
}
proc tpred_get_plank { {target {}} } {
    dict create \
        plank_enabled     $tpred::plank_enabled \
        anchor_frac       $tpred::plank_anchor_frac \
        apex_offset       $tpred::plank_apex_offset \
        apex_vgap         $tpred::plank_apex_vgap \
        apex_angle_deg    $tpred::plank_apex_angle_deg \
        arm_length        $tpred::plank_arm_length \
        plank_restitution $tpred::plank_restitution
}

proc tpred_set_blockers { n_blockers randomize_n_blockers blocker_w blocker_h
                          blocker_restitution blocker_max_contacts } {
    set tpred::n_blockers           $n_blockers
    set tpred::randomize_n_blockers $randomize_n_blockers
    set tpred::blocker_w            $blocker_w
    set tpred::blocker_h            $blocker_h
    set tpred::blocker_restitution  $blocker_restitution
    set tpred::blocker_max_contacts $blocker_max_contacts
    return
}
proc tpred_get_blockers { {target {}} } {
    dict create \
        n_blockers           $tpred::n_blockers \
        randomize_n_blockers $tpred::randomize_n_blockers \
        blocker_w            $tpred::blocker_w \
        blocker_h            $tpred::blocker_h \
        blocker_restitution  $tpred::blocker_restitution \
        blocker_max_contacts $tpred::blocker_max_contacts
}

proc tpred_set_wind { wind_zone_w wind_zone_h wind_force_mean wind_force_sigma
                      wind_bidirectional } {
    set tpred::wind_zone_w        $wind_zone_w
    set tpred::wind_zone_h        $wind_zone_h
    set tpred::wind_force_mean    $wind_force_mean
    set tpred::wind_force_sigma   $wind_force_sigma
    set tpred::wind_bidirectional $wind_bidirectional
    return
}
proc tpred_get_wind { {target {}} } {
    dict create \
        wind_zone_w        $tpred::wind_zone_w \
        wind_zone_h        $tpred::wind_zone_h \
        wind_force_mean    $tpred::wind_force_mean \
        wind_force_sigma   $tpred::wind_force_sigma \
        wind_bidirectional $tpred::wind_bidirectional
}

proc tpred_set_fix { fix_visible } {
    set tpred::fix_visible $fix_visible
    return
}
proc tpred_get_fix { {target {}} } {
    dict create fix_visible $tpred::fix_visible
}

proc tpred_set_scale { scale } {
    set tpred::world_scale $scale
    catch { scaleObj tpred_group $scale $scale }
    redraw
}
proc tpred_get_scale { {target {}} } {
    dict create scale $tpred::world_scale
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

# Primary: Catch mode.
workspace::setup tpred_setup_catch {} \
    -adjusters {tpred_actions_catch tpred_launch tpred_plank tpred_strip \
                tpred_catch_geom tpred_start tpred_fix tpred_scale} \
    -label "Trajectory Prediction (Catch)"

# Variant: 2AFC mode.
workspace::variant 2afc {} -proc tpred_setup_2afc \
    -adjusters {tpred_actions_2afc tpred_launch tpred_plank tpred_catchers \
                tpred_start tpred_fix tpred_scale} \
    -label "Trajectory Prediction (2AFC)"

# Variant: Control mode.
workspace::variant control {} -proc tpred_setup_control \
    -adjusters {tpred_actions_control tpred_launch tpred_control \
                tpred_catch_geom tpred_fix tpred_scale} \
    -label "Trajectory Prediction (Control)"

# Variant: Blockers mode (Catch flow + vertical-wall clutter).
workspace::variant blockers {} -proc tpred_setup_blockers \
    -adjusters {tpred_actions_catch tpred_launch tpred_blockers tpred_strip \
                tpred_catch_geom tpred_start tpred_fix tpred_scale} \
    -label "Trajectory Prediction (Blockers)"

# Variant: Wind mode (Catch flow + single uncertain wind zone).
workspace::variant wind {} -proc tpred_setup_wind \
    -adjusters {tpred_actions_catch tpred_launch tpred_wind tpred_strip \
                tpred_catch_geom tpred_start tpred_fix tpred_scale} \
    -label "Trajectory Prediction (Wind)"

workspace::adjuster tpred_actions_catch {
    new    {action "New Trial (↑)"}
    reveal {action "Reveal Strip Now"}
    drop   {action "Drop (↓)"}
} -target {} -proc tpred_trigger \
  -label "Actions"

workspace::adjuster tpred_actions_2afc {
    left  {action "Left  (←)"}
    right {action "Right (→)"}
    new   {action "New Trial (↑)"}
} -target {} -proc tpred_trigger \
  -label "Response"

workspace::adjuster tpred_actions_control {
    new    {action "New Trial (↑)"}
    launch {action "Launch (↓)"}
} -target {} -proc tpred_trigger \
  -label "Actions"

workspace::adjuster tpred_launch {
    angle_min    {float -85.0 85.0 1.0 25.0 "Angle Min (deg above horizontal)"}
    angle_max    {float -85.0 85.0 1.0 75.0 "Angle Max (deg above horizontal)"}
    launch_speed {float   0.0 16.0 0.25 9.0 "Launch Speed"}
} -target {} -proc tpred_set_launch -getter tpred_get_launch \
  -label "Launch (applied on next trial)"

workspace::adjuster tpred_plank {
    plank_enabled     {bool  0 "Enable Inverted-V Plank"}
    anchor_frac       {float 0.0  1.0  0.05 0.5 "Anchor Fraction (along trajectory)"}
    apex_offset       {float 0.1  3.0  0.1  0.8 "Apex Offset (sideways from path)"}
    apex_vgap         {float 0.0  2.0  0.1  0.3 "Apex Vertical Gap (below anchor)"}
    apex_angle_deg    {float 30.0 150.0 5.0 90.0 "Apex Angle (smaller = steeper V)"}
    arm_length        {float 0.5  5.0  0.25 2.5 "Arm Length"}
    plank_restitution {float 0.0  1.0  0.05 0.4 "Restitution (bounciness)"}
} -target {} -proc tpred_set_plank -getter tpred_get_plank \
  -label "Plank (applied on next trial)"

workspace::adjuster tpred_blockers {
    n_blockers           {float 0 12  1    4    "Number of Blockers (when not randomized)"}
    randomize_n_blockers {bool  1 "Randomize N per trial (from {0, 4, 8})"}
    blocker_w            {float 0.1 1.0 0.05 0.2  "Blocker Width"}
    blocker_h            {float 0.5 4.0 0.25 2.5  "Blocker Height"}
    blocker_restitution  {float 0.0 1.0 0.05 0.15 "Blocker Restitution (low = clean drop)"}
    blocker_max_contacts {float 0   8   1    8    "Max Blocker Contacts (reject above)"}
} -target {} -proc tpred_set_blockers -getter tpred_get_blockers \
  -label "Blockers (applied on next trial)"

workspace::adjuster tpred_wind {
    wind_zone_w        {float 1.0 8.0  0.25 4.0 "Wind Zone Width"}
    wind_zone_h        {float 1.0 6.0  0.25 4.0 "Wind Zone Height"}
    wind_force_mean    {float 0.0 30.0 1.0  8.0 "Wind Force Mean (N)"}
    wind_force_sigma   {float 0.0 10.0 0.5  2.0 "Wind Force Sigma (uncertainty)"}
    wind_bidirectional {bool  1 "Randomize Wind Direction (L/R)"}
} -target {} -proc tpred_set_wind -getter tpred_get_wind \
  -label "Wind (applied on next trial)"

workspace::adjuster tpred_strip {
    catch_strip_y_min  {float -6.0  4.0 0.25 -3.0 "Catch Strip Y Min"}
    catch_strip_y_max  {float -6.0  4.0 0.25  2.0 "Catch Strip Y Max"}
    analyze_duration   {float  0.0  6.0 0.25  1.5 "Analyze Duration (s)"}
    hide_cue_on_reveal {bool   0 "Hide Launch Cue When Strip Appears"}
} -target {} -proc tpred_set_strip -getter tpred_get_strip \
  -label "Catch Strip (applied on next trial)"

workspace::adjuster tpred_catch_geom {
    catcher_width       {float 0.6 5.0  0.1  1.8 "Catcher Width"}
    catcher_wall_h      {float 0.1 2.0  0.05 0.6 "Catcher Wall Height"}
    catcher_restitution {float 0.0 1.0  0.05 0.1 "Catcher Restitution (bounciness)"}
} -target {} -proc tpred_set_catch_geom -getter tpred_get_catch_geom \
  -label "Catcher (applied on next trial)"

workspace::adjuster tpred_catchers {
    catcher_y     {float -7.0 -1.0 0.25 -5.5 "Catcher Y (raise = easier)"}
    catcher_width {float 0.8 4.0 0.1 1.8 "Catcher Width"}
    catcher_gap   {float 0.0 4.0 0.1 0.4 "Catcher Gap"}
} -target {} -proc tpred_set_catchers -getter tpred_get_catchers \
  -label "Catchers (applied on next trial)"

workspace::adjuster tpred_control {
    catcher_x_min {float -8.0 8.0 0.25  0.0 "Catcher X Min"}
    catcher_x_max {float -8.0 8.0 0.25  5.0 "Catcher X Max"}
    catcher_y_min {float -6.0 4.0 0.25 -3.0 "Catcher Y Min"}
    catcher_y_max {float -6.0 4.0 0.25  0.0 "Catcher Y Max"}
    nudge_step    {float  0.5 10.0 0.5  3.0 "Angle Nudge Step (deg)"}
} -target {} -proc tpred_set_control -getter tpred_get_control \
  -label "Control Target (applied on next trial)"

workspace::adjuster tpred_start {
    randomize_start_y {bool 0 "Randomize Start Y"}
    bidirectional     {bool 1 "Bidirectional (random L/R launch)"}
} -target {} -proc tpred_set_start -getter tpred_get_start \
  -label "Start (applied on next trial)"

workspace::adjuster tpred_fix {
    fix_visible {bool 1 "Show Fixation"}
} -target {} -proc tpred_set_fix -getter tpred_get_fix \
  -label "Fixation (applied on next trial)"

workspace::adjuster tpred_scale {
    scale {float 0.2 1.2 0.05 0.7 "World Scale"}
} -target {} -proc tpred_set_scale -getter tpred_get_scale \
  -label "Scale"
