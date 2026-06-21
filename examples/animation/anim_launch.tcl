# examples/animation/anim_launch.tcl
# Launcher trajectory replay (analytic, via the launch_sim dlsh package)
# Demonstrates: animateCustom replay of a PRECOMPUTED path using
#               launch_sim::ball_pos_at_time (smooth at any refresh, no
#               snap-stepping), the arc-landing geometry, a tangent catcher,
#               and a decoupled occluder that hides the ball mid-flight.
#
# This is the analytic / kinematic counterpart to box2d/trajectory_pred.tcl:
# there is NO live physics -- launch_sim generates the trajectory headlessly
# and the ball is replayed by sampling its position each frame. The arc is
# centered on the launcher; the report space is the signed DEVIATION from the
# launch heading (0 = straight ahead).
#
# Controls (workbench):
#   Random  -- a sampled trial (slow launch, occluded "to the edge")
#   Launch  -- a deterministic launch from the Heading/Speed/Gravity sliders
#   Heading/Speed/Gravity -- manual launch params (used by Launch)
#   Occlusion -- fraction of the radial path hidden (live; rebuilds the trial)
#   Up arrow  -- repeat the current mode (new Random or re-Launch)
#
# NB: written to the examples' conventions but NOT yet run on the GL engine
# (launch_sim itself is headless-tested). Verify the look on the rig.

# --- load launch_sim (needs >= 0.2 for any-side manual launches) ------------
package require launch_sim

namespace eval launchdemo {
    variable tr           {}     ;# current trial dict
    variable occluder     {}     ;# occluder regions (decoupled)
    variable ball         ""     ;# ball obj
    variable catcher      ""     ;# true-exit catcher obj
    variable resp_catcher ""     ;# subject's swipe catcher obj
    variable replay_speed 1.0
    variable arrived      0
    variable responding   0
    variable last_setup   launchdemo_random
    variable arc_R        5.0    ;# master size: landing-arc radius (scales all)
    variable occ_prop     0.55   ;# fraction of the radial path that is hidden
    variable m_heading    45.0   ;# manual launch: heading (deg)
    variable m_speed      2.5    ;# manual launch: speed
    variable m_gravity    5.0    ;# manual launch: gravity
    variable show_endpoint 1     ;# show the green endpoint bar on arrival?
}

# ============================================================
# Visual helpers
# ============================================================
proc launchdemo_circle { x y r color } {
    set c [polygon]; polycirc $c 1
    scaleObj $c [expr {2.0*$r}] [expr {2.0*$r}]
    translateObj $c $x $y
    polycolor $c {*}$color
    return $c
}

# A short bar centered at (x,y), rotated to angle_rad (the tangent catcher).
proc launchdemo_bar { x y angle_rad color {len 1.3} {thick 0.28} } {
    set r [polygon]
    scaleObj $r $len $thick
    translateObj $r $x $y
    rotateObj $r [expr {$angle_rad*180.0/$::pi}] 0 0 1
    polycolor $r {*}$color
    return $r
}

# A thin arc band centered on the heading, span +-half, radial half-width w.
# An annular sector = polyannulus (ring) + polysector (wedge) on the unit quad:
# anti-aliased, no vertex work. polysector removes a mouth centred on +X, so the
# KEPT wedge is centred on -X -- rotate by (h+180) to aim it at the heading.
proc launchdemo_arc_band { cx cy R half h color {w 0.06} } {
    set obj [polygon]
    set span_deg [expr {2.0*$half*180.0/$::pi}]
    scaleObj $obj [expr {2.0*($R+$w)}] [expr {2.0*($R+$w)}]   ;# outer radius R+w
    polyannulus $obj [expr {($R-$w)/($R+$w)}]                 ;# inner radius R-w
    if { $span_deg < 359.0 } { polysector $obj [expr {360.0-$span_deg}] }
    polycolor $obj {*}$color
    translateObj $obj $cx $cy
    rotateObj $obj [expr {$h*180.0/$::pi + 180.0}] 0 0 1
    return $obj
}

# Draw one occluder region (rect / arc / circle) as a filled object.
proc launchdemo_region_obj { reg color } {
    switch -- [dict get $reg type] {
        rect {
            set x0 [dict get $reg x0]; set y0 [dict get $reg y0]
            set x1 [dict get $reg x1]; set y1 [dict get $reg y1]
            set r [polygon]
            scaleObj $r [expr {$x1-$x0}] [expr {$y1-$y0}]
            translateObj $r [expr {0.5*($x0+$x1)}] [expr {0.5*($y0+$y1)}]
            polycolor $r {*}$color
            return $r
        }
        circle {
            return [launchdemo_circle [dict get $reg cx] [dict get $reg cy] \
                        [dict get $reg r] $color]
        }
        arc {
            # annular sector r0..r1 spanning a0..a1 -> polyannulus + polysector
            # (AA masked quad). Kept wedge is centred on -X, so aim it at the
            # span midpoint with rotateObj.
            set cx [dict get $reg cx]; set cy [dict get $reg cy]
            set r0 [dict get $reg r0]; set r1 [dict get $reg r1]
            set a0 [dict get $reg a0]; set a1 [dict get $reg a1]
            set span_deg [expr {($a1-$a0)*180.0/$::pi}]
            set mid_deg  [expr {0.5*($a0+$a1)*180.0/$::pi}]
            set obj [polygon]
            scaleObj $obj [expr {2.0*$r1}] [expr {2.0*$r1}]
            polyannulus $obj [expr {$r0/$r1}]
            if { $span_deg < 359.0 } { polysector $obj [expr {360.0-$span_deg}] }
            polycolor $obj {*}$color
            translateObj $obj $cx $cy
            rotateObj $obj [expr {$mid_deg + 180.0}] 0 0 1
            return $obj
        }
    }
}

# ============================================================
# Per-frame driver -- replay the launch_sim trajectory
#   animateCustom passes {t dt frame obj}; t = seconds since (re)build.
# ============================================================
proc launchdemo_drive { t dt frame obj } {
    if { $launchdemo::tr eq "" } return
    set lt [dict get $launchdemo::tr land_time]
    set tt [expr {$t * $launchdemo::replay_speed}]
    if { $tt > $lt } { set tt $lt }

    lassign [launch_sim::ball_pos_at_time $launchdemo::tr $tt] x y
    translateObj $obj $x $y 0
    # NB the ball is NOT hidden here -- the occluder is drawn on top of it
    # (higher priorityObj), so it is clipped by the occluder edge as a real
    # accretion/deletion event rather than popping on/off.

    # "arrived" at the rim: a real task fires a sound + opens the response
    # window here (dserv_send); the demo just flashes the true catcher green.
    if { $tt >= $lt && !$launchdemo::arrived } {
        set launchdemo::arrived 1
        if { $launchdemo::catcher ne "" && $launchdemo::show_endpoint } {
            setVisible $launchdemo::catcher 1
        }
    }

    # response mode: slide the subject's catcher to the cursor's arc position
    if { $launchdemo::responding && $launchdemo::resp_catcher ne "" } {
        set dev [launchdemo_cursor_dev]
        lassign [launch_sim::catcher_pose $launchdemo::tr $dev] kx ky ka
        translateObj $launchdemo::resp_catcher $kx $ky 0
        rotateObj $launchdemo::resp_catcher [expr {$ka*180.0/$::pi}] 0 0 1
        setVisible $launchdemo::resp_catcher 1
    }
}

# Cursor position as a signed deviation from the heading, clamped to the arc.
proc launchdemo_cursor_dev {} {
    lassign [getMouseWorld] mx my
    set cx [dict get $launchdemo::tr arc_cx]
    set cy [dict get $launchdemo::tr arc_cy]
    set h  [dict get $launchdemo::tr heading]
    set dev [expr {atan2(sin(atan2($my-$cy,$mx-$cx)-$h), cos(atan2($my-$cy,$mx-$cx)-$h))}]
    set half [expr {[dict get $launchdemo::tr arc_span_deg]/2.0*$::pi/180.0}]
    if { $dev >  $half } { set dev $half }
    if { $dev < -$half } { set dev [expr {-$half}] }
    return $dev
}

# ============================================================
# Scene build
# ============================================================
proc launchdemo_build_scene { {with_response 0} } {
    glistInit 1
    resetObjList
    set launchdemo::arrived 0
    set launchdemo::responding 0

    set cx [dict get $launchdemo::tr arc_cx]
    set cy [dict get $launchdemo::tr arc_cy]
    set R  [dict get $launchdemo::tr arc_radius]
    set h  [dict get $launchdemo::tr heading]
    set half [expr {[dict get $launchdemo::tr arc_span_deg]/2.0*$::pi/180.0}]

    # z-order (priorityObj, higher = drawn on top): ball(0) < occluder(1) <
    # markers(2). The occluder is OPAQUE and on top of the ball, so the ball is
    # clipped by its edge -- a real accretion/deletion event, not a visibility
    # toggle. The arc guide, launcher, and catcher sit above the occluder so
    # they stay visible.
    set arc [launchdemo_arc_band $cx $cy $R $half $h {0.4 0.4 0.45}]
    priorityObj $arc 2
    glistAddObject $arc 0

    foreach reg $launchdemo::occluder {
        set ob [launchdemo_region_obj $reg {0.16 0.16 0.22}]
        priorityObj $ob 1
        glistAddObject $ob 0
    }

    # launcher (arc center)
    set lo [launchdemo_circle $cx $cy 0.22 {0.6 0.6 0.6}]
    priorityObj $lo 2
    glistAddObject $lo 0

    # catcher at the true exit -- GREEN, hidden until the ball ARRIVES (no
    # endpoint marker is shown during the prediction; the green appearing is
    # the "arrived" cue, standing in for the task's exit sound)
    lassign [launch_sim::catcher_pose $launchdemo::tr [dict get $launchdemo::tr deviation]] kx ky ka
    set launchdemo::catcher [launchdemo_bar $kx $ky $ka {0.3 1.0 0.4}]
    priorityObj $launchdemo::catcher 2
    setVisible $launchdemo::catcher 0
    glistAddObject $launchdemo::catcher 0

    # subject's swipe catcher (hidden until they respond)
    if { $with_response } {
        set launchdemo::resp_catcher [launchdemo_bar $kx $ky $ka {0.4 0.7 1.0}]
        priorityObj $launchdemo::resp_catcher 2
        setVisible $launchdemo::resp_catcher 0
        glistAddObject $launchdemo::resp_catcher 0
    } else {
        set launchdemo::resp_catcher ""
    }

    # ball -- replayed each frame from launch_sim (priority 0 = behind occluder)
    set b [launchdemo_circle 0 0 0.18 {0.2 0.9 1.0}]
    objName $b ball
    priorityObj $b 0
    set launchdemo::ball $b
    animateCustom ball -proc launchdemo_drive -params {}
    glistAddObject $b 0

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# Trial generation + setups
# ============================================================
proc launchdemo_arc_params {} {
    # launcher_jitter 0 => launcher AT the arc/occluder center (concentric,
    # clean demo geometry). The occluder is a concentric ring so the ball
    # flies out from the center and vanishes before reaching the arc.
    # arc_radius = the master size knob (launchdemo::arc_R).
    # slow launches so the ball can be pursued before it is occluded; gentle
    # gravity so the curve stays on-scale. (speed/gravity are the knobs to
    # tune the difficulty + how long it's visible.)
    return [dict merge [launch_sim::default_params] \
        [list boundary arc angle_min 0 angle_max 360 \
         speed_min 1.5 speed_max 4.0 gravity_min 2 gravity_max 8 \
         arc_radius $launchdemo::arc_R arc_span_deg 150 launcher_jitter 0]]
}

# Concentric occluder ring hiding the OUTER occ_prop of the radial path: the
# ball is visible out to r0=(1-occ_prop)*R, then hidden through the exit.
proc launchdemo_occ_ring {} {
    set R $launchdemo::arc_R
    return [list [dict create type arc cx 0 cy 0 \
                r0 [expr {(1.0-$launchdemo::occ_prop)*$R}] r1 [expr {$R+0.4}] \
                a0 [expr {-$::pi}] a1 [expr {$::pi}]]]
}

# Build a trial. manual=0 -> sample from the random ranges (the task demo);
# manual=1 -> a deterministic launch from the Heading/Speed/Gravity sliders.
proc launchdemo_new_trial { {manual 0} } {
    set occ [launchdemo_occ_ring]
    if { $manual } {
        # full arc span (accept any landing) + generous max_sim_time so even
        # slow launches reach the arc, and a low min_visible so fast ones do
        # too -- a manual launch should "just fire" for any slider combo
        set p [dict merge [launch_sim::default_params] \
            [list boundary arc launcher_jitter 0 arc_radius $launchdemo::arc_R \
             arc_span_deg 360 max_sim_time 20.0 min_visible 0.05 \
             angle_min   $launchdemo::m_heading angle_max   $launchdemo::m_heading \
             speed_min   $launchdemo::m_speed   speed_max   $launchdemo::m_speed \
             gravity_min $launchdemo::m_gravity gravity_max $launchdemo::m_gravity]]
        if { [catch {set tr [launch_sim::sample_trajectory $p -1]}] } {
            puts "launch_sim: no valid landing for those parameters"
            return 0
        }
    } else {
        set p [launchdemo_arc_params]
        set tr [launch_sim::sample_trajectory $p -1]
        # resample so the exit is hidden ("occluded to the edge")
        for { set k 0 } { $k < 200 } { incr k } {
            if { [launch_sim::point_occluded [dict get $tr land_x] \
                      [dict get $tr land_y] $occ] } break
            set tr [launch_sim::sample_trajectory $p -1]
        }
    }
    set launchdemo::tr [launch_sim::occlude $tr $occ]
    set launchdemo::occluder $occ
    return 1
}

# A random (sampled) trial -- the task demo.
proc launchdemo_random {} {
    set launchdemo::last_setup launchdemo_random
    if { [launchdemo_new_trial 0] } { launchdemo_build_scene 0 }
}
# A deterministic launch from the slider params.
proc launchdemo_manual {} {
    set launchdemo::last_setup launchdemo_manual
    if { [launchdemo_new_trial 1] } { launchdemo_build_scene 0 }
}

# ============================================================
# Input callbacks
# ============================================================
proc onUpArrow    {} { eval $launchdemo::last_setup }   ;# new trial
proc onMousePress {} {
    if { $launchdemo::resp_catcher ne "" } { set launchdemo::responding 1 }
}
proc onMouseRelease {} {
    if { !$launchdemo::responding } return
    set launchdemo::responding 0
    set dev  [launchdemo_cursor_dev]
    set true [dict get $launchdemo::tr deviation]
    set err  [expr {abs($dev-$true)*180.0/$::pi}]
    puts [format "report=%.1f  true=%.1f  error=%.1f deg" \
        [expr {$dev*180.0/$::pi}] [expr {$true*180.0/$::pi}] $err]
}

# ============================================================
# Workspace interface
# ============================================================
# Primary entry the workbench builds.
proc launchdemo_setup {} { launchdemo_random }

# Action buttons: Random (sampled trial) | Launch (the slider params).
proc launchdemo_action { action } {
    switch -- $action {
        random { launchdemo_random }
        launch { launchdemo_manual }
    }
    return
}

# Occlusion proportion -- a "live" control: rebuilds the current trial so you
# see the effect. (For an experiment IV, randomize occ_prop per trial instead.)
proc launchdemo_set_occ { prop } {
    set launchdemo::occ_prop $prop
    eval $launchdemo::last_setup
    return
}
proc launchdemo_get_occ { {target {}} } { dict create prop $launchdemo::occ_prop }

# Show/hide the green endpoint bar that appears on arrival. Updates live: when
# turned off, a trial just stays occluded (the "arrived" sound, not vision,
# would signal it in the real task).
proc launchdemo_set_endpoint { v } {
    set launchdemo::show_endpoint $v
    if { $launchdemo::catcher ne "" } {
        setVisible $launchdemo::catcher [expr {$v && $launchdemo::arrived}]
        redraw
    }
    return
}
proc launchdemo_get_endpoint { {target {}} } { dict create show $launchdemo::show_endpoint }

# Manual launch params (Heading/Speed/Gravity). Stored on change; fired by the
# Launch button -- but if we're already in Launch mode, re-fire live.
proc launchdemo_relaunch_if_manual {} {
    if { $launchdemo::last_setup eq "launchdemo_manual" } { launchdemo_manual }
}
proc launchdemo_set_heading { v } { set launchdemo::m_heading $v; launchdemo_relaunch_if_manual }
proc launchdemo_get_heading { {target {}} } { dict create heading $launchdemo::m_heading }
proc launchdemo_set_speed   { v } { set launchdemo::m_speed   $v; launchdemo_relaunch_if_manual }
proc launchdemo_get_speed   { {target {}} } { dict create speed $launchdemo::m_speed }
proc launchdemo_set_gravity { v } { set launchdemo::m_gravity $v; launchdemo_relaunch_if_manual }
proc launchdemo_get_gravity { {target {}} } { dict create gravity $launchdemo::m_gravity }

if { [llength [info commands workspace::setup]] } {
    workspace::reset
    workspace::setup launchdemo_setup {} \
        -adjusters {launchdemo_actions launchdemo_heading launchdemo_speed \
                    launchdemo_gravity launchdemo_occlusion launchdemo_endpoint} \
        -label "Launcher (analytic)"
    workspace::adjuster launchdemo_actions {
        random {action "Random"}
        launch {action "Launch"}
    } -target {} -proc launchdemo_action -label "Actions"
    workspace::adjuster launchdemo_heading {
        heading {float 0 360 5 45 "Heading" deg}
    } -target {} -proc launchdemo_set_heading -getter launchdemo_get_heading -label "Heading"
    workspace::adjuster launchdemo_speed {
        speed {float 0.5 6.0 0.1 2.5 "Speed"}
    } -target {} -proc launchdemo_set_speed -getter launchdemo_get_speed -label "Speed"
    workspace::adjuster launchdemo_gravity {
        gravity {float 0.0 12.0 0.5 5.0 "Gravity"}
    } -target {} -proc launchdemo_set_gravity -getter launchdemo_get_gravity -label "Gravity"
    workspace::adjuster launchdemo_occlusion {
        prop {float 0.1 0.95 0.05 0.55 "Occlusion"}
    } -target {} -proc launchdemo_set_occ -getter launchdemo_get_occ -label "Occlusion"
    workspace::adjuster launchdemo_endpoint {
        show {bool 1 "Show endpoint bar"}
    } -target {} -proc launchdemo_set_endpoint -getter launchdemo_get_endpoint -label "Endpoint"
}

# Build an initial trial so the demo shows something when sourced directly.
launchdemo_setup
