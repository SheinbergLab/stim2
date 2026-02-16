# examples/world/maze3d.tcl
# First-person 3D maze with collectible items
# Demonstrates: worldCreate, worldLoadTMX, worldMaze3D* commands,
#   Kenney sprite sheet items, pickup callbacks, 2D/3D view toggle
#
# Navigate a first-person 3D maze rendered from a TMX tilemap.
# Kenney sprite items are scattered throughout as collectibles.
#
# Controls:
#   Up/Down Arrows    - Move forward/back
#   Left/Right Arrows - Turn
#   W/A/S/D           - Move forward/back, strafe left/right
#   M                 - Toggle 2D map / 3D view
#   R                 - Reset to spawn
#   I                 - Show item info (console)
#   P                 - Print maze status (console)

package require yajltcl
package require spritesheet

# ============================================================
# STATE
# ============================================================

namespace eval maze_demo {
    variable w ""
    variable move_fwd 0
    variable move_str 0
    variable turn_yaw 0
    variable items_collected 0
    variable items_total 0
    variable kenney_frames 0
    variable num_items 15
    variable is_3d 1

    # Maze3D config defaults
    variable wall_height 1.0
    variable fov 70.0
    variable move_speed 3.0
    variable turn_speed 2.0
    variable fog_start 2.0
    variable fog_end 10.0
    variable ambient 0.25
}

# ============================================================
# SETUP
# ============================================================

proc maze_setup { {num_items 15} } {
    glistInit 1
    resetObjList

    set maze_demo::items_collected 0
    set maze_demo::items_total 0
    set maze_demo::num_items $num_items
    set maze_demo::is_3d 1

    # Create world
    set w [worldCreate]
    objName $w maze_world
    set maze_demo::w $w

    worldSetGravity $w 0 0
    worldSetAutoCenter $w 0

    # Load TMX
    worldLoadTMX $w [assetFind "world/maze_test.tmx"] \
        -pixels_per_meter 32 \
        -collision_layer "Walls"

    # Configure 3D maze
    worldMaze3DConfigure $w \
        -wall_height $maze_demo::wall_height \
        -eye_height  0.5 \
        -move_speed  $maze_demo::move_speed \
        -turn_speed  $maze_demo::turn_speed \
        -fov         $maze_demo::fov \
        -physics     1 \
        -cam_radius  0.15 \
        -cam_damping 10.0 \
        -fog_start   $maze_demo::fog_start \
        -fog_end     $maze_demo::fog_end \
        -fog_color  {0.02 0.02 0.05 1.0} \
        -ambient    $maze_demo::ambient \
        -draw_floor   1 \
        -draw_ceiling 1

    # Textures from tileset
    worldMaze3DConfigure $w \
        -wall_gid    1 \
        -floor_gid   2 \
        -ceiling_gid 3

    # Enable 3D and place at spawn
    worldMaze3DEnable $w 1
    worldMaze3DPlaceAt $w "spawn"

    # Load items
    maze_load_items

    # Per-frame update
    addPreScript $w maze_onUpdate

    # Display
    glistAddObject $w 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# ITEMS
# ============================================================

proc maze_load_items {} {
    set w $maze_demo::w

    # Load Kenney sprite sheet
    set sheet_json [spritesheet::process \
        [assetFind "world/spritesheet-tiles-default.xml"] \
        -epsilon 4.0 -min_area 20.0]
    set sheet_data [yajl::json2dict $sheet_json]
    worldAddSpriteSheet $w "Kenney" $sheet_data

    set meta [dict get $sheet_data _metadata]
    set maze_demo::kenney_frames [dict get $meta frame_count]

    # Pickup callback
    worldMaze3DItemCallback $w maze_onPickup

    # Find open cells
    set info [worldMaze3DInfo $w]
    set gw [dict get $info grid_w]
    set gh [dict get $info grid_h]
    set cell [dict get $info cell_size]
    set spawn_x [dict get $info cam_x]
    set spawn_z [dict get $info cam_z]

    set open_cells [list]
    for {set gz 0} {$gz < $gh} {incr gz} {
        for {set gx 0} {$gx < $gw} {incr gx} {
            if {[worldMaze3DQueryCell $w $gx $gz] == 0} {
                set mx [expr {($gx + 0.5) * $cell}]
                set mz [expr {($gz + 0.5) * $cell}]
                set dx [expr {$mx - $spawn_x}]
                set dz [expr {$mz - $spawn_z}]
                if {($dx*$dx + $dz*$dz) > 4.0} {
                    lappend open_cells [list $mx $mz]
                }
            }
        }
    }

    # Scatter items
    set n [expr {min($maze_demo::num_items, [llength $open_cells])}]
    set used [list]

    for {set i 0} {$i < $n} {incr i} {
        while 1 {
            set idx [expr {int(rand() * [llength $open_cells])}]
            if {$idx ni $used} break
        }
        lappend used $idx

        set coords [lindex $open_cells $idx]
        set mx [lindex $coords 0]
        set mz [lindex $coords 1]
        set frame [expr {int(rand() * $maze_demo::kenney_frames)}]
        set bob_amp [expr {0.03 + rand() * 0.04}]
        set bob_spd [expr {0.8 + rand() * 0.6}]

        worldMaze3DItemAdd $w "Kenney" $mx $mz \
            -name "item_$i" \
            -frame $frame \
            -size 0.4 \
            -radius 0.6 \
            -height 0.3 \
            -bob_amplitude $bob_amp \
            -bob_speed $bob_spd
    }

    set maze_demo::items_total $n
}

proc maze_onPickup {item_id name} {
    incr maze_demo::items_collected
    puts "Picked up $name! ($maze_demo::items_collected / $maze_demo::items_total)"
    if {$maze_demo::items_collected >= $maze_demo::items_total} {
        puts "*** All items collected! ***"
    }
}

# ============================================================
# UPDATE
# ============================================================

proc maze_onUpdate {} {
    set w $maze_demo::w
    if {$w eq ""} return

    if {$maze_demo::turn_yaw != 0} {
        worldMaze3DRotate $w $maze_demo::turn_yaw
    }
    if {$maze_demo::move_fwd != 0 || $maze_demo::move_str != 0} {
        worldMaze3DMove $w $maze_demo::move_fwd $maze_demo::move_str
    }
}

# ============================================================
# KEYBOARD
# ============================================================

proc onLeftArrow {}  { set maze_demo::turn_yaw  1.0 }
proc onRightArrow {} { set maze_demo::turn_yaw -1.0 }
proc onUpArrow {}    { set maze_demo::move_fwd  1.0 }
proc onDownArrow {}  { set maze_demo::move_fwd -1.0 }

proc onLeftArrowRelease {}  { set maze_demo::turn_yaw 0 }
proc onRightArrowRelease {} { set maze_demo::turn_yaw 0 }
proc onUpArrowRelease {}    { set maze_demo::move_fwd 0 }
proc onDownArrowRelease {}  { set maze_demo::move_fwd 0 }

proc onKeyPress {keycode} {
    set w $maze_demo::w

    switch $keycode {
        119 - 87  { set maze_demo::move_fwd  1.0 }
        115 - 83  { set maze_demo::move_fwd -1.0 }
        97  - 65  { set maze_demo::move_str -1.0 }
        100 - 68  { set maze_demo::move_str  1.0 }
        109 - 77  { maze_toggle_view }
        114 - 82  { worldMaze3DPlaceAt $w "spawn"; puts "Reset to spawn" }
        105 - 73  { maze_show_items }
        112 - 80  { maze_status }
    }
}

proc onKeyRelease {keycode} {
    switch $keycode {
        119 - 87  { set maze_demo::move_fwd 0 }
        115 - 83  { set maze_demo::move_fwd 0 }
        97  - 65  { set maze_demo::move_str 0 }
        100 - 68  { set maze_demo::move_str 0 }
    }
}

# ============================================================
# TOGGLE / STATUS
# ============================================================

# Discrete turn for action buttons (bypasses turn_speed*dt scaling)
proc maze3d_step_turn {delta_yaw} {
    set w $maze_demo::w
    if {$w eq ""} return
    set info [worldMaze3DInfo $w]
    set x [dict get $info cam_x]
    set z [dict get $info cam_z]
    set yaw [expr {[dict get $info cam_yaw] + $delta_yaw}]
    worldMaze3DCamera $w $x $z $yaw
}

proc maze_toggle_view {} {
    set w $maze_demo::w
    set info [worldMaze3DInfo $w]
    if {[dict get $info enabled]} {
        worldMaze3DEnable $w 0
        set maze_demo::is_3d 0
        puts "Switched to 2D map view"
    } else {
        worldMaze3DEnable $w 1
        set maze_demo::is_3d 1
        puts "Switched to 3D maze view"
    }
}

proc maze_status {} {
    set w $maze_demo::w
    set info [worldMaze3DInfo $w]
    puts "=== Maze3D Status ==="
    puts [format "  Position: %.2f, %.2f  Yaw: %.2f" \
        [dict get $info cam_x] [dict get $info cam_z] [dict get $info cam_yaw]]
    puts [format "  Grid: %dx%d  Cell: %.2f  Faces: %d" \
        [dict get $info grid_w] [dict get $info grid_h] \
        [dict get $info cell_size] [dict get $info face_count]]
    puts [format "  Items: %d/%d collected" \
        $maze_demo::items_collected $maze_demo::items_total]
    puts [format "  View: %s" \
        [expr {[dict get $info enabled] ? "3D" : "2D map"}]]
}

proc maze_show_items {} {
    set w $maze_demo::w
    set items [worldMaze3DItemList $w]
    puts "=== Items ==="
    foreach item $items {
        set id [dict get $item id]
        set d [worldMaze3DItemInfo $w $id]
        puts [format "  #%02d %-10s at (%5.1f,%5.1f) %s" \
            $id [dict get $d name] \
            [dict get $d x] [dict get $d z] \
            [expr {[dict get $d visible] ? "vis" : "HID"}]]
    }
}

proc maze_reset_items {} {
    set w $maze_demo::w
    set items [worldMaze3DItemList $w]
    foreach item $items {
        worldMaze3DItemShow $w [dict get $item id] 1
    }
    set maze_demo::items_collected 0
    puts "All items reset"
}

# ============================================================
# ADJUSTERS
# ============================================================

proc maze_set_rendering {target wall_height fov ambient} {
    set w $maze_demo::w
    if {$w eq ""} return
    set maze_demo::wall_height $wall_height
    set maze_demo::fov $fov
    set maze_demo::ambient $ambient
    worldMaze3DConfigure $w \
        -wall_height $wall_height \
        -fov $fov \
        -ambient $ambient
    return
}

proc maze_get_rendering {{target {}}} {
    dict create \
        wall_height $maze_demo::wall_height \
        fov $maze_demo::fov \
        ambient $maze_demo::ambient
}

proc maze_set_fog {target fog_start fog_end} {
    set w $maze_demo::w
    if {$w eq ""} return
    set maze_demo::fog_start $fog_start
    set maze_demo::fog_end $fog_end
    worldMaze3DConfigure $w \
        -fog_start $fog_start \
        -fog_end $fog_end
    return
}

proc maze_get_fog {{target {}}} {
    dict create \
        fog_start $maze_demo::fog_start \
        fog_end $maze_demo::fog_end
}

proc maze_set_speed {target move_speed turn_speed} {
    set w $maze_demo::w
    if {$w eq ""} return
    set maze_demo::move_speed $move_speed
    set maze_demo::turn_speed $turn_speed
    worldMaze3DConfigure $w \
        -move_speed $move_speed \
        -turn_speed $turn_speed
    return
}

proc maze_get_speed {{target {}}} {
    dict create \
        move_speed $maze_demo::move_speed \
        turn_speed $maze_demo::turn_speed
}

proc maze_trigger {action} {
    set w $maze_demo::w
    if {$w eq ""} return

    switch $action {
        move_fwd    { worldMaze3DMove $w  1.0 0.0 }
        move_back   { worldMaze3DMove $w -1.0 0.0 }
        strafe_left { worldMaze3DMove $w  0.0 -1.0 }
        strafe_right { worldMaze3DMove $w 0.0  1.0 }
        turn_left   { maze3d_step_turn  0.3 }
        turn_right  { maze3d_step_turn -0.3 }
        toggle_view { maze_toggle_view }
        reset_pos   { worldMaze3DPlaceAt $w "spawn"; puts "Reset to spawn" }
        reset_items { maze_reset_items }
        status      { maze_status }
    }
    return
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup maze_setup {
    num_items {int 1 30 1 15 "Number of Items"}
} -adjusters {maze_move maze_nav maze_actions maze_rendering maze_fog maze_speed} \
  -label "3D Maze Explorer"

workspace::adjuster maze_move {
    move_fwd     {action "Forward (↑/W)"}
    move_back    {action "Back (↓/S)"}
    strafe_left  {action "Strafe Left (A)"}
    strafe_right {action "Strafe Right (D)"}
} -target {} -proc maze_trigger \
  -label "Move"

workspace::adjuster maze_nav {
    turn_left   {action "Turn Left (←)"}
    turn_right  {action "Turn Right (→)"}
} -target {} -proc maze_trigger \
  -label "Turn"

workspace::adjuster maze_actions {
    toggle_view {action "Toggle 2D/3D (M)"}
    reset_pos   {action "Reset Position (R)"}
    reset_items {action "Reset Items"}
    status      {action "Print Status (P)"}
} -target {} -proc maze_trigger \
  -label "Actions"

workspace::adjuster maze_rendering {
    wall_height {float 0.5 3.0 0.1 1.0 "Wall Height"}
    fov         {float 40.0 120.0 5.0 70.0 "Field of View" deg}
    ambient     {float 0.0 1.0 0.05 0.25 "Ambient Light"}
} -target maze_world -proc maze_set_rendering -getter maze_get_rendering \
  -label "Rendering"

workspace::adjuster maze_fog {
    fog_start {float 0.5 10.0 0.5 2.0 "Fog Start"}
    fog_end   {float 2.0 20.0 0.5 10.0 "Fog End"}
} -target maze_world -proc maze_set_fog -getter maze_get_fog \
  -label "Fog"

workspace::adjuster maze_speed {
    move_speed {float 1.0 10.0 0.5 3.0 "Move Speed"}
    turn_speed {float 0.5 10.0 0.5 6.0 "Turn Speed"}
} -target maze_world -proc maze_set_speed -getter maze_get_speed \
  -label "Movement"
