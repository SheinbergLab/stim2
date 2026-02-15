# examples/world/kenney_tiles.tcl
# Kenney sprite physics sandbox
# Demonstrates: worldCreate, sprite sheets, auto-collision physics
#
# Drop randomly-shaped Kenney sprites into a walled arena and watch
# circles roll, boxes tumble, and polygons collide using Box2D physics
# with automatically detected collision shapes.
#
# Controls:
#   Down Arrow - Drop one random sprite
#   Space      - Drop a batch of sprites
#   X          - Clear all sprites

package require yajltcl
package require spritesheet

# ============================================================
# STATE
# ============================================================

namespace eval kenney_demo {
    variable w ""
    variable objects [list]
    variable frame_count 0
    variable batch_size 5
    variable max_objects 100
    variable gravity_y -9.8
    variable restitution 0.3
    variable friction 0.5
    variable density 1.0
}

# ============================================================
# SETUP
# ============================================================

proc kenney_setup { {gravity -9.8} {bounciness 0.3} } {
    glistInit 1
    resetObjList

    set kenney_demo::objects [list]
    set kenney_demo::gravity_y $gravity
    set kenney_demo::restitution $bounciness

    # Create world
    set w [worldCreate]
    objName $w kenney_world
    set kenney_demo::w $w

    worldSetGravity $w 0 $gravity

    # Load Kenney sprite sheet
    set sheet_json [spritesheet::process \
        [assetFind "world/spritesheet-tiles-default.xml"] \
        -epsilon 4.0 -min_area 20.0]
    set sheet_data [yajl::json2dict $sheet_json]
    worldAddSpriteSheet $w "Kenney" $sheet_data

    set meta [dict get $sheet_data _metadata]
    set kenney_demo::frame_count [dict get $meta frame_count]

    # Ground
    set ground [worldCreateSprite $w "ground" 0 0 -3 15.0 0.5 0]
    worldSpriteAddBody $w $ground -type static -friction 0.8

    # Walls
    set left [worldCreateSprite $w "left" 0 -7.5 0 0.5 10.0 0]
    worldSpriteAddBody $w $left -type static -friction 0.5

    set right [worldCreateSprite $w "right" 0 7.5 0 0.5 10.0 0]
    worldSpriteAddBody $w $right -type static -friction 0.5

    # Camera
    worldSetCameraMode $w locked
    worldSetCameraPos $w 0 0

    # Display
    glistAddObject $w 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# DROP / CLEAR PROCS
# ============================================================

proc kenney_drop_one {} {
    set w $kenney_demo::w
    if {$w eq ""} return

    set x [expr {rand() * 14.0 - 7.0}]
    set y 5.0
    set angle [expr {rand() * 360.0}]
    set frame [expr {int(rand() * $kenney_demo::frame_count)}]

    set obj [worldCreateSpriteFromSheet $w "Kenney" $x $y $frame]
    worldSetSpriteRotation $w $obj $angle

    worldSpriteAddBody $w $obj \
        -type dynamic \
        -density $kenney_demo::density \
        -friction $kenney_demo::friction \
        -restitution $kenney_demo::restitution

    lappend kenney_demo::objects $obj

    # Trim oldest if over limit
    if {[llength $kenney_demo::objects] > $kenney_demo::max_objects} {
        set old [lindex $kenney_demo::objects 0]
        catch {worldRemoveSprite $w $old}
        set kenney_demo::objects [lrange $kenney_demo::objects 1 end]
    }
}

proc kenney_drop_batch {} {
    for {set i 0} {$i < $kenney_demo::batch_size} {incr i} {
        kenney_drop_one
    }
}

proc kenney_clear {} {
    set w $kenney_demo::w
    foreach obj $kenney_demo::objects {
        catch {worldRemoveSprite $w $obj}
    }
    set kenney_demo::objects [list]
}

# ============================================================
# ADJUSTERS
# ============================================================

proc kenney_set_gravity {target gy} {
    set w $kenney_demo::w
    if {$w eq ""} return
    set kenney_demo::gravity_y $gy
    worldSetGravity $w 0 $gy
    return
}

proc kenney_get_gravity {{target {}}} {
    dict create gy $kenney_demo::gravity_y
}

proc kenney_set_physics {target density friction restitution} {
    set kenney_demo::density $density
    set kenney_demo::friction $friction
    set kenney_demo::restitution $restitution
    return
}

proc kenney_get_physics {{target {}}} {
    dict create \
        density $kenney_demo::density \
        friction $kenney_demo::friction \
        restitution $kenney_demo::restitution
}

proc kenney_set_limits {target batch_size max_objects} {
    set kenney_demo::batch_size [expr {int($batch_size)}]
    set kenney_demo::max_objects [expr {int($max_objects)}]
    return
}

proc kenney_get_limits {{target {}}} {
    dict create \
        batch_size $kenney_demo::batch_size \
        max_objects $kenney_demo::max_objects
}

proc kenney_trigger {action} {
    switch $action {
        drop_one   { kenney_drop_one }
        drop_batch { kenney_drop_batch }
        clear      { kenney_clear }
    }
    return
}

# ============================================================
# KEYBOARD
# ============================================================

proc onDownArrow {} { kenney_drop_one }

proc onKeyPress {keycode} {
    switch $keycode {
        32       { kenney_drop_batch }
        88 - 120 { kenney_clear }
    }
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup kenney_setup {
    gravity    {float -20.0 0.0 0.5 -9.8 "Gravity" m/s²}
    bounciness {float 0.0 1.0 0.05 0.3 "Bounciness"}
} -adjusters {kenney_actions kenney_gravity kenney_physics kenney_limits} \
  -label "Kenney Sprite Physics"

workspace::adjuster kenney_actions {
    drop_one   {action "Drop One (↓)"}
    drop_batch {action "Drop Batch (Space)"}
    clear      {action "Clear All (X)"}
} -target {} -proc kenney_trigger \
  -label "Actions"

workspace::adjuster kenney_gravity {
    gy {float -30.0 0.0 0.5 -9.8 "Gravity Y" m/s²}
} -target kenney_world -proc kenney_set_gravity -getter kenney_get_gravity \
  -label "Gravity"

workspace::adjuster kenney_physics {
    density     {float 0.1 10.0 0.1 1.0 "Density"}
    friction    {float 0.0 1.0 0.05 0.5 "Friction"}
    restitution {float 0.0 1.0 0.05 0.3 "Restitution"}
} -target {} -proc kenney_set_physics -getter kenney_get_physics \
  -label "New Sprite Physics"

workspace::adjuster kenney_limits {
    batch_size  {int 1 20 1 5 "Batch Size"}
    max_objects {int 10 200 10 100 "Max Objects"}
} -target {} -proc kenney_set_limits -getter kenney_get_limits \
  -label "Limits"
