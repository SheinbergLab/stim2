# test_sidescroll.tcl
#
# Multi-lane side-scroller test with constrained lane changes
#
# The level has 3 lanes (platforms at different heights):
#   Lane 0 (bottom): Ground level - always solid
#   Lane 1 (middle): Middle platform - has gaps
#   Lane 2 (top):    Top platform - has gaps
#
# Rules:
#   - Player falls with gravity (will drop through gaps)
#   - Player can only jump UP when there's a gap above (no ceiling)
#   - Must plan ahead: jump through gaps to reach higher lanes,
#     but watch out for gaps in your current lane!
#
# Controls:
#   Up Arrow / Space - Jump (only works if gap above)
#   R - Reset
#
# Usage:
#   source test_sidescroll.tcl
#   start_sidescroll

# Global state
set tm ""
set player ""
set trial_active 0
set trial_result ""
set current_lane 0

# Lane Y positions (in world units, bottom to top)
# These correspond to the platform rows in the TMX
# Row 11 (ground) = lane 0, Row 8 (middle) = lane 1, Row 5 (top) = lane 2
# With 32 pixels per meter and Y flipped, calculate from TMX
set lane_y {1.5 4.5 7.5}  ;# Approximate Y positions for each lane

# Movement/physics parameters  
set scroll_speed 3.0       ;# world units per second
set jump_impulse 10.0      ;# upward impulse for jumping
set gravity_y -25.0        ;# gravity strength (stronger for snappier feel)

# Animation parameters
set anim_fps 6.0           ;# frames per second for run animation

set goal_collected 0

proc load_sidescroll {} {
    global tm player scroll_speed gravity_y anim_fps current_lane goal_sprite
    
    set current_lane 0
    
    # Create tilemap
    set tm [tilemapCreate]
    
    # Set gravity - side-scroller style
    tilemapSetGravity $tm 0 $gravity_y
    
    # Disable auto-center since we want the level to start at the left
    tilemapSetAutoCenter $tm 0
    
    # Load the TMX
    set ppm 32
    set result [tilemapLoadTMX $tm "test_sidescroll.tmx" -pixels_per_meter $ppm]
    puts "Loaded: $result"
    
    # Get map info
    set info [tilemapGetMapInfo $tm]
    puts "Map info: $info"
    
    set world_w [dict get $info world_width]
    set world_h [dict get $info world_height]
    
    # Find spawn point from objects
    set objects [tilemapGetObjects $tm]
    puts "Objects: $objects"
    
    set spawn_x 2.0
    set spawn_y 3.0  ;# Start on middle lane
    
    set gcount -1
	foreach obj $objects {
		set name [dict get $obj name]
		set type [dict get $obj type]
		set x [dict get $obj x]
		set y [dict get $obj y]
		
		if {$type eq "spawn"} {
			set gw [expr {[dict get $obj width] / double($ppm)}]
			set gh [expr {[dict get $obj height] / double($ppm)}]
			set spawn_x [expr {$x + ($gw / 2.0)}]
			set spawn_y [expr {$y + ($gh / 2.0)}]
			puts "Spawn at: $spawn_x, $spawn_y"
		} elseif {$type eq "goal"} {
			puts "Goal at: $x, $y"
			# Create goal as a sensor sprite
			set gw [expr {[dict get $obj width] / double($ppm)}]
			set gh [expr {[dict get $obj height] / double($ppm)}]
			 puts "Goal size: gw=$gw, gh=$gh"

			set cx [expr {$x + ($gw / 2.0)}]
			set cy [expr {$y - ($gh / 2.0)}]
			       puts "Calculated center: cx=$cx, cy=$cy"

			set goal_sprite [tilemapCreateSprite $tm goal_[incr gcount] 3 $cx $cy $gw $gh]
			        puts "Created sprite: $goal_sprite"

			tilemapSpriteAddBody $tm $goal_sprite static -sensor 1
			tilemapSetSpriteVisible $tm $goal_sprite 1
			tilemapSetSpriteRotation $tm $goal_sprite [expr {rand()*2*3.14159}]
		}
	}
    
    # Create player sprite at spawn point
    set player [tilemapCreateSprite $tm "player" 4 $spawn_x $spawn_y 0.8 0.8]

    tilemapSpriteAddBody $tm $player dynamic -fixedrotation 1 -damping 0.1 -friction 0.5
    puts "Created player sprite: $player at $spawn_x, $spawn_y"
    
    # Set up run animation
    tilemapSetSpriteAnimation $tm $player $anim_fps {4 5 6 7} 1
    tilemapPlayAnimation $tm $player 1
    
    # Set initial horizontal velocity to match scroll speed
    tilemapSetLinearVelocity $tm $player $scroll_speed 0
    
    # Set up collision callback
    tilemapSetCollisionCallback $tm on_collision
    
    # Add pre-script for per-frame updates
    addPreScript $tm onUpdate
    
    # Add to display
    glistAddObject $tm 0
    glistSetDynamic 0 1
    
    # --- Camera Setup ---
    tilemapSetCameraMode $tm scroll $scroll_speed 0
    tilemapSetCameraSmooth $tm 0

    
    # Set camera bounds
    set cam_margin 6.0
    tilemapSetCameraBounds $tm $cam_margin [expr {$world_w - $cam_margin}] -999 999
    
    # Start camera at player position  
    tilemapSetCameraPos $tm $spawn_x [expr {$world_h / 2.0}]
    
    puts "\n=== MULTI-LANE SIDE-SCROLLER ==="
    puts "3 lanes with gaps - plan your route!"
    puts "Jump (Up/Space) only works when there's a gap above you"
    puts "Watch out for gaps in your lane - you'll fall through!"
    puts "Press R to reset"
    
    
    set info [tilemapGetMapInfo $tm]
	puts "Map info: $info"
}


proc on_collision {bodyA bodyB} {
    global tm collected_goals
    
    set goal_name ""
    if {$bodyA eq "player" && [string match "goal_*" $bodyB]} {
        set goal_name $bodyB
    } elseif {$bodyB eq "player" && [string match "goal_*" $bodyA]} {
        set goal_name $bodyA
    } else {
        return
    }
    
    set collected_goals($goal_name) 1
    
    set sprite [tilemapGetSpriteByName $tm $goal_name]
    tilemapSetSpriteVisible $tm $sprite 0
    puts "*** COLLECTED $goal_name! ***"
}

#============================================================
# Lane detection and jump logic
#============================================================

proc get_current_lane {} {
    global tm player lane_y
    
    set info [tilemapGetSpriteInfo $tm $player]
    set py [dict get $info y]
    
    # Find which lane we're closest to
    set best_lane 0
    set best_dist 999
    set idx 0
    foreach ly $lane_y {
        set dist [expr {abs($py - $ly)}]
        if {$dist < $best_dist} {
            set best_dist $dist
            set best_lane $idx
        }
        incr idx
    }
    return $best_lane
}

proc can_jump_up {} {
    global tm player lane_y
    
    set info [tilemapGetSpriteInfo $tm $player]
    set px [dict get $info x]
    set py [dict get $info y]
    
    set current [get_current_lane]
    
    # Can't jump if already at top lane
    if {$current >= [llength $lane_y] - 1} {
        return 0
    }
    
    # Check if there's a gap above us (no collision)
    # Query a point above the player where the next lane would be
    set above_y [lindex $lane_y [expr {$current + 1}]]
    
    # Check slightly ahead of player (where they'll be when jump peaks)
    set check_x [expr {$px + 0.5}]
    
    # Use tilemapQueryPoint to check if space above is clear
    set blocked [tilemapQueryPoint $tm $check_x $above_y -ignore $player]
    
    return [expr {!$blocked}]
}

proc is_grounded {} {
    global tm player
    
    set info [tilemapGetSpriteInfo $tm $player]
    set vy [dict get $info vel_y]
    
    # Simple ground check: vertical velocity near zero or negative (falling)
    # and there's something below us
    if {$vy > 0.5} {
        return 0  ;# Moving up, not grounded
    }
    
    set px [dict get $info x]
    set py [dict get $info y]
    
    # Check if there's ground below
    set below_y [expr {$py - 0.5}]
    set has_ground [tilemapQueryPoint $tm $px $below_y -ignore $player]
    
    return $has_ground
}

#============================================================
# Per-frame update
#============================================================

proc onUpdate {} {
    global tm player scroll_speed trial_active trial_result current_lane
    
    if {$tm eq "" || $player eq ""} return
    
    # Always maintain scroll_speed horizontally
    if {$trial_active} {
        set info [tilemapGetSpriteInfo $tm $player]
        set vy [dict get $info vel_y]
        tilemapSetLinearVelocity $tm $player $scroll_speed $vy
        
        # Update current lane for display/logic
        set current_lane [get_current_lane]
    }
    
    check_player_status
}

proc check_player_status {} {
    global tm player trial_active trial_result
    
    if {$tm eq "" || $player eq "" || !$trial_active} return
    
    set pinfo [tilemapGetSpriteInfo $tm $player]
    set cinfo [tilemapGetCameraInfo $tm]
    
    set px [dict get $pinfo x]
    set py [dict get $pinfo y]
    set cx [dict get $cinfo x]
    
    # Fell behind camera
    if {$px < [expr {$cx - 7}]} {
        set trial_active 0
        set trial_result "FAILED - fell behind"
        puts "*** FELL BEHIND - TRIAL FAILED ***"
    }
    
    # Fell off bottom
    if {$py < -1} {
        set trial_active 0
        set trial_result "FAILED - fell off"
        puts "*** FELL OFF - TRIAL FAILED ***"
    }
}

#============================================================
# Input handlers
#============================================================

proc onUpArrow {} {
    do_jump
}

proc onLeftArrow {} {
#     global tm player move_speed
#     if {$tm eq "" || $player eq ""} return
#     
#     set info [tilemapGetSpriteInfo $tm $player]
#     set vel_y [dict get $info vel_y]
#     tilemapSetLinearVelocity $tm $player -4.0 $vel_y 
}

proc onRightArrow {} {
#     global tm player
#     if {$tm eq "" || $player eq ""} return
#     
#     set info [tilemapGetSpriteInfo $tm $player]
#     set vel_y [dict get $info vel_y]
#     tilemapSetLinearVelocity $tm $player 4.0 $vel_y
}

proc onDownArrow {} {
    # Could add fast-fall here
}

proc do_jump {} {
    global tm player jump_impulse trial_active
    
    if {$tm eq "" || $player eq ""} return
    if {!$trial_active} return
    
    if {![is_grounded]} {
        return
    }
    
    tilemapApplyImpulse $tm $player 0 $jump_impulse
}

proc onKeyPress {keycode} {
    if {$keycode == 32} {
        do_jump
    } elseif {$keycode == 82} {
        reset_sidescroll
    }
}

#============================================================
# Reset / Start
#============================================================

proc reset_sidescroll {} {
    global tm player trial_active trial_result scroll_speed current_lane collected_goals
        
    if {$tm eq ""} return
    
    # Clear collected goals tracking
    array unset collected_goals
    array set collected_goals {}
    
    # Make all goal sprites visible again
    set sprite_count [tilemapGetSpriteCount $tm]  ;# Need to add this command
    for {set i 0} {$i < $sprite_count} {incr i} {
        set info [tilemapGetSpriteInfo $tm $i]
        set name [dict get $info name]
        if {[string match "goal_*" $name]} {
            tilemapSetSpriteVisible $tm $i 1
        }
    }
    
    set trial_active 1
    set trial_result ""
    set current_lane 0
    
    set objects [tilemapGetObjects $tm]
    foreach obj $objects {
        set type [dict get $obj type]
        if {$type eq "spawn"} {
            set x [dict get $obj x]
            set y [dict get $obj y]
            tilemapSetSpritePosition $tm $player $x $y
            tilemapSetLinearVelocity $tm $player $scroll_speed 0
            
            set info [tilemapGetMapInfo $tm]
            tilemapSetCameraPos $tm $x [expr {[dict get $info world_height] / 2.0}]
            tilemapSetCameraMode $tm scroll $scroll_speed 0
            
            tilemapPlayAnimation $tm $player 1
            break
        }
    }
    
    puts "Trial reset - GO!"
}

proc start_sidescroll {} {
    global trial_active
    
    glistInit 1
    resetObjList
    load_sidescroll
    glistSetVisible 1
    redraw
    
    set trial_active 1
    puts "\n=== TRIAL STARTED ==="
}

#============================================================
# Debug
#============================================================

proc debug_status {} {
    global tm player current_lane
    if {$tm eq "" || $player eq ""} {
        puts "Not loaded"
        return
    }
    
    set pinfo [tilemapGetSpriteInfo $tm $player]
    set cinfo [tilemapGetCameraInfo $tm]
    
    puts "Player: x=[format %.2f [dict get $pinfo x]] y=[format %.2f [dict get $pinfo y]] vy=[format %.2f [dict get $pinfo vel_y]]"
    puts "Camera: x=[format %.2f [dict get $cinfo x]]"
    puts "Lane: $current_lane  Grounded: [is_grounded]  CanJump: [can_jump_up]"
}

puts "Multi-lane side-scroller loaded."
puts "Run 'start_sidescroll' to begin."
puts "Run 'debug_status' to check state."