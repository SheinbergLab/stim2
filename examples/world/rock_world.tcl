# examples/world/rock_world.tcl
# Platformer character control with TMX tilemap
# Demonstrates: worldCreate, worldLoadTMX, sprite tilesets with
#   Aseprite animations, physics bodies, camera, keyboard control
#
# Loads a TMX tilemap with collision tiles and a PinkStar animated
# character sprite.  Arrow keys move/jump, number keys switch animations.
#
# Controls:
#   Left/Right Arrow - Move
#   Up Arrow / Space - Jump
#   Down Arrow       - Stop & Idle
#   R                - Reset to spawn
#   1-5              - Switch animation (Idle, Run, Jump, Fall, Attack)

# ============================================================
# STATE
# ============================================================

namespace eval rock_demo {
    variable w ""
    variable player ""
    variable move_speed 2.0
    variable jump_impulse 10.0
    variable gravity_y -20.0
    variable world_scale 20.0
    variable player_size 0.0
    variable current_anim "Idle"
    variable tileset_name "PinkStar"
}

# ============================================================
# SETUP
# ============================================================

proc rock_setup { {speed 2.0} {jump 10.0} {gravity -20.0} } {
    glistInit 1
    resetObjList

    set rock_demo::move_speed $speed
    set rock_demo::jump_impulse $jump
    set rock_demo::gravity_y $gravity
    set rock_demo::current_anim "Idle"

    set w [worldCreate]
    objName $w rock_world
    set rock_demo::w $w

    worldSetGravity $w 0 $gravity
    worldLoadTMX $w [assetFind "world/RockWorld.tmx"] -normalize 1 -scale $rock_demo::world_scale

    # Find spawn point
    set spawn_x 0.0
    set spawn_y 2.0
    set objects [worldGetObjects $w]
    foreach obj $objects {
        if {[dict get $obj type] eq "spawn"} {
            set spawn_x [dict get $obj x]
            set spawn_y [dict get $obj y]
            break
        }
    }

    # Player sprite from tileset
    set ps [expr {$rock_demo::world_scale / 24.0}]
    set rock_demo::player_size $ps

    set player [worldCreateSpriteFromTileset $w "player" $rock_demo::tileset_name \
                    $spawn_x $spawn_y $ps $ps "Idle"]
    set rock_demo::player $player

    worldSpriteAddBody $w $player \
        -type dynamic \
        -fixedRotation 1 \
        -damping 0.1 \
        -friction 0.5 \
        -density 2.0 \
        -corner_radius 0.05

    # Camera
    worldSetCameraMode $w locked
    worldSetCameraPos $w 0 0

    # Per-frame update
    addPreScript $w rock_onUpdate

    # Display
    glistAddObject $w 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# UPDATE / HELPERS
# ============================================================

proc rock_onUpdate {} {
    # Placeholder for auto-animation switching (fall detection, etc.)
}

proc rock_is_grounded {} {
    set w $rock_demo::w
    set player $rock_demo::player
    if {$w eq "" || $player eq ""} { return 0 }

    set info [worldGetSpriteInfo $w $player]
    set vy [dict get $info vy]
    if {$vy > 0.1} { return 0 }

    set px [dict get $info x]
    set py [dict get $info y]
    set check [expr {$rock_demo::world_scale * 0.5 / 16.0}]
    return [worldQueryPoint $w $px [expr {$py - $check}] -ignore $player]
}

proc rock_do_jump {} {
    set w $rock_demo::w
    set player $rock_demo::player
    if {$w eq "" || $player eq ""} return
    if {![rock_is_grounded]} return

    worldApplyImpulse $w $player 0 $rock_demo::jump_impulse
    worldSetSpriteAnimationByName $w $player $rock_demo::tileset_name "Jump"
    set rock_demo::current_anim "Jump"
}

proc rock_reset {} {
    set w $rock_demo::w
    set player $rock_demo::player
    if {$w eq ""} return

    set objects [worldGetObjects $w]
    foreach obj $objects {
        if {[dict get $obj type] eq "spawn"} {
            set x [dict get $obj x]
            set y [dict get $obj y]
            worldSetSpritePosition $w $player $x $y
            worldSetLinearVelocity $w $player 0 0
            worldSetSpriteAnimationByName $w $player $rock_demo::tileset_name "Idle"
            set rock_demo::current_anim "Idle"
            break
        }
    }
}

# ============================================================
# KEYBOARD
# ============================================================

proc onUpArrow {} { rock_do_jump }

proc onLeftArrow {} {
    set w $rock_demo::w
    set player $rock_demo::player
    if {$w eq "" || $player eq ""} return

    set info [worldGetSpriteInfo $w $player]
    worldSetLinearVelocity $w $player -$rock_demo::move_speed [dict get $info vy]
    worldSetSpriteAnimationByName $w $player $rock_demo::tileset_name "Run"
    set rock_demo::current_anim "Run"
}

proc onRightArrow {} {
    set w $rock_demo::w
    set player $rock_demo::player
    if {$w eq "" || $player eq ""} return

    set info [worldGetSpriteInfo $w $player]
    worldSetLinearVelocity $w $player $rock_demo::move_speed [dict get $info vy]
    worldSetSpriteAnimationByName $w $player $rock_demo::tileset_name "Run"
    set rock_demo::current_anim "Run"
}

proc onDownArrow {} {
    set w $rock_demo::w
    set player $rock_demo::player
    if {$w eq "" || $player eq ""} return

    set info [worldGetSpriteInfo $w $player]
    worldSetLinearVelocity $w $player 0 [dict get $info vy]
    worldSetSpriteAnimationByName $w $player $rock_demo::tileset_name "Idle"
    set rock_demo::current_anim "Idle"
}

proc onKeyPress {keycode} {
    set w $rock_demo::w
    set player $rock_demo::player

    # Space = jump
    if {$keycode == 32} { rock_do_jump; return }

    # R = reset
    if {$keycode == 82 || $keycode == 114} { rock_reset; return }

    if {$w eq "" || $player eq ""} return

    # Number keys for animation
    set ts $rock_demo::tileset_name
    switch $keycode {
        49 { set anim "Idle" }
        50 { set anim "Run" }
        51 { set anim "Jump" }
        52 { set anim "Fall" }
        53 { set anim "Attack" }
        default { return }
    }
    worldSetSpriteAnimationByName $w $player $ts $anim
    set rock_demo::current_anim $anim
}

# ============================================================
# ADJUSTERS
# ============================================================

proc rock_set_movement {target speed jump} {
    set rock_demo::move_speed $speed
    set rock_demo::jump_impulse $jump
    return
}

proc rock_get_movement {{target {}}} {
    dict create \
        speed $rock_demo::move_speed \
        jump $rock_demo::jump_impulse
}

proc rock_set_gravity {target gy} {
    set rock_demo::gravity_y $gy
    set w $rock_demo::w
    if {$w ne ""} {
        worldSetGravity $w 0 $gy
    }
    return
}

proc rock_get_gravity {{target {}}} {
    dict create gy $rock_demo::gravity_y
}

proc rock_set_anim {anim} {
    set w $rock_demo::w
    set player $rock_demo::player
    if {$w eq "" || $player eq ""} return
    worldSetSpriteAnimationByName $w $player $rock_demo::tileset_name $anim
    set rock_demo::current_anim $anim
    return
}

proc rock_get_anim {{target {}}} {
    dict create anim $rock_demo::current_anim
}

proc rock_trigger {action} {
    set w $rock_demo::w
    set player $rock_demo::player
    if {$w eq "" || $player eq ""} return

    switch $action {
        move_left  { onLeftArrow }
        move_right { onRightArrow }
        stop       { onDownArrow }
        jump       { rock_do_jump }
        reset      { rock_reset }
    }
    return
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup rock_setup {
    speed   {float 0.5 10.0 0.5 2.0 "Move Speed" deg/s}
    jump    {float 1.0 30.0 1.0 10.0 "Jump Impulse"}
    gravity {float -40.0 0.0 1.0 -20.0 "Gravity" m/s²}
} -adjusters {rock_anim rock_actions rock_movement rock_gravity} \
  -label "Rock World Platformer"

workspace::adjuster rock_anim {
    anim {choice {Idle Run Jump Fall Attack} Idle "Animation"}
} -target {} -proc rock_set_anim -getter rock_get_anim \
  -label "Animation"

workspace::adjuster rock_actions {
    move_left  {action "← Left"}
    move_right {action "Right →"}
    stop       {action "Stop (↓)"}
    jump       {action "Jump (↑/Space)"}
    reset      {action "Reset (R)"}
} -target {} -proc rock_trigger \
  -label "Actions"

workspace::adjuster rock_movement {
    speed {float 0.5 10.0 0.5 2.0 "Move Speed" deg/s}
    jump  {float 1.0 30.0 1.0 10.0 "Jump Impulse"}
} -target {} -proc rock_set_movement -getter rock_get_movement \
  -label "Movement"

workspace::adjuster rock_gravity {
    gy {float -40.0 0.0 1.0 -20.0 "Gravity Y" m/s²}
} -target rock_world -proc rock_set_gravity -getter rock_get_gravity \
  -label "Gravity"
