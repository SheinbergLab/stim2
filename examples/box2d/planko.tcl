# examples/box2d/planko.tcl
# Planko - Ball drop through static planks
# Demonstrates: Box2D physics, dg/dl world storage, polygon visualization
#
# A ball drops through randomly placed static planks into catchers.
# The world is defined using dynamic groups (dg) with typed columns,
# then built into a Box2D physics world linked to visual polygons.
# This is the same data structure used in real Planko experiments.
#
# The ball trajectory is pre-simulated and stored, then played back
# on "drop" using kinematic body updates (matching the approach used
# in actual experiments for guaranteed reproducibility).
#
# Controls:
#   Down Arrow - Drop the ball (play stored trajectory)
#   Up Arrow   - Generate new board and reset

# ============================================================
# STATE
# ============================================================

namespace eval planko {
    package require box2d
    variable bworld ""
    variable ball ""
    variable world_dg ""

    # Stored trajectory from simulation
    variable traj_t {}
    variable traj_x {}
    variable traj_y {}
    variable land_time 6.0
    variable land_side ""

    variable nplanks 10
    variable plank_width 3.0
    variable ball_radius 0.5
    variable restitution 0.2
    variable xrange 16
    variable yrange 12
    variable catcher_y -7.5
    variable ball_start_y 8.0
    variable minplanks 1
}

# ============================================================
# DG WORLD CREATION (experiment-style data structure)
# ============================================================

proc planko_create_ball_dg {} {
    set g [dg_create]

    dl_set $g:name [dl_slist ball]
    dl_set $g:shape [dl_slist Circle]
    dl_set $g:type 0
    dl_set $g:tx [dl_flist 0]
    dl_set $g:ty [dl_flist $planko::ball_start_y]
    dl_set $g:sx [dl_flist $planko::ball_radius]
    dl_set $g:sy [dl_flist $planko::ball_radius]
    dl_set $g:angle [dl_flist 0.0]
    dl_set $g:restitution [dl_flist $planko::restitution]

    return $g
}

proc planko_create_catcher_dg { tx name } {
    set catcher_y $planko::catcher_y
    set y [expr {$catcher_y - (0.5 + 0.5/2)}]

    set g [dg_create]

    dl_set $g:name [dl_slist ${name}_b ${name}_r ${name}_l]
    dl_set $g:shape [dl_repeat [dl_slist Box] 3]
    dl_set $g:type [dl_repeat 0 3]
    dl_set $g:tx [dl_flist $tx [expr {$tx+2.5}] [expr {$tx-2.5}]]
    dl_set $g:ty [dl_flist $y $catcher_y $catcher_y]
    dl_set $g:sx [dl_flist 5 0.5 0.5]
    dl_set $g:sy [dl_flist 0.5 2 2]
    dl_set $g:angle [dl_zeros 3.]
    dl_set $g:restitution [dl_zeros 3.]

    return $g
}

proc planko_create_plank_dg {} {
    set n $planko::nplanks
    set xrange $planko::xrange
    set xrange_2 [expr {$xrange/2}]
    set yrange $planko::yrange
    set yrange_2 [expr {$yrange/2}]

    set g [dg_create]

    dl_set $g:name [dl_paste [dl_repeat [dl_slist plank] $n] [dl_fromto 0 $n]]
    dl_set $g:shape [dl_repeat [dl_slist Box] $n]
    dl_set $g:type [dl_repeat 0 $n]
    dl_set $g:tx [dl_sub [dl_mult $xrange [dl_urand $n]] $xrange_2]
    dl_set $g:ty [dl_sub [dl_mult $yrange [dl_urand $n]] $yrange_2]
    dl_set $g:sx [dl_repeat $planko::plank_width $n]
    dl_set $g:sy [dl_repeat 0.5 $n]
    dl_set $g:angle [dl_mult 2 $::pi [dl_urand $n]]
    dl_set $g:restitution [dl_repeat $planko::restitution $n]

    return $g
}

proc planko_make_world {} {
    set planks [planko_create_plank_dg]
    set left_catcher [planko_create_catcher_dg -3 catchl]
    set right_catcher [planko_create_catcher_dg 3 catchr]
    set ball [planko_create_ball_dg]

    foreach p [list $ball $left_catcher $right_catcher] {
        dg_append $planks $p
        dg_delete $p
    }

    return $planks
}

# ============================================================
# PHYSICS SIMULATION (for board acceptance and trajectory recording)
# ============================================================

proc planko_build_sim_world { dg } {
    set world [box2d::createWorld]
    set n [dl_length $dg:name]

    for { set i 0 } { $i < $n } { incr i } {
        foreach v {name shape type tx ty sx sy angle restitution} {
            set $v [dl_get $dg:$v $i]
        }
        if { $shape == "Box" } {
            set body [box2d::createBox $world $name $type $tx $ty $sx $sy $angle]
        } elseif { $shape == "Circle" } {
            set body [box2d::createCircle $world $name $type $tx $ty $sx]
        }
        box2d::setRestitution $world $body $restitution

        if { $name == "ball" } { set ball $body }
    }

    return "$world $ball"
}

proc planko_test_simulation { world ball { simtime 6 } } {
    set step [expr {[screen_set FrameDuration]/1000.0}]
    box2d::setBodyType $world $ball 2

    # Record trajectory
    set traj_t [list]
    set traj_x [list]
    set traj_y [list]
    set contacts {}

    for { set t 0 } { $t < $simtime } { set t [expr {$t+$step}] } {
        box2d::step $world $step
        if { [box2d::getContactBeginEventCount $world] } {
            lappend contacts [box2d::getContactBeginEvents $world]
        }
        lassign [box2d::getBodyInfo $world $ball] tx ty a
        lappend traj_t $t
        lappend traj_x $tx
        lappend traj_y $ty
    }

    return [list $traj_t $traj_x $traj_y $contacts]
}

proc planko_accept_board { traj_t traj_x traj_y contacts } {
    set catcher_y $planko::catcher_y
    set upper [expr {$catcher_y+0.01}]
    set lower [expr {$catcher_y-0.01}]

    # Final position
    set x [lindex $traj_x end]
    set y [lindex $traj_y end]

    if { $y < $upper && $y > $lower } {
        set result [expr {$x > 0}]
    } else {
        return "-1 0 0"
    }

    set planks [lmap c $contacts \
        { expr { [string match plank* [lindex $c 0]] \
                     ? [lindex [lindex $c 0] 0] : [continue] } }]
    # unique planks
    set seen {}
    set planks [lmap p $planks { if {$p in $seen} continue; lappend seen $p; set p }]
    set nhit [llength $planks]
    if { $nhit < $planko::minplanks } { return "-1 0 0" }

    # Find landing time: first time y reaches catcher level
    set land_time 0
    set n [llength $traj_y]
    for { set i 0 } { $i < $n } { incr i } {
        if { [lindex $traj_y $i] < $upper } {
            set land_time [lindex $traj_t $i]
            break
        }
    }

    return "$result $nhit $land_time"
}

proc planko_generate_world {} {
    set done 0
    while { !$done } {
        box2d::destroy all
        set new_world [planko_make_world]
        lassign [planko_build_sim_world $new_world] world ball
        lassign [planko_test_simulation $world $ball] traj_t traj_x traj_y contacts
        lassign [planko_accept_board $traj_t $traj_x $traj_y $contacts] \
            result nhit land_time
        if { $result != -1 } {
            # Store trajectory for playback
            set planko::traj_t $traj_t
            set planko::traj_x $traj_x
            set planko::traj_y $traj_y
            set planko::land_time $land_time
            set planko::land_side [expr {$result ? "right" : "left"}]
            return $new_world
        } else {
            dg_delete $new_world
        }
    }
}

# ============================================================
# TRAJECTORY PLAYBACK
# ============================================================

proc planko_update_position { ball body start } {
    set now [expr {($::StimTime - $start) / 1000.0}]

    # Find first trajectory sample past current time
    set n [llength $planko::traj_t]
    for { set i 0 } { $i < $n } { incr i } {
        if { [lindex $planko::traj_t $i] > $now } {
            set x [lindex $planko::traj_x $i]
            set y [lindex $planko::traj_y $i]
            Box2D_setTransform $planko::bworld [setObjProp $ball body] $x $y
            return
        }
    }
    # Past end of trajectory - hold final position
    set x [lindex $planko::traj_x end]
    set y [lindex $planko::traj_y end]
    Box2D_setTransform $planko::bworld [setObjProp $ball body] $x $y
}

# ============================================================
# VISUAL HELPERS
# ============================================================

proc planko_make_rect {} {
    set s [polygon]
    return $s
}

proc planko_make_circle {} {
    set circ [polygon]
    polycirc $circ 1
    return $circ
}

proc planko_create_visual_box { bworld name type tx ty sx sy {angle 0} {color {1 1 1}} } {
    set body [Box2D_createBox $bworld $name $type $tx $ty $sx $sy $angle]

    set box [planko_make_rect]
    scaleObj $box [expr {1.0*$sx}] [expr {1.0*$sy}]
    translateObj $box $tx $ty
    rotateObj $box $angle 0 0 1
    polycolor $box {*}$color

    set degrees [expr {$angle * (180.0 / $::pi)}]
    set m [dl_tcllist [mat4_createTranslationAngle $tx $ty $degrees]]
    setObjMatrix $box {*}$m

    Box2D_linkObj $bworld $body $box
    setObjProp $box body $body
    setObjProp $box bworld $bworld

    return $box
}

proc planko_create_visual_circle { bworld name type tx ty radius {angle 0} {color {1 1 1}} } {
    set body [Box2D_createCircle $bworld $name $type $tx $ty $radius $angle]

    set circ [planko_make_circle]
    scaleObj $circ [expr {2.0*$radius}] [expr {2.0*$radius}]
    translateObj $circ $tx $ty
    polycolor $circ {*}$color

    set degrees [expr {$angle * (180.0 / $::pi)}]
    set m [dl_tcllist [mat4_createTranslationAngle $tx $ty $degrees]]
    setObjMatrix $circ {*}$m

    Box2D_linkObj $bworld $body $circ
    setObjProp $circ body $body
    setObjProp $circ bworld $bworld

    return $circ
}

# ============================================================
# SETUP
# ============================================================

proc planko_setup { nplanks } {
    set planko::nplanks $nplanks

    # Clean up previous world dg
    if { $planko::world_dg ne "" && [dg_exists $planko::world_dg] } {
        dg_delete $planko::world_dg
    }

    # Generate an accepted world (also records trajectory)
    set dg [planko_generate_world]
    set planko::world_dg $dg

    # Build the visual scene from the dg
    resetObjList
    glistInit 1

    set bworld [Box2D]
    set planko::bworld $bworld
    glistAddObject $bworld 0

    set n [dl_length $dg:name]
    for { set i 0 } { $i < $n } { incr i } {
        foreach v {name shape type tx ty sx sy angle restitution} {
            set $v [dl_get $dg:$v $i]
        }
        if { $shape == "Box" } {
            set obj [planko_create_visual_box $bworld $name $type \
                         $tx $ty $sx $sy $angle { 0.9 0.9 0.9 0.8 }]
        } elseif { $shape == "Circle" } {
            set obj [planko_create_visual_circle $bworld $name $type \
                         $tx $ty $sx $angle { 0 1 1 }]
        }
        Box2D_setRestitution $bworld [setObjProp $obj body] $restitution
        glistAddObject $obj 0

        if { $name == "ball" } { set planko::ball $obj }
    }

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1

    setBackground 128 128 128

    redraw
}

# ============================================================
# ACTIONS
# ============================================================

proc planko_trigger { action } {
    switch $action {
        drop {
            if { $planko::ball ne "" } {
                # Kinematic playback from stored trajectory
                set body [setObjProp $planko::ball body]
                Box2D_setBodyType $planko::bworld $body 1 ;# kinematic
                addPreScript $planko::ball \
                    "planko_update_position $planko::ball $body $::StimTime"
            }
        }
        reset {
            planko_setup $planko::nplanks
        }
    }
    return
}

# ============================================================
# ADJUSTERS
# ============================================================

proc planko_set_physics { restitution } {
    set planko::restitution $restitution
    return
}

proc planko_get_physics { {target {}} } {
    dict create restitution $planko::restitution
}

proc planko_set_plank { plank_width } {
    set planko::plank_width $plank_width
    return
}

proc planko_get_plank { {target {}} } {
    dict create plank_width $planko::plank_width
}

proc planko_set_ball { ball_radius } {
    set planko::ball_radius $ball_radius
    return
}

proc planko_get_ball { {target {}} } {
    dict create ball_radius $planko::ball_radius
}

# ============================================================
# KEYBOARD
# ============================================================

proc onDownArrow {} { planko_trigger drop }
proc onUpArrow {} { planko_trigger reset }

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup planko_setup {
    nplanks {int 3 30 1 10 "Number of Planks"}
} -adjusters {planko_actions planko_physics planko_plank planko_ball} \
  -label "Planko (Static Planks)"

workspace::adjuster planko_actions {
    drop  {action "Drop Ball (↓)"}
    reset {action "New Board (↑)"}
} -target {} -proc planko_trigger \
  -label "Actions"

workspace::adjuster planko_physics {
    restitution {float 0.0 1.0 0.05 0.2 "Restitution (Bounciness)"}
} -target {} -proc planko_set_physics -getter planko_get_physics \
  -label "Physics (applied on reset)"

workspace::adjuster planko_plank {
    plank_width {float 1.0 6.0 0.5 3.0 "Plank Width"}
} -target {} -proc planko_set_plank -getter planko_get_plank \
  -label "Plank Geometry (applied on reset)"

workspace::adjuster planko_ball {
    ball_radius {float 0.2 1.5 0.1 0.5 "Ball Radius"}
} -target {} -proc planko_set_ball -getter planko_get_ball \
  -label "Ball (applied on reset)"
