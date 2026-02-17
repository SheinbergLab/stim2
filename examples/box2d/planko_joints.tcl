# examples/box2d/planko_joints.tcl
# Planko Joints - Ball drop through rotating planks on revolute joints
# Demonstrates: Box2D revolute joints, dynamic planks
#
# Planks are attached to static pivot points via revolute joints,
# so they swing and rotate when the ball hits them.
#
# Controls:
#   Down Arrow - Drop the ball (set to dynamic)
#   Up Arrow   - Generate new board and reset

# ============================================================
# STATE
# ============================================================

namespace eval planko_j {
    variable bworld ""
    variable ball ""

    variable nplanks 10
    variable plank_width 2.5
    variable plank_height 0.5
    variable ball_radius 0.5
    variable restitution 0.2
    variable xrange 16
    variable yrange 12
    variable catcher_y -7.5
    variable ball_start_y 8.0

    variable spring_enabled 0
    variable spring_hertz 3.0
    variable spring_damping 0.5
}

# ============================================================
# VISUAL HELPERS (angles in degrees, matching bounce_stim.tcl)
# ============================================================

proc plankoj_make_rect {} {
    set s [polygon]
    return $s
}

proc plankoj_make_circle {} {
    set circ [polygon]
    polycirc $circ 1
    return $circ
}

# create a box2d body and visual box (angle is in degrees)
proc plankoj_create_box { bworld name type tx ty sx sy {angle 0} {color {1 1 1}} } {
    set radians [expr {$angle * ($::pi / 180.0)}]
    set body [Box2D_createBox $bworld $name $type $tx $ty $sx $sy $radians]

    set box [plankoj_make_rect]
    scaleObj $box [expr {1.0*$sx}] [expr {1.0*$sy}]
    translateObj $box $tx $ty
    rotateObj $box $angle 0 0 1
    polycolor $box {*}$color

    set m [dl_tcllist [mat4_createTranslationAngle $tx $ty $angle]]
    setObjMatrix $box {*}$m

    Box2D_linkObj $bworld $body $box
    setObjProp $box body $body
    setObjProp $box bworld $bworld

    return $box
}

# create a box2d body and visual circle (angle is in degrees)
proc plankoj_create_circle { bworld name type tx ty radius {angle 0} {color {1 1 1}} } {
    set radians [expr {$angle * ($::pi / 180.0)}]
    set body [Box2D_createCircle $bworld $name $type $tx $ty $radius $radians]

    set circ [plankoj_make_circle]
    scaleObj $circ [expr {2.0*$radius}] [expr {2.0*$radius}]
    translateObj $circ $tx $ty
    polycolor $circ {*}$color

    set m [dl_tcllist [mat4_createTranslationAngle $tx $ty $angle]]
    setObjMatrix $circ {*}$m

    Box2D_linkObj $bworld $body $circ
    setObjProp $circ body $body
    setObjProp $circ bworld $bworld

    return $circ
}

# create a catcher (3 boxes forming a U shape)
proc plankoj_create_catcher { bworld name tx } {
    set catcher_y $planko_j::catcher_y
    set y [expr {$catcher_y - (0.5 + 0.5/2)}]
    set color { 0.3 0.3 0.3 }

    set mg [metagroup]

    set b [plankoj_create_box $bworld ${name}_b 0 $tx $y 5 0.5 0 $color]
    metagroupAdd $mg $b

    set r [plankoj_create_box $bworld ${name}_r 0 [expr {$tx+2.5}] $catcher_y 0.5 2 0 $color]
    metagroupAdd $mg $r

    set l [plankoj_create_box $bworld ${name}_l 0 [expr {$tx-2.5}] $catcher_y 0.5 2 0 $color]
    metagroupAdd $mg $l

    return $mg
}

# ============================================================
# SETUP (single-loop, direct body handles for joints)
# ============================================================

proc plankoj_setup { nplanks } {
    set planko_j::nplanks $nplanks

    resetObjList
    glistInit 1

    set bworld [Box2D]
    set planko_j::bworld $bworld
    glistAddObject $bworld 0

    # Catchers
    set catcher1 [plankoj_create_catcher $bworld catchl -3]
    glistAddObject $catcher1 0
    set catcher2 [plankoj_create_catcher $bworld catchr 3]
    glistAddObject $catcher2 0

    # Generate planks with pivots and joints in one loop
    set xrange $planko_j::xrange
    set xrange_2 [expr {$xrange / 2}]
    set yrange $planko_j::yrange
    set yrange_2 [expr {$yrange / 2}]

    for { set i 0 } { $i < $nplanks } { incr i } {
        set tx [expr {rand() * $xrange - $xrange_2}]
        set ty [expr {rand() * $yrange - $yrange_2}]
        set angle [expr {rand() * 360}]

        # Pivot (static, tiny circle)
        set pivot [plankoj_create_circle $bworld pivot${i} 0 \
                       $tx $ty 0.1 0 { 0 0 0 }]
        glistAddObject $pivot 0

        # Plank (dynamic, rectangle)
        set plank [plankoj_create_box $bworld plank${i} 2 \
                       $tx $ty $planko_j::plank_width $planko_j::plank_height \
                       $angle { 0.9 0.9 0.9 0.8 }]
        glistAddObject $plank 0
        Box2D_setRestitution $bworld [setObjProp $plank body] $planko_j::restitution

        # Joint (using body handles directly, same as testplankojoint.tcl)
        set pivot_body [setObjProp $pivot body]
        set plank_body [setObjProp $plank body]
        set joint [Box2D_revoluteJointCreate $bworld $pivot_body $plank_body]

        # Apply spring if enabled
        if { $planko_j::spring_enabled } {
            Box2D_revoluteJointEnableSpring $bworld $joint 1
            Box2D_revoluteJointSetSpringHertz $bworld $joint $planko_j::spring_hertz
            Box2D_revoluteJointSetSpringDampingRatio $bworld $joint $planko_j::spring_damping
        }
    }

    # Ball (static until dropped)
    set ball [plankoj_create_circle $bworld ball 0 \
                  0 $planko_j::ball_start_y $planko_j::ball_radius 0 { 0 1 1 }]
    glistAddObject $ball 0
    set planko_j::ball $ball

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1

    setBackground 128 128 128

    redraw
}

# ============================================================
# ACTIONS
# ============================================================

proc plankoj_trigger { action } {
    switch $action {
        drop {
            if { $planko_j::ball ne "" } {
                set body [setObjProp $planko_j::ball body]
                Box2D_setBodyType $planko_j::bworld $body 2
            }
        }
        reset {
            plankoj_setup $planko_j::nplanks
        }
    }
    return
}

# ============================================================
# ADJUSTERS
# ============================================================

proc plankoj_set_physics { restitution } {
    set planko_j::restitution $restitution
    return
}

proc plankoj_get_physics { {target {}} } {
    dict create restitution $planko_j::restitution
}

proc plankoj_set_plank { plank_width plank_height } {
    set planko_j::plank_width $plank_width
    set planko_j::plank_height $plank_height
    return
}

proc plankoj_get_plank { {target {}} } {
    dict create \
        plank_width $planko_j::plank_width \
        plank_height $planko_j::plank_height
}

proc plankoj_set_spring { spring_enabled spring_hertz spring_damping } {
    set planko_j::spring_enabled [expr {int($spring_enabled)}]
    set planko_j::spring_hertz $spring_hertz
    set planko_j::spring_damping $spring_damping
    return
}

proc plankoj_get_spring { {target {}} } {
    dict create \
        spring_enabled $planko_j::spring_enabled \
        spring_hertz $planko_j::spring_hertz \
        spring_damping $planko_j::spring_damping
}

proc plankoj_set_ball { ball_radius } {
    set planko_j::ball_radius $ball_radius
    return
}

proc plankoj_get_ball { {target {}} } {
    dict create ball_radius $planko_j::ball_radius
}

# ============================================================
# KEYBOARD
# ============================================================

proc onDownArrow {} { plankoj_trigger drop }
proc onUpArrow {} { plankoj_trigger reset }

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup plankoj_setup {
    nplanks {int 3 30 1 10 "Number of Planks"}
} -adjusters {plankoj_actions plankoj_physics plankoj_plank plankoj_spring plankoj_ball} \
  -label "Planko (Revolute Joints)"

workspace::adjuster plankoj_actions {
    drop  {action "Drop Ball (↓)"}
    reset {action "New Board (↑)"}
} -target {} -proc plankoj_trigger \
  -label "Actions"

workspace::adjuster plankoj_physics {
    restitution {float 0.0 1.0 0.05 0.2 "Restitution"}
} -target {} -proc plankoj_set_physics -getter plankoj_get_physics \
  -label "Physics (applied on reset)"

workspace::adjuster plankoj_plank {
    plank_width  {float 1.0 5.0 0.25 2.5 "Plank Width"}
    plank_height {float 0.2 1.5 0.1 0.5 "Plank Height"}
} -target {} -proc plankoj_set_plank -getter plankoj_get_plank \
  -label "Plank Geometry (applied on reset)"

workspace::adjuster plankoj_spring {
    spring_enabled {bool 0 "Enable Spring"}
    spring_hertz   {float 0.5 10.0 0.5 3.0 "Spring Frequency" Hz}
    spring_damping {float 0.0 2.0 0.1 0.5 "Spring Damping"}
} -target {} -proc plankoj_set_spring -getter plankoj_get_spring \
  -label "Joint Springs (applied on reset)"

workspace::adjuster plankoj_ball {
    ball_radius {float 0.2 1.5 0.1 0.5 "Ball Radius"}
} -target {} -proc plankoj_set_ball -getter plankoj_get_ball \
  -label "Ball (applied on reset)"
