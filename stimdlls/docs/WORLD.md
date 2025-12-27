# World Module

A 2D game/simulation engine combining OpenGL rendering with Box2D physics, designed for Tcl-based behavioral experiments and interactive applications.

## Architecture

The module is split into focused source files:

| File | Lines | Purpose |
|------|-------|---------|
| `world.c` | ~400 | Core orchestration, lifecycle, physics world, collision callbacks |
| `world_sprite.c` | ~540 | Sprite instances, physics bodies, animation, forces |
| `world_spritesheet.c` | ~580 | Sprite sheet loading, parsing, frame name lookup |
| `world_render.c` | ~240 | OpenGL rendering for tiles and sprites |
| `world_camera.c` | ~200 | Camera modes (locked, follow, lerp) |
| `world_atlas.c` | ~150 | Texture atlas management |
| `world_tilemap.c` | ~725 | TMX map loading, tile layers, collision |
| `world_internal.h` | ~335 | Shared types, constants, internal API |

## Tcl Command Reference

### World Lifecycle

```tcl
set w [worldCreate]                    ;# Create a new world, returns ID
worldSetGravity $w 0 -9.8              ;# Set gravity vector
```

### Loading Content

```tcl
# Load a Tiled TMX map
worldLoadTMX $w "level.tmx" -pixels_per_meter 32 -collision_layer "collision"

# Load a sprite sheet (Aseprite JSON or Kenney XML via spritesheet package)
set sheet_data [yajl::json2dict [spritesheet::process "sheet.json"]]
worldAddSpriteSheet $w "player" $sheet_data

# Query loaded sprite sheets
worldGetSpriteSheets $w                ;# Returns list of sheet info dicts
worldGetAnimationFrames $w "player" "run"  ;# Get frame indices for animation
```

### Sprites

```tcl
# Create sprite from sprite sheet
set sprite [worldCreateSpriteFromSheet $w "player" $x $y ?$frame?]

# Create sprite from tile atlas (legacy)
set sprite [worldCreateSprite $w "name" $tile_id $x $y $w $h ?$atlas?]

# Create from tileset with optional animation
set sprite [worldCreateSpriteFromTileset $w "name" "sheetName" $x $y $w $h ?animation?]

# Sprite manipulation
worldSetSpritePosition $w $sprite $x $y
worldSetSpriteRotation $w $sprite $radians
worldSetSpriteVisible $w $sprite 0|1
worldSetSpriteTile $w $sprite $tile_id
worldSetSpriteFrame $w $sprite $frame_index
worldSetSpriteFrameByName $w $sprite "frame_name"  ;# Lookup by name

# Query
worldGetSpriteCount $w
worldGetSpriteByName $w "name"         ;# Returns sprite index or -1
worldGetSpriteInfo $w $sprite          ;# Returns dict with x, y, w, h, angle, vx, vy, etc.

# Remove
worldRemoveSprite $w $sprite
```

### Physics Bodies

```tcl
# Add physics body to sprite (auto-detects collision shapes from sprite sheet)
worldSpriteAddBody $w $sprite \
    -type dynamic|static|kinematic \
    -density 1.0 \
    -friction 0.3 \
    -restitution 0.0 \
    -fixedRotation 0|1 \
    -bullet 0|1 \
    -sensor 0|1 \
    -gravityScale 1.0

# Apply forces/impulses
worldApplyImpulse $w $sprite $ix $iy
worldApplyForce $w $sprite $fx $fy
worldSetLinearVelocity $w $sprite $vx $vy

# Query contacts
worldGetContacts $w                    ;# Returns list of {bodyA bodyB} pairs
```

### Animation

```tcl
# Set animation by frame list
worldSetSpriteAnimation $w $sprite {0 1 2 3} $fps ?$loop?

# Set animation by name (from sprite sheet)
worldSetSpriteAnimationByName $w $sprite "sheetName" "animName" ?$fps? ?$loop?

# Control playback
worldPlayAnimation $w $sprite 0|1      ;# Stop/start
```

### Camera

```tcl
worldSetCameraMode $w locked|follow|lerp
worldSetCameraPos $w $x $y
worldSetCameraTarget $w $sprite        ;# For follow/lerp modes
worldSetCameraLerp $w 0.1              ;# Smoothing factor for lerp mode
worldSetCameraZoom $w 1.0
worldGetCameraInfo $w                  ;# Returns dict with x, y, zoom, mode
```

### Collision Callbacks

```tcl
# Set Tcl callback for collision events
worldSetCollisionCallback $w myCollisionProc

# Callback receives: proc myCollisionProc {bodyA bodyB eventType}
#   eventType: "begin" or "end"
```

### Spatial Queries

```tcl
# Point query - check if point hits any body
worldQueryPoint $w $x $y ?-ignore $sprite?

# AABB query - find bodies in rectangle
worldQueryAABB $w $minX $minY $maxX $maxY
```

### TMX Objects

```tcl
# Get objects from TMX object layers
worldGetObjects $w ?type?              ;# Filter by type name
# Returns list of dicts: {name, type, x, y, width, height, properties...}
```

## Sprite Sheet Format

The module accepts sprite sheet data as Tcl dicts, typically produced by the `spritesheet` package from Aseprite JSON or Kenney XML files.

```tcl
{
    _metadata {
        image "spritesheet.png"
        texture_width 512
        texture_height 256
        frame_count 24
        canonical_canvas {w 64 h 64}
        animations {
            idle {frames {0 1 2 3} fps 10 direction forward}
            run  {frames {4 5 6 7 8 9} fps 15 direction forward}
        }
    }
    frame_0 {
        frame_rect {x 0 y 0 w 64 h 64}
        fixtures {
            {shape polygon data {{x 10 y 5} {x 54 y 5} {x 54 y 60} {x 10 y 60}}}
        }
    }
    frame_1 { ... }
}
```

Collision fixtures support three shape types:
- `polygon` - list of {x y} vertices
- `box` - {x y w h}
- `circle` - {center_x center_y radius}

Coordinates are in pixels relative to the frame, automatically normalized during loading.

## Example Usage

```tcl
package require yajltcl
package require spritesheet

# Setup
resetObjList
glistInit 1

# Create world
set w [worldCreate]
worldSetGravity $w 0 -9.8

# Load sprite sheet
set json [spritesheet::process "player.json" -epsilon 4.0]
worldAddSpriteSheet $w "player" [yajl::json2dict $json]

# Create player sprite
set player [worldCreateSpriteFromSheet $w "player" 0 5]
worldSpriteAddBody $w $player -type dynamic -fixedRotation 1
worldSetSpriteAnimationByName $w $player "player" "idle"

# Create ground
set ground [worldCreateSprite $w "ground" 0 0 -2 20 0.5]
worldSpriteAddBody $w $ground -type static

# Camera
worldSetCameraMode $w follow
worldSetCameraTarget $w $player

# Display
glistAddObject $w 0
glistSetDynamic 0 1
glistSetVisible 1
```

## Building

Add to CMakeLists.txt:
```cmake
add_library(world SHARED
    world.c
    world_sprite.c
    world_spritesheet.c
    world_render.c
    world_camera.c
    world_atlas.c
    world_tilemap.c
)
target_link_libraries(world box2d OpenGL::GL tcl)
```

## Dependencies

- **Box2D v3** - Physics simulation
- **OpenGL** - Rendering
- **Tcl 8.6+/9.0** - Scripting interface
- **stb_image** - Image loading (header-only)
- **yajltcl** - JSON parsing (for sprite sheets)
- **mxml** - XML parsing (for TMX maps)
