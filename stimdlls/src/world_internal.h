/*
 * world_internal.h
 *
 * Internal shared types for the world module.
 * Not for public consumption - use world.h instead.
 */

#ifndef WORLD_INTERNAL_H
#define WORLD_INTERNAL_H

#include <tcl.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stim2.h>
#include <objname.h>
#include "box2d/box2d.h"
#include "aseprite_json.h"

/*========================================================================
 * Configuration
 *========================================================================*/

#define WORLD_MAX_TILES          8192
#define WORLD_MAX_SPRITES        512
#define WORLD_MAX_FRAMES         512
#define WORLD_MAX_ATLASES        16
#define WORLD_MAX_OBJECTS        256
#define WORLD_MAX_PATH_LEN       512
#define WORLD_MAX_COLLISION_VERTS    8
#define WORLD_MAX_SHAPES_PER_TILE    8
#define WORLD_MAX_TILE_COLLISIONS    256
#define WORLD_MAX_SPRITE_TILESETS    16
#define WORLD_MAX_OBJECT_PROPS       16
#define WORLD_MAX_SHAPES_PER_BODY    16

/*========================================================================
 * Forward Declarations
 *========================================================================*/

typedef struct World        World;
typedef struct Sprite       Sprite;
typedef struct Camera       Camera;
typedef struct Atlas        Atlas;
typedef struct SpriteSheet  SpriteSheet;
typedef struct Maze3D Maze3D;

/*========================================================================
 * Collision Types (shared by sprite, tilemap)
 *========================================================================*/

typedef enum {
    SHAPE_NONE = 0,
    SHAPE_BOX,
    SHAPE_POLYGON,
    SHAPE_CIRCLE
} CollisionShapeType;

typedef struct {
    CollisionShapeType type;
    
    /* BOX: offset and size as fraction of tile (0.0-1.0) */
    float box_x, box_y;
    float box_w, box_h;
    
    /* POLYGON: vertices as fraction of tile */
    float verts_x[WORLD_MAX_COLLISION_VERTS];
    float verts_y[WORLD_MAX_COLLISION_VERTS];
    int vert_count;

    /* CIRCLE */
    float circle_x, circle_y;
    float circle_radius;
} CollisionShape;

typedef struct {
    CollisionShape shapes[WORLD_MAX_SHAPES_PER_TILE];
    int shape_count;
} TileCollision;

/*========================================================================
 * Atlas - Texture atlas for tiles/sprites (world_atlas.c)
 *========================================================================*/

struct Atlas {
    GLuint texture;
    char filename[WORLD_MAX_PATH_LEN];
    int width, height;
    int tile_width, tile_height;
    int cols, rows;
    int firstgid;
    float tile_u, tile_v;   /* normalized tile size */
};

/*========================================================================
 * Camera (world_camera.c)
 *========================================================================*/

typedef enum {
    CAM_LOCKED,
    CAM_FIXED_SCROLL,
    CAM_FOLLOW,
    CAM_FOLLOW_DEADZONE,
    CAM_FOLLOW_LOOKAHEAD
} CameraMode;

struct Camera {
    float x, y;
    float target_x, target_y;
    float smooth_speed;
    
    CameraMode mode;
    
    /* Fixed scroll */
    float scroll_vx, scroll_vy;
    
    /* Follow */
    int follow_sprite;
    float deadzone_w, deadzone_h;
    float lookahead_x, lookahead_y;
    
    /* Bounds */
    float min_x, max_x;
    float min_y, max_y;
    int use_bounds;
};

/*========================================================================
 * Tile Instance (world_tilemap.c)
 *========================================================================*/

typedef struct {
    char name[64];
    float x, y, w, h;
    float u0, v0, u1, v1;
    int layer, atlas_id, has_body;
    int is_collision;    /* on collision layer (even if body is merged) */
} TileInstance;

/*========================================================================
 * TMX Objects (world_tilemap.c)
 *========================================================================*/

typedef struct {
    char name[32];
    char value[256];
    char type[16];
} TMXProperty;

typedef struct {
    char name[64], type[64];
    float x, y, width, height;
    int is_point, is_ellipse;
    TMXProperty props[WORLD_MAX_OBJECT_PROPS];
    int prop_count;
} TMXObject;

/*========================================================================
 * Sprite Sheet / Tileset (world_spritesheet.c)
 *========================================================================*/

typedef struct {
    float x, y, w, h;           /* pixel rect in sprite sheet */
    float u0, v0, u1, v1;       /* normalized texture coords */
} SpriteFrame;

struct SpriteSheet {
    char name[64];
    int firstgid;
    int tile_width, tile_height;
    int atlas_id;
    
    /* Aseprite data if available (embedded, not pointer) */
    AsepriteData aseprite;
    int has_aseprite;
    
    /* Frame data */
    SpriteFrame frames[WORLD_MAX_FRAMES];
    int frame_count;
    
    /* Frame name -> index lookup */
    Tcl_HashTable frame_names;
    int frame_names_init;
    
    float canonical_w, canonical_h;
    
    /* Per-frame collision */
    TileCollision frame_collisions[WORLD_MAX_TILE_COLLISIONS];
    int tile_collision_count;
};

/*========================================================================
 * Sprite (world_sprite.c)
 *========================================================================*/

struct Sprite {
    char name[64];
    float x, y, angle, w, h;
    float u0, v0, u1, v1;

    int sprite_sheet_id;
    int current_frame;
    int uses_sprite_sheet;

    int atlas_id, tile_id, visible, has_body;
    b2BodyId body;
    float body_offset_x, body_offset_y;

    /* Hitbox from Aseprite */
    int has_hitbox_data;
    float hitbox_w_ratio, hitbox_h_ratio;
    float hitbox_offset_x, hitbox_offset_y;
    
    /* Animation */
    int anim_frames[32];
    int anim_frame_count;
    int anim_current_frame;
    float anim_fps;
    float anim_time;
    int anim_loop;
    int anim_playing;
};

/*========================================================================
 * World - Main container (world.c)
 *========================================================================*/

struct World {
    /* Tiles */
    TileInstance tiles[WORLD_MAX_TILES];
    int tile_count;
    int layer_counts[8];
    int num_layers;
    
    /* Sprites */
    Sprite sprites[WORLD_MAX_SPRITES];
    int sprite_count;
    
    /* Sprite sheets */
    SpriteSheet sprite_sheets[WORLD_MAX_SPRITE_TILESETS];
    int sprite_sheet_count;
    
    /* TMX Objects */
    TMXObject objects[WORLD_MAX_OBJECTS];
    int object_count;
    
    /* Atlases */
    Atlas atlases[WORLD_MAX_ATLASES];
    int atlas_count;
    
    /* Camera */
    Camera camera;
    
    /* Rendering */
    GLuint shader_program;
    GLuint vao, vbo;
    GLuint sprite_vao, sprite_vbo;
    GLint u_texture, u_modelview, u_projection;
    int tiles_dirty;
    
    /* Physics */
    b2WorldId world_id;
    int has_world;
    b2Vec2 gravity;
    int substep_count;
    Tcl_HashTable body_table;
    int body_count;
    
    /* Map info */
    int map_width, map_height;
    int tile_pixel_width, tile_pixel_height;
    float tile_size;
    float pixels_per_meter;
    float offset_x, offset_y;
    char base_path[WORLD_MAX_PATH_LEN];
    
    /* Options */
    int auto_center;
    int normalize;
    float norm_scale;
    
    /* Callbacks */
    char collision_callback[256];
    Tcl_Interp *interp;

    /* 3D Maze (world_maze3d.c) */
    Maze3D *maze3d;
  
};

/*========================================================================
 * Internal APIs (called between modules)
 *========================================================================*/

/* world_atlas.c */
Atlas* world_find_atlas_for_gid(World *w, int gid);
void   world_get_tile_uvs(Atlas *a, int gid, float *u0, float *v0, float *u1, float *v1);
GLuint world_load_texture(const char *path, int *out_width, int *out_height);
int    world_load_atlas(World *w, const char *file, int tw, int th, int firstgid);
int    world_load_packed_atlas(World *w, const char *file);

/* world_camera.c */
void   world_camera_init(Camera *cam);
void   world_camera_update(World *w, float dt);

/* world_sprite.c */
void   world_sprite_update_animation(World *w, Sprite *sp, float dt);
void   world_sprite_sync_physics(World *w, Sprite *sp);
TileCollision* world_get_tile_collision(World *w, int tile_id);
void   world_sprite_register_commands(Tcl_Interp *interp, OBJ_LIST *olist);

/* world.c utilities (shared) */
void   world_get_directory(const char *path, char *dir, int max);
void   world_join_path(char *dest, int max, const char *dir, const char *file);

/* world_tilemap.c */
int    world_load_tmx(World *w, const char *filename);
void   world_tilemap_register_commands(Tcl_Interp *interp, OBJ_LIST *olist);

/* world_render.c */
int    world_init_gl(World *w);
void   world_render(World *w);
void   world_rebuild_vbo(World *w);
void   world_build_sprite_verts(World *w, Sprite *sp, float *verts);

/* world_camera.c */
void   world_camera_register_commands(Tcl_Interp *interp, OBJ_LIST *olist);

/* world_sprite.c - sprite sheet helpers (used by world.c) */
SpriteSheet* world_find_sprite_sheet(World *w, const char *name);
SpriteSheet* world_find_sprite_sheet_by_gid(World *w, int gid);

/* world_spritesheet.c */
void   world_spritesheet_register_commands(Tcl_Interp *interp, OBJ_LIST *olist);
int    world_spritesheet_find_frame(SpriteSheet *ss, const char *name);

/* world_maze3d.c */
Maze3D* maze3d_create(void);
void    maze3d_destroy(Maze3D *m);
void    maze3d_render(World *w, Maze3D *m);
void    maze3d_render_2d_marker(World *w, Maze3D *m);
void    maze3d_update_items(World *w, Maze3D *m, float dt);
void    maze3d_sync_camera(World *w, Maze3D *m);
void    maze3d_rotate(Maze3D *m, float dyaw, float dpitch);
int     maze3d_is_enabled(Maze3D *m);
void    world_maze3d_register_commands(Tcl_Interp *interp, OBJ_LIST *olist);

/*========================================================================
 * Global State
 *========================================================================*/

extern int WorldID;  /* graphics object type ID, set in World_Init */

#endif /* WORLD_INTERNAL_H */
