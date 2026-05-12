# examples/box2d/trajectory_pred.tcl
# Trajectory Prediction (Phase 1: launch-only)
#
# Monkey-task demo: ball with a launch-direction cue arcs under gravity
# toward a pair of open-faced catchers. Subject reports whether the
# ball lands in the LEFT or RIGHT catcher. Trajectory is pre-simulated
# at trial generation, then played back kinematically on response.
#
# Trial structure:
#   - Static scene shown: fixation, ball, launch arrow, catcher pair.
#     (Box2D world is paused; subject can free-view or fixate.)
#   - Subject hits L or R button (or left/right arrow).
#   - Ball is animated along the stored trajectory until it crosses the
#     catcher line; chosen catcher is colored green (correct) or red.
#   - "New Trial" regenerates the world with a fresh sampled angle.
#
# Catcher placement guarantees one catcher contains the landing point:
# we pre-simulate the full trajectory, find where it crosses catcher_y,
# randomly pick left-or-right as the correct side, then position the
# pair so the chosen catcher straddles the landing x.
#
# Controls:
#   Left Arrow  - Choose left catcher
#   Right Arrow - Choose right catcher
#   Up Arrow    - New trial

# ============================================================
# STATE
# ============================================================

namespace eval tpred {
    package require box2d

    variable bworld      ""
    variable ball        ""
    variable launch_cue  ""
    variable catcher_L_parts {}
    variable catcher_R_parts {}
    variable world_group ""
    variable world_dg    ""

    # Landing point discovered by the pre-sim (used only to place the
    # catcher pair so one catcher contains the landing x).
    variable land_x      0.0
    variable land_y      0.0
    variable land_time   0.0

    # Set to 1 once the live ball has crossed the catcher line.
    variable landed      0

    # Per-trial sampled values
    variable angle_deg         45.0   ;# angle above horizontal (0..90)
    variable launch_dir_sign   1      ;# +1 = ball launched rightward, -1 = leftward
    variable angle_display_deg 45.0   ;# actual direction angle for the cue arrow
    variable launch_vx   0.0
    variable launch_vy   0.0
    variable ball_x0     -6.0
    variable ball_y0     0.0
    variable correct_side L      ;# L or R
    variable cL_cx       0.0
    variable cR_cx       0.0

    # Trial flow
    variable response    ""       ;# "", "L", or "R"

    # World geometry (workspace units)
    variable xrange      18.0
    variable yrange      14.0
    variable catcher_y   -5.5
    variable gravity_y   -10.0

    # Launch parameters
    variable angle_min   25.0
    variable angle_max   75.0
    variable launch_speed 9.0
    variable ball_radius 0.35

    # Start position (ball is launched from the left half)
    variable start_x_min -7.5
    variable start_x_max -5.5
    variable start_y_min  -2.0
    variable start_y_max   2.0
    variable randomize_start_y 0

    # When on, the ball is launched from a random side per trial,
    # producing both left-to-right and right-to-left trajectories.
    variable bidirectional 1

    # Catcher geometry
    variable catcher_width 1.8
    variable catcher_wall_h 1.2
    variable catcher_wall_w 0.15
    variable catcher_floor_h 0.15
    variable catcher_gap   0.4   ;# inner gap between L and R catchers

    # Inverted-V plank (Phase 2). The apex sits at (apex_x, apex_y);
    # each arm has length arm_length and the full angle between the
    # two arms is apex_angle_deg (smaller = steeper V = stronger
    # deflection). Same V can bounce the ball L or R depending on
    # which arm the incoming trajectory strikes.
    variable plank_enabled       0
    variable plank_apex_x        0.0
    variable plank_apex_y        -1.0
    variable plank_apex_angle_deg 90.0
    variable plank_arm_length    2.5
    variable plank_thickness     0.15
    variable plank_restitution   0.4

    # Fixation
    variable fix_radius  0.18
    variable fix_visible 1

    # World transform
    variable world_scale 0.7

    # Sim parameters
    variable sim_max_time 4.0
    variable max_attempts 200
}

# ============================================================
# TRAJECTORY GENERATION (rejection sampling)
# ============================================================

# Pre-simulate one trial. Returns 1 on success (and fills tpred:: state),
# 0 if no acceptable trajectory was found.
proc tpred_sample_trial {} {
    set step  [expr {[screen_set FrameDuration] / 1000.0}]
    if { $step <= 0 } { set step 0.01667 }

    set xr2 [expr {$tpred::xrange / 2.0}]
    set yr2 [expr {$tpred::yrange / 2.0}]

    # Ball comes to rest with its bottom on top of the catcher plate.
    set rest_y [expr {$tpred::catcher_y \
                      + $tpred::catcher_floor_h / 2.0 \
                      + $tpred::ball_radius}]

    for { set attempt 0 } { $attempt < $tpred::max_attempts } { incr attempt } {
        # Sample launch angle, side, optional start y.
        set ang [expr {$tpred::angle_min + \
                       rand() * ($tpred::angle_max - $tpred::angle_min)}]
        # Direction: +1 launches rightward (ball starts on the left side),
        # -1 launches leftward (ball starts mirrored on the right side).
        if { $tpred::bidirectional } {
            set sign [expr {rand() < 0.5 ? -1 : 1}]
        } else {
            set sign 1
        }
        set rad [expr {$ang * $::pi / 180.0}]
        set vx  [expr {$sign * $tpred::launch_speed * cos($rad)}]
        set vy  [expr {$tpred::launch_speed * sin($rad)}]

        # Sample start x in the configured range; mirror to the opposite
        # side when launching leftward so the trajectory still has room
        # to arc across the field of view.
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

        # Build a throwaway physics world to record the trajectory.
        set world [box2d::createWorld]

        # Inverted-V plank: two static angled boxes, created BEFORE the
        # ball so they exist when the simulation steps. Same V geometry
        # is used in the live world so live physics reproduces the sim.
        if { $tpred::plank_enabled } {
            lassign [tpred_plank_geom] \
                r_cx r_cy r_ang l_cx l_cy l_ang arm_L arm_T
            set rb [box2d::createBox $world plank_r 0 \
                        $r_cx $r_cy $arm_L $arm_T $r_ang]
            set lb [box2d::createBox $world plank_l 0 \
                        $l_cx $l_cy $arm_L $arm_T $l_ang]
            box2d::setRestitution $world $rb $tpred::plank_restitution
            box2d::setRestitution $world $lb $tpred::plank_restitution
        }

        set body  [box2d::createCircle $world ball 2 $sx $sy $tpred::ball_radius]
        box2d::setLinearVelocity $world $body $vx $vy

        set ts {}; set xs {}; set ys {}
        set ok 1
        set crossed 0
        set land_i 0
        set i 0
        for { set t 0.0 } { $t < $tpred::sim_max_time } { set t [expr {$t + $step}] } {
            box2d::step $world $step
            lassign [box2d::getBodyInfo $world $body] bx by _
            lappend ts $t
            lappend xs $bx
            lappend ys $by

            # Reject if ball leaves the visible field before reaching catcher_y.
            if { $bx < -$xr2 || $bx > $xr2 || $by > $yr2 } {
                set ok 0
                break
            }
            # Stop when ball's bottom reaches the top of the catcher plate.
            if { !$crossed && $by <= $rest_y } {
                set crossed 1
                set land_i $i
                incr i
                break
            }
            incr i
        }
        box2d::destroy $world

        if { !$ok || !$crossed } { continue }

        # Accept this trial. Live playback re-runs the physics, so we
        # only need to remember the launch vector and the landing
        # point (the latter drives catcher placement). The display
        # angle is the direction of the launch vector in world coords,
        # so the cue arrow points the right way for either direction.
        set tpred::angle_deg         $ang
        set tpred::launch_dir_sign   $sign
        set tpred::angle_display_deg [expr {atan2($vy, $vx) * 180.0 / $::pi}]
        set tpred::launch_vx $vx
        set tpred::launch_vy $vy
        set tpred::ball_x0   $sx
        set tpred::ball_y0   $sy
        set tpred::land_x    [lindex $xs $land_i]
        set tpred::land_y    [lindex $ys $land_i]
        set tpred::land_time [lindex $ts $land_i]
        return 1
    }
    return 0
}

# ============================================================
# CATCHER PLACEMENT (one catcher always contains landing x)
# ============================================================

proc tpred_place_catchers {} {
    set w     $tpred::catcher_width
    set gap   $tpred::catcher_gap
    set lx    $tpred::land_x

    # Pick L or R as the correct catcher (random).
    if { rand() < 0.5 } {
        set tpred::correct_side L
    } else {
        set tpred::correct_side R
    }

    # Inner edges of the two catchers (the gap is centered between them).
    # Place pair so the chosen catcher's interior straddles lx with a
    # small random offset (so the ball doesn't always land dead-center).
    set half_w  [expr {$w / 2.0}]
    set inset   [expr {$tpred::catcher_wall_w * 1.5}]   ;# stay clear of walls
    set jitter_max [expr {$half_w - $inset}]
    if { $jitter_max < 0 } { set jitter_max 0 }
    set jitter [expr {(2.0 * rand() - 1.0) * $jitter_max}]

    if { $tpred::correct_side eq "L" } {
        # Left catcher center sits left of the gap.
        set tpred::cL_cx [expr {$lx - $jitter}]
        set tpred::cR_cx [expr {$tpred::cL_cx + $w + $gap}]
    } else {
        set tpred::cR_cx [expr {$lx - $jitter}]
        set tpred::cL_cx [expr {$tpred::cR_cx - $w - $gap}]
    }
}

# ============================================================
# PLANK GEOMETRY
# ============================================================

# Returns the per-arm box geometry for the current inverted-V plank.
# Result: { right_cx right_cy right_angle_rad   left_cx left_cy left_angle_rad
#          arm_length arm_thickness }
# Both arms share the same length and thickness; only center and rotation
# differ. The apex sits at (apex_x, apex_y).
proc tpred_plank_geom {} {
    set apex_x  $tpred::plank_apex_x
    set apex_y  $tpred::plank_apex_y
    set L       $tpred::plank_arm_length
    set thick   $tpred::plank_thickness
    set half    [expr {$tpred::plank_apex_angle_deg * $::pi / 360.0}] ;# half angle, rad
    set halfL   [expr {$L / 2.0}]

    # Right arm: from apex pointing toward (sin half, -cos half).
    set rdx [expr { sin($half)}]
    set rdy [expr {-cos($half)}]
    set r_cx [expr {$apex_x + $halfL * $rdx}]
    set r_cy [expr {$apex_y + $halfL * $rdy}]
    set r_ang [expr {atan2($rdy, $rdx)}]

    # Left arm: mirror across the vertical through apex.
    set ldx [expr {-sin($half)}]
    set ldy [expr {-cos($half)}]
    set l_cx [expr {$apex_x + $halfL * $ldx}]
    set l_cy [expr {$apex_y + $halfL * $ldy}]
    set l_ang [expr {atan2($ldy, $ldx)}]

    return [list $r_cx $r_cy $r_ang $l_cx $l_cy $l_ang $L $thick]
}

# ============================================================
# VISUAL HELPERS
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

# Plain (visual-only) circle, no physics body.
proc tpred_visual_circle { cx cy radius color } {
    set c [tpred_make_circle]
    scaleObj $c [expr {2.0*$radius}] [expr {2.0*$radius}]
    translateObj $c $cx $cy
    polycolor $c {*}$color
    return $c
}

# Box2D-backed static box: real body + linked visual. Used for the
# inverted-V plank arms so the live ball physically bounces off them.
# Same setObjMatrix dance as the body circle so the visual picks up
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

# Box2D-backed static circle: real body in the visualization world,
# linked to its polygon. Created as static (type 0) so it sits still
# during the analyze epoch; tpred_respond flips it to dynamic and
# applies the launch velocity. Matches the planko_catch pattern.
proc tpred_create_body_circle { bworld name tx ty radius color } {
    set body [Box2D_createCircle $bworld $name 0 $tx $ty $radius]  ;# 0 = static
    set c    [tpred_make_circle]
    scaleObj $c [expr {2.0*$radius}] [expr {2.0*$radius}]
    polycolor $c {*}$color
    # Enable the obj's use_matrix flag and seed it with the start
    # position. Box2D's per-frame link update writes into GR_MATRIX;
    # without setObjMatrix the obj draws from its translateObj state
    # and ignores those writes -- that's why the ball would never move.
    set m [dl_tcllist [mat4_createTranslationAngle $tx $ty 0.0]]
    setObjMatrix $c {*}$m
    Box2D_linkObj $bworld $body $c
    setObjProp $c body   $body
    setObjProp $c bworld $bworld
    return $c
}

# Build a catcher footprint (bottom plate only) at center cx, sitting
# on the catcher line at catcher_y. With realtime physics playback,
# side walls would be visual-only -- the ball would arc through them
# on its way down -- so we draw just the floor segment. The L/R
# decision is read from the ball's x when it crosses catcher_y.
proc tpred_build_catcher { cx tag color } {
    set w       $tpred::catcher_width
    set floor_h $tpred::catcher_floor_h
    set y       $tpred::catcher_y

    set parts {}
    set b [tpred_visual_rect $cx $y $w $floor_h $color]
    objName $b "catch_${tag}_b"
    lappend parts $b
    return $parts
}

# A simple arrow polygon (triangle) pointing along +x in its local frame.
# Length = total length, width = base width. Tip is at (length, 0).
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

# Build a small fixation cross.
proc tpred_build_fix {} {
    set mg [metagroup]
    set h [polygon]
    set r $tpred::fix_radius
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
# KINEMATIC PLAYBACK
# ============================================================

# Watcher (post-script on the bworld). Runs each frame after the world
# steps; detects when the dynamic ball crosses the catcher line and
# converts it back to kinematic so it stops cleanly inside the catcher.
proc tpred_check_landing { bworld } {
    if { $tpred::landed } { return }
    if { $tpred::response eq "" } { return }
    set body [setObjProp $tpred::ball body]
    lassign [Box2D_getBodyInfo $bworld $body] bx by _

    # Ball comes to rest with its bottom on top of the catcher plate.
    set rest_y [expr {$tpred::catcher_y \
                      + $tpred::catcher_floor_h / 2.0 \
                      + $tpred::ball_radius}]

    if { $by <= $rest_y } {
        set tpred::landed 1
        # Freeze the ball and snap to the exact rest position so it
        # doesn't visually overshoot into the plate between frames.
        Box2D_setBodyType $bworld $body 1   ;# kinematic
        Box2D_setLinearVelocity $bworld $body 0 0
        Box2D_updateTransform $bworld $body $bx $rest_y
        tpred_show_feedback
    }
}

proc tpred_show_feedback {} {
    if { $tpred::response eq "" } { return }
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

# ============================================================
# SETUP / NEW TRIAL
# ============================================================

proc tpred_setup {} {
    if { $tpred::world_dg ne "" && [dg_exists $tpred::world_dg] } {
        dg_delete $tpred::world_dg
    }
    set tpred::world_dg ""

    if { ![tpred_sample_trial] } {
        error "tpred_setup: failed to sample a valid trajectory"
    }
    tpred_place_catchers

    set tpred::response ""
    set tpred::landed   0

    resetObjList
    glistInit 1

    # Background "world" object (kinematic stepper not used for playback,
    # but having a Box2D obj gives us a place to hang PreScripts and a
    # consistent group root that can be transformed).
    set bworld [Box2D]
    set tpred::bworld $bworld
    glistAddObject $bworld 0

    set grp [metagroup]
    objName $grp tpred_group
    set tpred::world_group $grp

    # Catcher pair (built first so they draw behind the ball).
    set cat_color { 0.75 0.75 0.78 1.0 }
    set tpred::catcher_L_parts [tpred_build_catcher $tpred::cL_cx L $cat_color]
    foreach p $tpred::catcher_L_parts { metagroupAdd $grp $p }
    set tpred::catcher_R_parts [tpred_build_catcher $tpred::cR_cx R $cat_color]
    foreach p $tpred::catcher_R_parts { metagroupAdd $grp $p }

    # Catcher-line reference (faint horizontal line, visual only) -
    # drawn just below the catcher floor plates.
    set line [tpred_visual_rect 0 \
                  [expr {$tpred::catcher_y - $tpred::catcher_floor_h/2.0 - 0.04}] \
                  $tpred::xrange 0.04 { 0.35 0.35 0.4 1.0 }]
    metagroupAdd $grp $line

    # Inverted-V plank (Phase 2). Two static linked boxes form the V.
    # Live physics bounces the ball off whichever arm it hits; the
    # pre-sim used the same geometry, so the recorded land_x already
    # reflects the post-bounce trajectory.
    if { $tpred::plank_enabled } {
        lassign [tpred_plank_geom] \
            r_cx r_cy r_ang l_cx l_cy l_ang arm_L arm_T
        set plank_color { 0.85 0.85 0.9 1.0 }
        set pr [tpred_create_body_box $bworld plank_r \
                    $r_cx $r_cy $arm_L $arm_T $r_ang $plank_color]
        Box2D_setRestitution $bworld [setObjProp $pr body] \
            $tpred::plank_restitution
        metagroupAdd $grp $pr
        set pl [tpred_create_body_box $bworld plank_l \
                    $l_cx $l_cy $arm_L $arm_T $l_ang $plank_color]
        Box2D_setRestitution $bworld [setObjProp $pl body] \
            $tpred::plank_restitution
        metagroupAdd $grp $pl
    }

    # Launch arrow (direction cue). The display angle reflects the
    # actual direction of the launch vector, so the arrow points the
    # right way for both leftward and rightward launches.
    set arrow_len   [expr {$tpred::launch_speed * 0.18}]
    set arrow_width 0.32
    set cue [tpred_build_arrow \
                 $tpred::ball_x0 $tpred::ball_y0 \
                 $tpred::angle_display_deg $arrow_len $arrow_width \
                 { 1.0 0.85 0.2 1.0 }]
    objName $cue tpred_launch_cue
    set tpred::launch_cue $cue
    metagroupAdd $grp $cue

    # Ball as a kinematic Box2D body — linked visual is moved by Box2D's
    # per-frame sync whenever we call Box2D_setTransform on the body.
    set ball_obj [tpred_create_body_circle \
                      $bworld ball \
                      $tpred::ball_x0 $tpred::ball_y0 $tpred::ball_radius \
                      { 0.2 0.85 1.0 1.0 }]
    objName $ball_obj tpred_ball
    set tpred::ball $ball_obj
    metagroupAdd $grp $ball_obj

    # Fixation (drawn last so it's on top).
    if { $tpred::fix_visible } {
        set fx [tpred_build_fix]
        objName $fx tpred_fix
        metagroupAdd $grp $fx
    }

    glistAddObject $grp 0

    # Landing watcher runs after each world step.
    addPostScript $bworld [list tpred_check_landing $bworld]

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    setBackground 25 25 30

    tpred_set_scale $tpred::world_scale
    redraw
}

# ============================================================
# RESPONSE / PLAYBACK
# ============================================================

proc tpred_respond { side } {
    if { $tpred::response ne "" } { return }       ;# already responded
    if { $side ne "L" && $side ne "R" } { return }
    set tpred::response $side

    # Hide the launch cue once a response is in.
    if { $tpred::launch_cue ne "" } {
        setVisible $tpred::launch_cue 0
    }

    # Live physics playback: flip the ball to dynamic and apply the
    # sampled launch velocity. The visualization world's per-frame
    # b2World_Step takes it from there; tpred_check_landing (a
    # post-script on the world) detects when the ball crosses the
    # catcher line and triggers feedback.
    set body [setObjProp $tpred::ball body]
    Box2D_setBodyType $tpred::bworld $body 2          ;# dynamic
    Box2D_setLinearVelocity $tpred::bworld $body \
        $tpred::launch_vx $tpred::launch_vy
    redraw
}

proc tpred_trigger { action } {
    switch $action {
        left   { tpred_respond L }
        right  { tpred_respond R }
        new    { tpred_setup }
    }
    return
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

proc tpred_set_plank { plank_enabled apex_x apex_y apex_angle_deg arm_length
                       plank_restitution } {
    set tpred::plank_enabled        $plank_enabled
    set tpred::plank_apex_x         $apex_x
    set tpred::plank_apex_y         $apex_y
    set tpred::plank_apex_angle_deg $apex_angle_deg
    set tpred::plank_arm_length     $arm_length
    set tpred::plank_restitution    $plank_restitution
    return
}
proc tpred_get_plank { {target {}} } {
    dict create \
        plank_enabled     $tpred::plank_enabled \
        apex_x            $tpred::plank_apex_x \
        apex_y            $tpred::plank_apex_y \
        apex_angle_deg    $tpred::plank_apex_angle_deg \
        arm_length        $tpred::plank_arm_length \
        plank_restitution $tpred::plank_restitution
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
# KEYBOARD
# ============================================================

proc onLeftArrow  {} { tpred_trigger left  }
proc onRightArrow {} { tpred_trigger right }
proc onUpArrow    {} { tpred_trigger new   }

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup tpred_setup {} \
    -adjusters {tpred_actions tpred_launch tpred_plank tpred_catchers tpred_start tpred_fix tpred_scale} \
    -label "Trajectory Prediction (Launch)"

workspace::adjuster tpred_actions {
    left  {action "Left  (←)"}
    right {action "Right (→)"}
    new   {action "New Trial (↑)"}
} -target {} -proc tpred_trigger \
  -label "Response"

workspace::adjuster tpred_launch {
    angle_min    {float -85.0 85.0 1.0 25.0 "Angle Min (deg above horizontal)"}
    angle_max    {float -85.0 85.0 1.0 75.0 "Angle Max (deg above horizontal)"}
    launch_speed {float   0.0 16.0 0.25 9.0 "Launch Speed"}
} -target {} -proc tpred_set_launch -getter tpred_get_launch \
  -label "Launch (applied on next trial)"

workspace::adjuster tpred_plank {
    plank_enabled     {bool  0 "Enable Inverted-V Plank"}
    apex_x            {float -6.0  6.0 0.25  0.0 "Apex X"}
    apex_y            {float -5.0  5.0 0.25 -1.0 "Apex Y"}
    apex_angle_deg    {float 30.0 150.0 5.0 90.0 "Apex Angle (smaller = steeper V)"}
    arm_length        {float  0.5  5.0 0.25  2.5 "Arm Length"}
    plank_restitution {float  0.0  1.0 0.05  0.4 "Restitution (bounciness)"}
} -target {} -proc tpred_set_plank -getter tpred_get_plank \
  -label "Plank (applied on next trial)"

workspace::adjuster tpred_catchers {
    catcher_y     {float -7.0 -1.0 0.25 -5.5 "Catcher Y (raise = easier)"}
    catcher_width {float 0.8 4.0 0.1 1.8 "Catcher Width"}
    catcher_gap   {float 0.0 4.0 0.1 0.4 "Catcher Gap"}
} -target {} -proc tpred_set_catchers -getter tpred_get_catchers \
  -label "Catchers (applied on next trial)"

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
