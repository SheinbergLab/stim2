# two_rooms_test.tcl
#
# Two-room puzzle: push a block to open the path to the goal
#
# Controls (via stim2 key callbacks):
#   Arrow keys for movement
#   Define onKeyPress for other keys if needed
#
# Usage:
#   source two_rooms_test.tcl
#   start_puzzle

# Global state
set tm ""
set player ""
set pushable ""
set goal_reached 0

# Movement parameters
set move_speed 4
set jump_impulse 6

proc load_puzzle {} {
    global tm player pushable
    
    # Create tilemap (auto-centers by default)
    set tm [tilemapCreate]
    
    # Set gravity - platformer style
#    tilemapSetGravity $tm 0 -15

	# No gravity for top down view
    tilemapSetGravity $tm 0 0
    
    # Load the TMX
    set result [tilemapLoadTMX $tm "two_rooms.tmx" -pixels_per_meter 32]
    puts "Loaded: $result"
    
    # Find spawn point and goal from objects
    set objects [tilemapGetObjects $tm]
    puts "Objects: $objects"
    
    set spawn_x 0
    set spawn_y 0
    set pushable_x 0
    set pushable_y 0
    
    foreach obj $objects {
        set name [dict get $obj name]
        set type [dict get $obj type]
        set x [dict get $obj x]
        set y [dict get $obj y]
        
        if {$type eq "spawn"} {
            set spawn_x $x
            set spawn_y $y
            puts "Spawn at: $spawn_x, $spawn_y"
        } elseif {$type eq "pushable"} {
            set pushable_x $x
            set pushable_y $y
            puts "Pushable block at: $pushable_x, $pushable_y"
        } elseif {$type eq "goal"} {
            puts "Goal at: $x, $y"
        }
    }
    
    # Create player sprite at spawn point
    # tile_id 4 = player tile (green)
    set player [tilemapCreateSprite $tm "player" 4 $spawn_x $spawn_y 1.0 1.0]
    tilemapSpriteAddBody $tm $player dynamic
    puts "Created player sprite: $player"
    
    # Create pushable block sprite
    # tile_id 3 = pushable tile (orange/yellow)
    set pushable [tilemapCreateSprite $tm "pushable" 3 $pushable_x $pushable_y 1.0 1.0]
    tilemapSpriteAddBody $tm $pushable dynamic
    puts "Created pushable sprite: $pushable"
    
    # Set up collision callback
    tilemapSetCollisionCallback $tm on_collision
    
    # Add to display
    glistAddObject $tm 0
    glistSetDynamic 0 1
    
    puts "\nPuzzle loaded!"
    puts "Arrow keys to move, Up to jump"
    puts "Push the orange block to reach the goal in the right room"
}

proc on_collision {bodyA bodyB} {
    global goal_reached
    
    # Check if player reached goal
    if {($bodyA eq "player" && $bodyB eq "goal") ||
        ($bodyA eq "goal" && $bodyB eq "player")} {
        if {!$goal_reached} {
            set goal_reached 1
            puts "*** GOAL REACHED! ***"
            # Could trigger reward, sound, visual feedback here
        }
    }
}

#============================================================
# Stim2 key callbacks
#============================================================

proc onLeftArrow {} {
    global tm player move_speed
    if {$tm eq "" || $player eq ""} return
    
    set info [tilemapGetSpriteInfo $tm $player]
    set vel_y [dict get $info vel_y]
    tilemapSetLinearVelocity $tm $player [expr {-$move_speed}] $vel_y
}

proc onRightArrow {} {
    global tm player move_speed
    if {$tm eq "" || $player eq ""} return
    
    set info [tilemapGetSpriteInfo $tm $player]
    set vel_y [dict get $info vel_y]
    tilemapSetLinearVelocity $tm $player $move_speed $vel_y
}

proc onUpArrow {} {
    global tm player move_speed
    if {$tm eq "" || $player eq ""} return
    set info [tilemapGetSpriteInfo $tm $player]
    set vel_x [dict get $info vel_x]
    tilemapSetLinearVelocity $tm $player $vel_x $move_speed
}

proc onDownArrow {} {
    global tm player move_speed
    if {$tm eq "" || $player eq ""} return
    set info [tilemapGetSpriteInfo $tm $player]
    set vel_x [dict get $info vel_x]
    tilemapSetLinearVelocity $tm $player $vel_x [expr {-$move_speed}]
}

# proc onUpArrow {} {
#     global tm player jump_impulse
#     if {$tm eq "" || $player eq ""} return
#     
#     # Only jump if not already moving up fast (crude ground check)
#     set info [tilemapGetSpriteInfo $tm $player]
#     set vel_y [dict get $info vel_y]
#     if {$vel_y < 1} {
#         tilemapApplyImpulse $tm $player 0 $jump_impulse
#     }
# }
# 
# proc onDownArrow {} {
#     # Could be used for crouch or fast-fall
#     global tm player
#     if {$tm eq "" || $player eq ""} return
#     
#     # For now, just stop horizontal movement
#     set info [tilemapGetSpriteInfo $tm $player]
#     set vel_y [dict get $info vel_y]
#     tilemapSetLinearVelocity $tm $player 0 $vel_y
# }

# Optional: handle other keys via onKeyPress
proc onKeyPress {keycode} {
    global tm player pushable goal_reached
    
    # R key (82) = reset
    if {$keycode == 82} {
        reset_puzzle
    }
}

proc reset_puzzle {} {
    global tm player pushable goal_reached
    
    if {$tm eq ""} return
    
    set goal_reached 0
    
    # Reset positions from object data
    set objects [tilemapGetObjects $tm]
    foreach obj $objects {
        set type [dict get $obj type]
        set x [dict get $obj x]
        set y [dict get $obj y]
        
        if {$type eq "spawn"} {
            tilemapSetSpritePosition $tm $player $x $y
            # Also reset velocity
            tilemapSetLinearVelocity $tm $player 0 0
        } elseif {$type eq "pushable"} {
            tilemapSetSpritePosition $tm $pushable $x $y
            tilemapSetLinearVelocity $tm $pushable 0 0
        }
    }
    
    puts "Puzzle reset!"
}

proc start_puzzle {} {
    glistInit 1
    resetObjList
    load_puzzle
    glistSetVisible 1
    redraw
}

puts "Two Rooms Puzzle loaded."
puts "Run 'start_puzzle' to begin."
