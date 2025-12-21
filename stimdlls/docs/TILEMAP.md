# Tilemap Module

A stim2 module for tile-based worlds with integrated Box2D physics. Loads TMX files created with [Tiled Map Editor](https://www.mapeditor.org/), renders tile layers and sprites, and provides physics simulation for interactive experiments.

## Overview

The tilemap module combines:
- **TMX loading** - Parse Tiled map files including tilesets, layers, and objects
- **Batched tile rendering** - Efficient single-draw-call rendering of static tiles
- **Sprite system** - Dynamic entities with position, rotation, and animation
- **Box2D physics** - Collision detection, rigid body dynamics, forces/impulses
- **Tcl integration** - Full scripting control for experimental paradigms

## Quick Start

```tcl
# Load the module
load tilemap

# Create tilemap (auto-centers by default)
set tm [tilemapCreate]

# Set physics mode
tilemapSetGravity $tm 0 0          ;# Top-down (no gravity)
# tilemapSetGravity $tm 0 -10      ;# Platformer (gravity down)

# Load a TMX file
set result [tilemapLoadTMX $tm "level.tmx" -pixels_per_meter 32]
puts "Loaded: $result"

# Create sprites from TMX objects
foreach obj [tilemapGetObjects $tm "spawn"] {
    set x [dict get $obj x]
    set y [dict get $obj y]
    set player [tilemapCreateSprite $tm "player" 4 $x $y 1.0 1.0]
    tilemapSpriteAddBody $tm $player dynamic
}

# Add to display
glistAddObject $tm 0
glistSetDynamic 0 1
```

## TMX File Structure

The module expects TMX files with:

### Layers
- **Background** - Visual tiles without collision (rendered first)
- **Collision** - Tiles that generate static physics bodies
- **Other layers** - Additional visual layers

### Object Layers
Objects define spawn points, goals, triggers, and dynamic entities:
```xml
<objectgroup name="Objects">
  <object name="player_spawn" type="spawn" x="96" y="160">
    <point/>
  </object>
  <object name="goal" type="goal" x="416" y="160" width="32" height="32"/>
  <object name="pushable" type="pushable" x="224" y="128" width="32" height="32">
    <properties>
      <property name="tile_id" type="int" value="3"/>
      <property name="density" type="float" value="2.0"/>
    </properties>
  </object>
</objectgroup>
```

### Custom Properties
Objects can have custom properties (set in Tiled's Properties panel):
- `tile_id` (int) - Which tile to use for the sprite
- `density` (float) - Physics body density
- `friction` (float) - Surface friction
- `damping` (float) - Linear damping (slowdown)

These are accessible via `tilemapGetObjects` and can drive sprite creation.

## Coordinate System

- **TMX files**: Top-left origin, Y-down, pixel units
- **World space**: Center origin, Y-up, meter units
- **Auto-center**: By default, maps are centered at (0,0)

The `pixels_per_meter` parameter controls the conversion (default: 32).

## Commands Reference

### Tilemap Management

#### tilemapCreate
Create a new tilemap object.
```tcl
set tm [tilemapCreate]
```
Returns: tilemap object ID

#### tilemapLoadTMX
Load a TMX file into the tilemap.
```tcl
tilemapLoadTMX $tm "file.tmx" ?-pixels_per_meter N? ?-collision_layer NAME?
```
- `-pixels_per_meter` - Conversion factor (default: 32)
- `-collision_layer` - Layer name for collision bodies (default: "Collision")

Returns: dict with `tiles`, `bodies`, `objects`, `atlases` counts

#### tilemapSetGravity
Set world gravity.
```tcl
tilemapSetGravity $tm gx gy
```
- Top-down: `0 0`
- Platformer: `0 -10`

#### tilemapSetAutoCenter
Enable/disable auto-centering on load.
```tcl
tilemapSetAutoCenter $tm 0|1
```

#### tilemapSetOffset
Manually offset all tiles, sprites, and bodies.
```tcl
tilemapSetOffset $tm offset_x offset_y
```

#### tilemapGetMapInfo
Get map dimensions and settings.
```tcl
set info [tilemapGetMapInfo $tm]
# Returns: {width_tiles N height_tiles N tile_size F world_width F world_height F pixels_per_meter F}
```

### Sprites

#### tilemapCreateSprite
Create a dynamic sprite.
```tcl
set sprite [tilemapCreateSprite $tm name tile_id x y width height ?atlas_id?]
```
- `name` - Unique identifier (used in collision callbacks)
- `tile_id` - Tile index in the atlas
- `x y` - World position
- `width height` - Size in world units
- `atlas_id` - Which atlas to use (default: 0)

Returns: sprite index

#### tilemapSpriteAddBody
Add a physics body to a sprite.
```tcl
tilemapSpriteAddBody $tm sprite_id ?type? ?options...?
```
Types: `dynamic` (default), `static`, `kinematic`

Options:
- `-fixedrotation 0|1` - Lock rotation (default: 1 for top-down feel)
- `-damping N` - Linear damping (default: 5.0)
- `-friction N` - Surface friction (default: 0.5)
- `-density N` - Body density (default: 1.0)

Example:
```tcl
# Heavy pushable block
tilemapSpriteAddBody $tm $block dynamic -density 3.0 -damping 8.0

# Light nimble player
tilemapSpriteAddBody $tm $player dynamic -density 0.5 -damping 3.0
```

#### tilemapSetSpritePosition
Teleport a sprite (and its body) to a position.
```tcl
tilemapSetSpritePosition $tm sprite_id x y
```

#### tilemapSetSpriteTile
Change which tile the sprite displays.
```tcl
tilemapSetSpriteTile $tm sprite_id tile_id
```

#### tilemapGetSpriteInfo
Get sprite state for debugging.
```tcl
set info [tilemapGetSpriteInfo $tm sprite_id]
# Returns: {name S x F y F angle F has_body I body_x F body_y F vel_x F vel_y F}
```

### Physics Control

#### tilemapApplyImpulse
Apply an instant velocity change (good for jumping).
```tcl
tilemapApplyImpulse $tm sprite_id ix iy
```

#### tilemapSetLinearVelocity
Set velocity directly (good for movement).
```tcl
tilemapSetLinearVelocity $tm sprite_id vx vy
```

#### tilemapApplyForce
Apply continuous force (good for thrust/acceleration).
```tcl
tilemapApplyForce $tm sprite_id fx fy
```

### Animation

#### tilemapSetSpriteAnimation
Configure animation frames for a sprite.
```tcl
tilemapSetSpriteAnimation $tm sprite_id fps {frame_list} ?loop?
```
- `fps` - Frames per second
- `frame_list` - List of tile IDs
- `loop` - 1 to loop, 0 for once (default: 1)

Example:
```tcl
tilemapSetSpriteAnimation $tm $player 8 {0 1 2 3 2 1} 1
```

#### tilemapPlayAnimation
Start or stop animation.
```tcl
tilemapPlayAnimation $tm sprite_id 0|1
```

### Objects and Collisions

#### tilemapGetObjects
Get objects from TMX object layers.
```tcl
set objects [tilemapGetObjects $tm ?type_filter?]
```
Returns list of dicts:
```tcl
{name S type S x F y F width F height F properties {key value ...}}
```

#### tilemapGetContacts
Get collision events from the current frame.
```tcl
set contacts [tilemapGetContacts $tm]
# Returns: {begin {{shapeA shapeB} ...} end {{shapeA shapeB} ...}}
```

#### tilemapSetCollisionCallback
Register a Tcl proc to call on collisions.
```tcl
tilemapSetCollisionCallback $tm proc_name
```

The proc receives two body names:
```tcl
proc on_collision {bodyA bodyB} {
    if {$bodyA eq "player" && $bodyB eq "goal"} {
        puts "Player reached the goal!"
    }
}
tilemapSetCollisionCallback $tm on_collision
```

## Physics Modes

### Top-Down (Recommended for Puzzles)
```tcl
tilemapSetGravity $tm 0 0
# Bodies use default: fixedrotation=1, damping=5.0
```
- No gravity - objects stay where placed
- Locked rotation - no spinning
- High damping - things stop when not pushed
- Good for: Sokoban, Zelda-style, navigation tasks

### Platformer
```tcl
tilemapSetGravity $tm 0 -10
tilemapSpriteAddBody $tm $player dynamic -fixedrotation 1 -damping 0.1
```
- Gravity pulls down
- Low damping - floaty air control
- Good for: Mario-style, timing-based tasks

## Input Handling

The module integrates with stim2's key callbacks:

```tcl
# Movement speed
set move_speed 4

proc onLeftArrow {} {
    global tm player move_speed
    set info [tilemapGetSpriteInfo $tm $player]
    set vel_y [dict get $info vel_y]
    tilemapSetLinearVelocity $tm $player [expr {-$move_speed}] $vel_y
}

proc onRightArrow {} {
    global tm player move_speed
    set info [tilemapGetSpriteInfo $tm $player]
    set vel_y [dict get $info vel_y]
    tilemapSetLinearVelocity $tm $player $move_speed $vel_y
}

proc onUpArrow {} {
    global tm player move_speed
    set info [tilemapGetSpriteInfo $tm $player]
    set vel_x [dict get $info vel_x]
    tilemapSetLinearVelocity $tm $player $vel_x $move_speed
}

proc onDownArrow {} {
    global tm player move_speed
    set info [tilemapGetSpriteInfo $tm $player]
    set vel_x [dict get $info vel_x]
    tilemapSetLinearVelocity $tm $player $vel_x [expr {-$move_speed}]
}
```

## Cleanup

The tilemap properly cleans up when destroyed:
```tcl
resetObjList    ;# Calls tilemap_delete via GR_DELETEFUNCP
glistInit 1
```

This frees:
- OpenGL resources (VAOs, VBOs, textures, shaders)
- Box2D world and all bodies
- Hash tables and allocated memory

## Building

Add to CMakeLists.txt:
```cmake
add_library(tilemap SHARED src/tilemap.c src/tmx_xml.cpp)
target_link_libraries(tilemap ${COMMON_LIBS} ${BOX2D_LIB})
# tinyxml2 - either system or bundled
find_package(tinyxml2 QUIET)
if(tinyxml2_FOUND)
    target_link_libraries(tilemap tinyxml2::tinyxml2)
else()
    target_sources(tilemap PRIVATE src/tinyxml2.cpp)
endif()
```

Dependencies:
- Box2D v3
- tinyxml2
- stb_image.h (bundled)
- OpenGL/GLFW (via stim2)

## Tips

### Tile Atlas
Create a PNG with tiles arranged in a grid. Reference it in Tiled as a tileset. Tile index 0 is typically empty/transparent.

### Procedural Levels
TMX files are XML - generate them programmatically:
```tcl
proc generate_maze {width height} {
    # Generate maze data...
    set tmx "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    append tmx "<map version=\"1.10\" ...>\n"
    # ... build XML ...
    return $tmx
}
```

### Sprite Sheets
Commercial sprite sheets (e.g., from gameart2d.com) work well. Import into Tiled as a tileset, then reference tile IDs in your scripts.

### Performance
- Static tiles are batched into a single draw call
- Sprites are drawn individually (could be optimized for many sprites)
- Physics runs at frame rate with configurable substeps

## Example: Two-Room Puzzle

A complete example with pushable blocks:

```tcl
# Load module and create tilemap
load tilemap
set tm [tilemapCreate]
tilemapSetGravity $tm 0 0

# Load level
tilemapLoadTMX $tm "two_rooms.tmx" -pixels_per_meter 32

# Create sprites from objects
foreach obj [tilemapGetObjects $tm] {
    set type [dict get $obj type]
    set x [dict get $obj x]
    set y [dict get $obj y]
    
    if {$type eq "spawn"} {
        set player [tilemapCreateSprite $tm "player" 4 $x $y 1.0 1.0]
        tilemapSpriteAddBody $tm $player dynamic
    } elseif {$type eq "pushable"} {
        set block [tilemapCreateSprite $tm "pushable" 3 $x $y 1.0 1.0]
        tilemapSpriteAddBody $tm $block dynamic -density 2.0
    }
}

# Collision callback
proc on_collision {a b} {
    if {($a eq "player" && $b eq "goal") || ($a eq "goal" && $b eq "player")} {
        puts "*** GOAL REACHED! ***"
    }
}
tilemapSetCollisionCallback $tm on_collision

# Display
glistAddObject $tm 0
glistSetDynamic 0 1
```

## Version History

- **1.0** - Initial release
  - TMX loading with tinyxml2 wrapper
  - Batched tile rendering
  - Sprite system with Box2D physics
  - Animation support
  - Collision callbacks
  - Top-down and platformer physics modes
