# examples/box2d/physics_world.tcl
# Physics World - Ball drop through static planks
#
# A ball drops through randomly placed static planks onto a floor.
# The ball trajectory is pre-simulated and stored, then played back
# kinematically on "drop" - matching the planko approach.
#
# Controls:
#   Down Arrow  - Drop the ball
#   Up Arrow    - Reset (new board)

# ============================================================
# STATE
# ============================================================

namespace eval pworld {
    package require box2d

    variable bworld      ""
    variable ball        ""
    variable world_dg    ""
    variable world_group ""

    variable tx        0.0
    variable ty        0.0
    variable angle_deg 0.0

    variable traj_t {}
    variable traj_x {}
    variable traj_y {}

    variable nplanks    5
    variable plank_width 3.5
    variable ball_radius 0.4
    variable restitution 0.3
    variable xrange     12
    variable yrange     10
    variable ball_start_y 7.0
    variable floor_y   -6.5
}

# ============================================================
# DG WORLD CONSTRUCTION
# ============================================================

proc pworld_make_dg {} {
    set n    $pworld::nplanks
    set xr   $pworld::xrange
    set yr   $pworld::yrange
    set xr_2 [expr {$xr / 2}]
    set yr_2 [expr {$yr / 2}]

    set g [dg_create]

    dl_set $g:name        [dl_paste [dl_repeat [dl_slist plank] $n] [dl_fromto 0 $n]]
    dl_set $g:shape       [dl_repeat [dl_slist Box] $n]
    dl_set $g:type        [dl_repeat 0 $n]
    dl_set $g:tx          [dl_sub [dl_mult $xr [dl_urand $n]] $xr_2]
    dl_set $g:ty          [dl_sub [dl_mult $yr [dl_urand $n]] $yr_2]
    dl_set $g:sx          [dl_repeat $pworld::plank_width $n]
    dl_set $g:sy          [dl_repeat 0.4 $n]
    dl_set $g:angle       [dl_mult 2 $::pi [dl_urand $n]]
    dl_set $g:restitution [dl_repeat $pworld::restitution $n]

    # floor
    set floor [dg_create]
    dl_set $floor:name        [dl_slist floor]
    dl_set $floor:shape       [dl_slist Box]
    dl_set $floor:type        [dl_ilist 0]
    dl_set $floor:tx          [dl_flist 0]
    dl_set $floor:ty          [dl_flist $pworld::floor_y]
    dl_set $floor:sx          [dl_flist [expr {$xr + 2.0}]]
    dl_set $floor:sy          [dl_flist 0.5]
    dl_set $floor:angle       [dl_flist 0.0]
    dl_set $floor:restitution [dl_flist 0.0]
    dg_append $g $floor
    dg_delete $floor

    # ball
    set ball [dg_create]
    dl_set $ball:name        [dl_slist ball]
    dl_set $ball:shape       [dl_slist Circle]
    dl_set $ball:type        [dl_ilist 0]
    dl_set $ball:tx          [dl_flist 0]
    dl_set $ball:ty          [dl_flist $pworld::ball_start_y]
    dl_set $ball:sx          [dl_flist $pworld::ball_radius]
    dl_set $ball:sy          [dl_flist $pworld::ball_radius]
    dl_set $ball:angle       [dl_flist 0.0]
    dl_set $ball:restitution [dl_flist $pworld::restitution]
    dg_append $g $ball
    dg_delete $ball

    return $g
}

# ============================================================
# SIMULATION
# ============================================================

proc pworld_simulate { dg } {
    set world [box2d::createWorld]
    set n [dl_length $dg:name]
    set ball_body ""

    for { set i 0 } { $i < $n } { incr i } {
        foreach v {name shape type tx ty sx sy angle restitution} {
            set $v [dl_get $dg:$v $i]
        }
        if { $shape eq "Box" } {
            set body [box2d::createBox $world $name $type $tx $ty $sx $sy $angle]
        } elseif { $shape eq "Circle" } {
            set body [box2d::createCircle $world $name $type $tx $ty $sx]
        }
        box2d::setRestitution $world $body $restitution
        if { $name eq "ball" } { set ball_body $body }
    }

    box2d::setBodyType $world $ball_body 2
    set step [expr { [screen_set FrameDuration] / 1000.0 }]
    set simtime 6.0
    set traj_t {}
    set traj_x {}
    set traj_y {}

    for { set t 0.0 } { $t < $simtime } { set t [expr {$t + $step}] } {
        box2d::step $world $step
        lassign [box2d::getBodyInfo $world $ball_body] bx by _
        lappend traj_t $t
        lappend traj_x $bx
        lappend traj_y $by
    }

    box2d::destroy $world
    return [list $traj_t $traj_x $traj_y]
}

# ============================================================
# TRAJECTORY PLAYBACK
# ============================================================

proc pworld_update_ball { ball body start } {
    set now [expr { ($::StimTime - $start) / 1000.0 }]
    set n [llength $pworld::traj_t]
    for { set i 0 } { $i < $n } { incr i } {
        if { [lindex $pworld::traj_t $i] > $now } {
            Box2D_setTransform $pworld::bworld $body \
                [lindex $pworld::traj_x $i] \
                [lindex $pworld::traj_y $i]
            return
        }
    }
    Box2D_setTransform $pworld::bworld $body \
        [lindex $pworld::traj_x end] \
        [lindex $pworld::traj_y end]
}

# ============================================================
# VISUAL HELPERS (matching planko pattern exactly)
# ============================================================

proc pworld_make_rect {} {
    return [polygon]
}

proc pworld_make_circle {} {
    set c [polygon]
    polycirc $c 1
    return $c
}

proc pworld_create_visual_box { bworld name type tx ty sx sy {angle 0} {color {1 1 1}} } {
    set body [Box2D_createBox $bworld $name $type $tx $ty $sx $sy $angle]
    set box  [pworld_make_rect]
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

proc pworld_create_visual_circle { bworld name type tx ty radius {angle 0} {color {1 1 1}} } {
    set body [Box2D_createCircle $bworld $name $type $tx $ty $radius $angle]
    set circ [pworld_make_circle]
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

proc pworld_setup {} {
    if { $pworld::world_dg ne "" && [dg_exists $pworld::world_dg] } {
        dg_delete $pworld::world_dg
    }

    set dg [pworld_make_dg]
    set pworld::world_dg $dg

    lassign [pworld_simulate $dg] t x y
    set pworld::traj_t $t
    set pworld::traj_x $x
    set pworld::traj_y $y

    resetObjList
    glistInit 1

    set bworld [Box2D]
    set pworld::bworld $bworld
    glistAddObject $bworld 0

    set grp [metagroup]
    objName $grp pworld_group
    set pworld::world_group $grp

    set n [dl_length $dg:name]
    for { set i 0 } { $i < $n } { incr i } {
        foreach v {name shape type tx ty sx sy angle restitution} {
            set $v [dl_get $dg:$v $i]
        }
        if { $shape eq "Box" } {
            if { $name eq "floor" } {
                set color { 0.4 0.4 0.4 1.0 }
            } else {
                set color { 0.85 0.85 0.85 0.9 }
            }
            set obj [pworld_create_visual_box $bworld $name $type \
                         $tx $ty $sx $sy $angle $color]
        } elseif { $shape eq "Circle" } {
            set obj [pworld_create_visual_circle $bworld $name $type \
                         $tx $ty $sx $angle { 0.2 0.9 1.0 1.0 }]
            set pworld::ball $obj
        }
        Box2D_setRestitution $bworld [setObjProp $obj body] $restitution
        metagroupAdd $grp $obj
    }

    glistAddObject $grp 0

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    setBackground 40 40 40

    # Re-apply current world transform to the new metagroup
    pworld_set_transform $pworld::tx $pworld::ty $pworld::angle_deg
}

# ============================================================
# ACTIONS
# ============================================================

proc pworld_trigger { action } {
    switch $action {
        drop {
            if { $pworld::ball ne "" } {
                set body [setObjProp $pworld::ball body]
                Box2D_setBodyType $pworld::bworld $body 1
                addPreScript $pworld::ball \
                    "pworld_update_ball $pworld::ball $body $::StimTime"
            }
        }
        reset {
            pworld_setup
        }
    }
}

proc onDownArrow {} { pworld_trigger drop }
proc onUpArrow   {} { pworld_trigger reset }

# ============================================================
# WORLD TRANSFORM
# ============================================================

proc pworld_set_transform { tx ty angle_deg } {
    set pworld::tx        $tx
    set pworld::ty        $ty
    set pworld::angle_deg $angle_deg
    set m [dl_tcllist [mat4_createTranslationAngle $tx $ty $angle_deg]]
    setObjMatrix pworld_group {*}$m
    redraw
}

proc pworld_get_transform { {target {}} } {
    dict create \
        tx        $pworld::tx \
        ty        $pworld::ty \
        angle_deg $pworld::angle_deg
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup pworld_setup {} \
    -adjusters { pworld_actions pworld_transform } \
    -label "Physics World"

workspace::adjuster pworld_actions {
    drop  { action "Drop Ball" }
    reset { action "New Board" }
} -target {} -proc pworld_trigger \
  -label "Actions"

workspace::adjuster pworld_transform {
    tx        { float -8.0  8.0   0.25 0.0 "X Translate" }
    ty        { float -6.0  6.0   0.25 0.0 "Y Translate" }
    angle_deg { float -180.0 180.0 5.0 0.0 "Rotation"    }
} -target {} -proc pworld_set_transform -getter pworld_get_transform \
  -label "World Transform"
