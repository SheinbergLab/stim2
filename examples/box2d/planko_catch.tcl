# examples/box2d/planko_catch.tcl
# Planko Catch - Predict where the ball will land!
# Demonstrates: Trajectory playback, interactive prediction task
#
# A ball sits atop a board of randomly placed planks. The user
# positions a single catcher using a slider, predicting where
# the ball will land. Pressing "Drop" plays back the pre-simulated
# trajectory. The catcher turns green on a catch, red on a miss.
#
# This mirrors the experimental paradigm: the trajectory is
# pre-simulated and stored at board generation time, then played
# back deterministically on drop.
#
# Controls:
#   Catcher X slider - Position the catcher left/right
#   Drop Ball        - Play the stored trajectory
#   New Board        - Generate a new board and reset

# ============================================================
# STATE
# ============================================================

namespace eval pcatch {
    package require box2d
    variable bworld ""
    variable ball ""
    variable catcher_obj ""
    variable catcher_body ""
    variable world_dg ""

    # Stored trajectory from simulation
    variable traj_t {}
    variable traj_x {}
    variable traj_y {}
    variable land_time 6.0
    variable land_x 0.0
    variable dropped 0
    variable caught -1 ;# -1=pending, 0=missed, 1=caught

    # Parameters
    variable nplanks 5
    variable plank_width 3.0
    variable ball_radius 0.5
    variable restitution 0.3
    variable xrange 16
    variable yrange 12
    variable catcher_y -7.5
    variable catcher_x 0.0
    variable catcher_width 5.0
    variable ball_start_y 8.0
}

# ============================================================
# DG WORLD CREATION
# ============================================================

proc pcatch_create_ball_dg {} {
    set g [dg_create]

    dl_set $g:name [dl_slist ball]
    dl_set $g:shape [dl_slist Circle]
    dl_set $g:type 0
    dl_set $g:tx [dl_flist 0]
    dl_set $g:ty [dl_flist $pcatch::ball_start_y]
    dl_set $g:sx [dl_flist $pcatch::ball_radius]
    dl_set $g:sy [dl_flist $pcatch::ball_radius]
    dl_set $g:angle [dl_flist 0.0]
    dl_set $g:restitution [dl_flist $pcatch::restitution]

    return $g
}

proc pcatch_create_catcher_dg { tx } {
    set catcher_y $pcatch::catcher_y
    set w $pcatch::catcher_width
    set wall_h 2.0
    set floor_h 0.5
    set y_floor [expr {$catcher_y - ($floor_h + $wall_h/2.0)}]
    set half_w [expr {$w / 2.0}]

    set g [dg_create]

    # bottom, right wall, left wall
    dl_set $g:name [dl_slist catcher_b catcher_r catcher_l]
    dl_set $g:shape [dl_repeat [dl_slist Box] 3]
    dl_set $g:type [dl_repeat 0 3]
    dl_set $g:tx [dl_flist $tx [expr {$tx+$half_w}] [expr {$tx-$half_w}]]
    dl_set $g:ty [dl_flist $y_floor $catcher_y $catcher_y]
    dl_set $g:sx [dl_flist $w 0.5 0.5]
    dl_set $g:sy [dl_flist $floor_h $wall_h $wall_h]
    dl_set $g:angle [dl_zeros 3.]
    dl_set $g:restitution [dl_zeros 3.]

    return $g
}

proc pcatch_create_plank_dg {} {
    set n $pcatch::nplanks
    set xrange $pcatch::xrange
    set xrange_2 [expr {$xrange/2}]
    set yrange $pcatch::yrange
    set yrange_2 [expr {$yrange/2}]

    set g [dg_create]

    dl_set $g:name [dl_paste [dl_repeat [dl_slist plank] $n] [dl_fromto 0 $n]]
    dl_set $g:shape [dl_repeat [dl_slist Box] $n]
    dl_set $g:type [dl_repeat 0 $n]
    dl_set $g:tx [dl_sub [dl_mult $xrange [dl_urand $n]] $xrange_2]
    dl_set $g:ty [dl_sub [dl_mult $yrange [dl_urand $n]] $yrange_2]
    dl_set $g:sx [dl_repeat $pcatch::plank_width $n]
    dl_set $g:sy [dl_repeat 0.5 $n]
    dl_set $g:angle [dl_mult 2 $::pi [dl_urand $n]]
    dl_set $g:restitution [dl_repeat $pcatch::restitution $n]

    return $g
}

# Build world without catcher (catcher is added separately so we can move it)
proc pcatch_make_world {} {
    set planks [pcatch_create_plank_dg]
    set ball [pcatch_create_ball_dg]

    dg_append $planks $ball
    dg_delete $ball

    return $planks
}

# ============================================================
# PHYSICS SIMULATION (for trajectory recording)
# ============================================================

proc pcatch_build_sim_world { dg } {
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

    # Add a floor so the ball comes to rest even without a catcher
    box2d::createBox $world "floor" 0 0 [expr {$pcatch::catcher_y - 2.0}] 30 0.5 0

    return "$world $ball"
}

proc pcatch_test_simulation { world ball { simtime 6 } } {
    set step [expr {[screen_set FrameDuration]/1000.0}]
    box2d::setBodyType $world $ball 2

    set traj_t [list]
    set traj_x [list]
    set traj_y [list]

    for { set t 0 } { $t < $simtime } { set t [expr {$t+$step}] } {
        box2d::step $world $step
        lassign [box2d::getBodyInfo $world $ball] tx ty a
        lappend traj_t $t
        lappend traj_x $tx
        lappend traj_y $ty
    }

    return [list $traj_t $traj_x $traj_y]
}

# Find where the ball crosses catcher_y level
proc pcatch_find_landing { traj_t traj_x traj_y } {
    set catcher_y $pcatch::catcher_y
    set n [llength $traj_y]
    for { set i 0 } { $i < $n } { incr i } {
        if { [lindex $traj_y $i] < $catcher_y } {
            return [list [lindex $traj_t $i] [lindex $traj_x $i]]
        }
    }
    # Ball never reached catcher level
    return [list [lindex $traj_t end] [lindex $traj_x end]]
}

proc pcatch_generate_world {} {
    while { 1 } {
        box2d::destroy all
        set new_world [pcatch_make_world]
        lassign [pcatch_build_sim_world $new_world] world ball
        lassign [pcatch_test_simulation $world $ball] traj_t traj_x traj_y
        lassign [pcatch_find_landing $traj_t $traj_x $traj_y] land_time land_x

        # Accept if ball actually falls to catcher level and lands
        # within a reasonable x range
        if { $land_time < 5.5 && abs($land_x) < 10 } {
            set pcatch::traj_t $traj_t
            set pcatch::traj_x $traj_x
            set pcatch::traj_y $traj_y
            set pcatch::land_time $land_time
            set pcatch::land_x $land_x
            return $new_world
        } else {
            dg_delete $new_world
        }
    }
}

# ============================================================
# TRAJECTORY PLAYBACK
# ============================================================

proc pcatch_update_position { ball body start } {
    set now [expr {($::StimTime - $start) / 1000.0}]

    set n [llength $pcatch::traj_t]
    for { set i 0 } { $i < $n } { incr i } {
        if { [lindex $pcatch::traj_t $i] > $now } {
            set x [lindex $pcatch::traj_x $i]
            set y [lindex $pcatch::traj_y $i]
            Box2D_setTransform $pcatch::bworld [setObjProp $ball body] $x $y
            return
        }
    }

    # Past end of trajectory - hold final position
    set x [lindex $pcatch::traj_x end]
    set y [lindex $pcatch::traj_y end]
    Box2D_setTransform $pcatch::bworld [setObjProp $ball body] $x $y
}

# Check for ball-catcher contact events (runs as PostScript on bworld)
proc pcatch_check_contacts { w } {
    if { $pcatch::caught != -1 } { return }
    if { [Box2D_getContactBeginEventCount $w] > 0 } {
        set contacts [Box2D_getContactBeginEvents $w]
        foreach c $contacts {
            if { [lsearch $c catcher_b] >= 0 } {
                set pcatch::caught 1
                pcatch_show_feedback
                return
            }
        }
    }

    # Check if ball has passed below catcher without contact
    if { $pcatch::ball ne "" } {
        set body [setObjProp $pcatch::ball body]
        lassign [Box2D_getBodyInfo $pcatch::bworld $body] bx by ba
        if { $by < [expr {$pcatch::catcher_y - 3.0}] } {
            set pcatch::caught 0
            pcatch_show_feedback
        }
    }
}

proc pcatch_show_feedback {} {
    if { $pcatch::caught == 1 } {
        set color { 0.2 0.9 0.3 }
    } else {
        set color { 1.0 0.2 0.2 }
    }
    foreach obj $pcatch::catcher_parts {
        polycolor $obj {*}$color
    }
}

# ============================================================
# VISUAL HELPERS
# ============================================================

proc pcatch_make_rect {} {
    set s [polygon]
    return $s
}

proc pcatch_make_circle {} {
    set circ [polygon]
    polycirc $circ 1
    return $circ
}

proc pcatch_create_visual_box { bworld name type tx ty sx sy {angle 0} {color {1 1 1}} } {
    set body [Box2D_createBox $bworld $name $type $tx $ty $sx $sy $angle]

    set box [pcatch_make_rect]
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

proc pcatch_create_visual_circle { bworld name type tx ty radius {angle 0} {color {1 1 1}} } {
    set body [Box2D_createCircle $bworld $name $type $tx $ty $radius $angle]

    set circ [pcatch_make_circle]
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
# CATCHER VISUAL (built separately so we can reposition)
# ============================================================

proc pcatch_build_catcher { bworld tx } {
    set catcher_y $pcatch::catcher_y
    set w $pcatch::catcher_width
    set wall_h 2.0
    set wall_w 0.5
    set floor_h 0.5
    set y_floor [expr {$catcher_y - ($floor_h + $wall_h/2.0)}]
    set half_w [expr {$w / 2.0}]

    set color { 0.7 0.7 0.7 }

    set parts [list]

    # Floor
    set obj [pcatch_create_visual_box $bworld catcher_b 0 \
                 $tx $y_floor $w $floor_h 0 $color]
    glistAddObject $obj 0
    lappend parts $obj

    # Right wall
    set obj [pcatch_create_visual_box $bworld catcher_r 0 \
                 [expr {$tx+$half_w}] $catcher_y $wall_w $wall_h 0 $color]
    glistAddObject $obj 0
    lappend parts $obj

    # Left wall
    set obj [pcatch_create_visual_box $bworld catcher_l 0 \
                 [expr {$tx-$half_w}] $catcher_y $wall_w $wall_h 0 $color]
    glistAddObject $obj 0
    lappend parts $obj

    return $parts
}

# ============================================================
# SETUP
# ============================================================

proc pcatch_setup { nplanks } {
    set pcatch::nplanks $nplanks
    set pcatch::dropped 0
    set pcatch::caught -1

    # Clean up previous world dg
    if { $pcatch::world_dg ne "" && [dg_exists $pcatch::world_dg] } {
        dg_delete $pcatch::world_dg
    }

    # Generate world and record trajectory
    set dg [pcatch_generate_world]
    set pcatch::world_dg $dg

    # Build the visual scene
    resetObjList
    glistInit 1

    set bworld [Box2D]
    set pcatch::bworld $bworld
    glistAddObject $bworld 0

    # Add planks and ball from dg
    set n [dl_length $dg:name]
    for { set i 0 } { $i < $n } { incr i } {
        foreach v {name shape type tx ty sx sy angle restitution} {
            set $v [dl_get $dg:$v $i]
        }
        if { $shape == "Box" } {
            set obj [pcatch_create_visual_box $bworld $name $type \
                         $tx $ty $sx $sy $angle { 0.9 0.9 0.9 0.8 }]
        } elseif { $shape == "Circle" } {
            set obj [pcatch_create_visual_circle $bworld $name $type \
                         $tx $ty $sx $angle { 0 1 1 }]
        }
        Box2D_setRestitution $bworld [setObjProp $obj body] $restitution
        glistAddObject $obj 0

        if { $name == "ball" } {
            set pcatch::ball $obj
        }
    }

    # Add the catcher at current slider position
    set pcatch::catcher_parts [pcatch_build_catcher $bworld $pcatch::catcher_x]

    # Register contact detection
    addPostScript $bworld [list pcatch_check_contacts $bworld]

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1

    setBackground 128 128 128

    redraw
}

# ============================================================
# ACTIONS
# ============================================================

proc pcatch_trigger { action } {
    switch $action {
        drop {
            if { $pcatch::ball ne "" && !$pcatch::dropped } {
                set pcatch::dropped 1
                set body [setObjProp $pcatch::ball body]
                Box2D_setBodyType $pcatch::bworld $body 1 ;# kinematic
                addPreScript $pcatch::ball \
                    "pcatch_update_position $pcatch::ball $body $::StimTime"
            }
        }
        reset {
            pcatch_setup $pcatch::nplanks
        }
    }
    return
}

# ============================================================
# ADJUSTERS
# ============================================================

proc pcatch_set_catcher { catcher_x } {
    if { $pcatch::dropped } { return }

    set old_x $pcatch::catcher_x
    set pcatch::catcher_x $catcher_x
    set dx [expr {$catcher_x - $old_x}]

    # Move each catcher body and its visual
    foreach obj $pcatch::catcher_parts {
        set body [setObjProp $obj body]
        lassign [Box2D_getBodyInfo $pcatch::bworld $body] bx by ba
        set new_x [expr {$bx + $dx}]
        Box2D_setTransform $pcatch::bworld $body $new_x $by
        set m [dl_tcllist [mat4_createTranslationAngle $new_x $by 0]]
        setObjMatrix $obj {*}$m
    }
    redraw
    return
}

proc pcatch_get_catcher { {target {}} } {
    dict create catcher_x $pcatch::catcher_x
}

proc pcatch_set_physics { restitution } {
    set pcatch::restitution $restitution
    return
}

proc pcatch_get_physics { {target {}} } {
    dict create restitution $pcatch::restitution
}

proc pcatch_set_plank { plank_width } {
    set pcatch::plank_width $plank_width
    return
}

proc pcatch_get_plank { {target {}} } {
    dict create plank_width $pcatch::plank_width
}

# ============================================================
# KEYBOARD
# ============================================================

proc onDownArrow {} { pcatch_trigger drop }
proc onUpArrow {} { pcatch_trigger reset }

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup pcatch_setup {
    nplanks {int 2 15 1 5 "Number of Planks"}
} -adjusters {pcatch_actions pcatch_catcher pcatch_physics pcatch_plank} \
  -label "Planko Catch (Predict & Catch)"

workspace::adjuster pcatch_actions {
    drop  {action "Drop Ball (↓)"}
    reset {action "New Board (↑)"}
} -target {} -proc pcatch_trigger \
  -label "Actions"

workspace::adjuster pcatch_catcher {
    catcher_x {float -8.0 8.0 0.25 0.0 "Catcher Position"}
} -target {} -proc pcatch_set_catcher -getter pcatch_get_catcher \
  -label "Catcher (set before dropping)"

workspace::adjuster pcatch_physics {
    restitution {float 0.0 1.0 0.05 0.3 "Restitution (Bounciness)"}
} -target {} -proc pcatch_set_physics -getter pcatch_get_physics \
  -label "Physics (applied on reset)"

workspace::adjuster pcatch_plank {
    plank_width {float 1.0 6.0 0.5 3.0 "Plank Width"}
} -target {} -proc pcatch_set_plank -getter pcatch_get_plank \
  -label "Plank Geometry (applied on reset)"
