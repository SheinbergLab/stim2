/*
 * tilemap.c
 *
 * Tilemap rendering module with integrated Box2D physics and TMX loading.
 * Designed for behavioral experiments using Tiled Map Editor levels.
 *
 * Features:
 *   - Load TMX files directly (tilemapLoadTMX)
 *   - Efficient batched tile rendering
 *   - Atlas texture support with per-tile UV coordinates
 *   - Integrated Box2D physics for collision tiles
 *   - Dynamic sprites that sync with physics bodies
 *   - Object layer support (spawn points, triggers, etc.)
 *
 * Usage:
 *   load tilemap
 *   set tm [tilemapCreate]
 *   tilemapLoadTMX $tm "level.tmx"
 *   glistAddObject $tm 0
 *   glistSetDynamic 0 1
 *
 * Author: DLS 2025
 */

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <tcl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stim2.h>
#include "aseprite_json.h"
#include "box2d/box2d.h"

/* stb_image - define implementation here for tilemap's use */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifndef M_PI
#define M_PI 3.14159265358979323
#endif

/* TMX XML wrapper functions (from tmx_xml.cpp) */
#ifdef __cplusplus
extern "C" {
#endif
void* tmx_xml_load(const char* filename);
void tmx_xml_free(void* doc);
void* tmx_xml_get_map(void* doc);
int tmx_xml_map_get_int(void* map, const char* attr);
const char* tmx_xml_map_get_string(void* map, const char* attr);
void* tmx_xml_first_tileset(void* map);
void* tmx_xml_next_tileset(void* tileset);
int tmx_xml_tileset_get_int(void* tileset, const char* attr);
const char* tmx_xml_tileset_get_string(void* tileset, const char* attr);
void* tmx_xml_tileset_get_image(void* tileset);
void* tmx_xml_first_layer(void* map);
void* tmx_xml_next_layer(void* layer);
const char* tmx_xml_layer_get_name(void* layer);
int tmx_xml_layer_get_int(void* layer, const char* attr);
void* tmx_xml_layer_get_data(void* layer);
const char* tmx_xml_data_get_text(void* data);
const char* tmx_xml_data_get_encoding(void* data);
const char* tmx_xml_data_get_compression(void* data);
void* tmx_xml_first_objectgroup(void* map);
void* tmx_xml_next_objectgroup(void* objgroup);
const char* tmx_xml_objectgroup_get_name(void* objgroup);
void* tmx_xml_first_object(void* objgroup);
void* tmx_xml_next_object(void* obj);
const char* tmx_xml_object_get_string(void* obj, const char* attr);
float tmx_xml_object_get_float(void* obj, const char* attr, float def);
int tmx_xml_object_is_point(void* obj);
int tmx_xml_object_is_ellipse(void* obj);
void* tmx_xml_first_properties(void* element);
void* tmx_xml_first_property(void* props);
void* tmx_xml_next_property(void* prop);
const char* tmx_xml_property_get_name(void* prop);
const char* tmx_xml_property_get_value(void* prop);
const char* tmx_xml_property_get_type(void* prop);
void tmx_xml_set_base_path(const char* path);
const char* tmx_xml_tileset_get_name(void* tileset);
void* tmx_xml_tileset_get_properties(void* tileset);
const char* tmx_xml_tileset_get_property(void* tileset, const char* prop_name);

/* Tile collision shape iteration */
void* tmx_xml_tileset_first_tile(void* tileset);
void* tmx_xml_tileset_next_tile(void* tile);
int   tmx_xml_tile_get_id(void* tile);
void* tmx_xml_tile_get_objectgroup(void* tile);

/* Polygon/polyline support for collision objects */
int         tmx_xml_object_has_polygon(void* obj);
const char* tmx_xml_object_get_polygon_points(void* obj);
int         tmx_xml_object_has_polyline(void* obj);
const char* tmx_xml_object_get_polyline_points(void* obj);

#ifdef __cplusplus
}
#endif

#define MAX_TILES 8192
#define MAX_SPRITES 256
#define MAX_ATLASES 4
#define MAX_OBJECTS 256
#define MAX_PATH_LEN 512
#define MAX_COLLISION_VERTS 8    /* Box2D limit for polygon vertices */
#define MAX_SHAPES_PER_TILE 4       /* Max collision shapes per tile */
#define MAX_TILE_COLLISIONS 256  /* Max tiles with custom collision per tileset */

/* Collision shape types */
typedef enum {
    SHAPE_NONE = 0,
    SHAPE_BOX,
    SHAPE_POLYGON
} CollisionShapeType;

/* Single collision shape within a tile */
typedef struct {
    CollisionShapeType type;
    
    /* For BOX type: offset and size as fraction of tile (0.0-1.0) */
    float box_x, box_y;      /* top-left corner */
    float box_w, box_h;      /* width/height */
    
    /* For POLYGON type: vertices as fraction of tile */
    float verts_x[MAX_COLLISION_VERTS];
    float verts_y[MAX_COLLISION_VERTS];
    int vert_count;
} COLLISION_SHAPE;

/* All collision shapes for a single tile */
typedef struct {
    COLLISION_SHAPE shapes[MAX_SHAPES_PER_TILE];
    int shape_count;
} TILE_COLLISION;

typedef struct {
	char name[64];
    float x, y, w, h;
    float u0, v0, u1, v1;
    int layer, atlas_id, has_body;
} TILE_INSTANCE;

typedef struct {
    char name[64];
    float x, y, angle, w, h;
    float u0, v0, u1, v1;
    int atlas_id, tile_id, visible, has_body;
    b2BodyId body;
    float body_offset_x;  /* hitbox offset from sprite center, world units */
    float body_offset_y;
    /* Hitbox data from Aseprite */
    int has_hitbox_data;
    float hitbox_w_ratio;
    float hitbox_h_ratio;
    float hitbox_offset_x;
    float hitbox_offset_y;
    /* Animation support */
    int anim_frames[32];    /* tile IDs for animation */
    int anim_frame_count;
    int anim_current_frame;
    float anim_fps;
    float anim_time;
    int anim_loop;          /* 1=loop, 0=once */
    int anim_playing;
} SPRITE;

#define MAX_OBJECT_PROPS 16

typedef struct {
    char name[32];
    char value[256];  /* larger for script callbacks */
    char type[16];  /* int, float, bool, string */
} TMX_PROPERTY;

typedef struct {
    char name[64], type[64];
    float x, y, width, height;
    int is_point, is_ellipse;
    TMX_PROPERTY props[MAX_OBJECT_PROPS];
    int prop_count;
} TMX_OBJECT;

typedef struct {
    GLuint texture;
    char filename[MAX_PATH_LEN];
    int width, height, tile_width, tile_height, cols, rows, firstgid;
    float tile_u, tile_v;
} ATLAS;

typedef enum {
    CAM_LOCKED,           /* Camera doesn't move */
    CAM_FIXED_SCROLL,     /* Camera moves at constant velocity (for auto-scrollers) */
    CAM_FOLLOW,           /* Camera follows sprite directly (centered) */
    CAM_FOLLOW_DEADZONE,  /* Camera follows when sprite exits deadzone */
    CAM_FOLLOW_LOOKAHEAD  /* Camera leads in direction of movement */
} CameraMode;

typedef struct {
    float x, y;                  /* current camera center in world coords */
    float target_x, target_y;    /* where camera wants to be */
    float smooth_speed;          /* interpolation rate (0=instant, higher=smoother) */
    
    CameraMode mode;
    
    /* Fixed scroll params */
    float scroll_vx, scroll_vy;  /* constant velocity (world units/sec) */
    
    /* Follow params */
    int follow_sprite;           /* sprite index to track, -1 if none */
    float deadzone_w, deadzone_h;/* agent can move this far before camera follows */
    float lookahead_x, lookahead_y; /* how far ahead to look in movement direction */
    
    /* Bounds (optional) - camera center won't go beyond these */
    float min_x, max_x;
    float min_y, max_y;
    int use_bounds;
} CAMERA;


#define MAX_SPRITE_TILESETS 8
typedef struct {
    char name[64];                              /* tileset name e.g. "PinkStar" */
    int firstgid;
    int tile_width, tile_height;
    int atlas_id;                               /* index into tm->atlases[] */
    AsepriteData aseprite;                      /* parsed animation data */
    int has_aseprite;                           /* 1 if aseprite data loaded */

    TILE_COLLISION tile_collisions[MAX_TILE_COLLISIONS];
    int tile_collision_count;
} SPRITE_TILESET;

typedef struct {
    TILE_INSTANCE tiles[MAX_TILES];
    int tile_count;
    int layer_counts[8];    /* tiles per layer for z-order rendering */
    int num_layers;
    SPRITE sprites[MAX_SPRITES];
    SPRITE_TILESET sprite_tilesets[MAX_SPRITE_TILESETS];
    int sprite_tileset_count;
    int sprite_count;
    TMX_OBJECT objects[MAX_OBJECTS];
    int object_count;
    ATLAS atlases[MAX_ATLASES];
    int atlas_count;
    CAMERA camera;              /* camera system */
    GLuint shader_program, vao, vbo, sprite_vao, sprite_vbo;
    GLint u_texture, u_modelview, u_projection;
    b2WorldId world_id;
    int has_world;
    b2Vec2 gravity;
    int substep_count;
    Tcl_HashTable body_table;
    int body_count;
    int map_width, map_height, tile_pixel_width, tile_pixel_height;
    float tile_size, pixels_per_meter;
    float offset_x, offset_y;   /* world offset for centering */
    char base_path[MAX_PATH_LEN];
    int tiles_dirty;
    int auto_center;            /* auto-center on load */
    int normalize;               /* normalize to unit square on load */
    float norm_scale;            /* scale factor used for normalization */
    char collision_callback[256]; /* Tcl script to call on collision */
    Tcl_Interp *interp;
} TILEMAP;


static int TilemapID = -1;

#ifdef STIM2_USE_GLES
static const char *tilemap_vs =
    "#version 300 es\nprecision mediump float;\n"
    "layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV;\n"
    "out vec2 vUV; uniform mat4 projMat, modelviewMat;\n"
    "void main() { gl_Position = projMat * modelviewMat * vec4(aPos,0,1); vUV = aUV; }\n";
static const char *tilemap_fs =
    "#version 300 es\nprecision mediump float;\n"
    "in vec2 vUV; out vec4 fragColor; uniform sampler2D atlas;\n"
    "void main() { vec4 c = texture(atlas, vUV); if(c.a<0.1) discard; fragColor = c; }\n";
#else
static const char *tilemap_vs =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV;\n"
    "out vec2 vUV; uniform mat4 projMat, modelviewMat;\n"
    "void main() { gl_Position = projMat * modelviewMat * vec4(aPos,0,1); vUV = aUV; }\n";
static const char *tilemap_fs =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 fragColor; uniform sampler2D atlas;\n"
    "void main() { vec4 c = texture(atlas, vUV); if(c.a<0.1) discard; fragColor = c; }\n";
#endif

/* Utilities */
static void get_directory(const char *path, char *dir, int max) {
    strncpy(dir, path, max - 1); dir[max - 1] = '\0';
    char *s = strrchr(dir, '/'), *b = strrchr(dir, '\\');
    char *last = s > b ? s : b;
    if (last) *(last + 1) = '\0'; else dir[0] = '\0';
}

static void join_path(char *dest, int max, const char *dir, const char *file) {
    if (dir[0] && file[0] != '/' && file[0] != '\\')
        snprintf(dest, max, "%s%s", dir, file);
    else { strncpy(dest, file, max - 1); dest[max - 1] = '\0'; }
}

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log);
        fprintf(stderr, "tilemap shader: %s\n", log); return 0; }
    return s;
}

static int tilemap_init_gl(TILEMAP *tm) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, tilemap_vs);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, tilemap_fs);
    if (!vs || !fs) return -1;
    tm->shader_program = glCreateProgram();
    glAttachShader(tm->shader_program, vs);
    glAttachShader(tm->shader_program, fs);
    glLinkProgram(tm->shader_program);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok; glGetProgramiv(tm->shader_program, GL_LINK_STATUS, &ok);
    if (!ok) return -1;
    tm->u_texture = glGetUniformLocation(tm->shader_program, "atlas");
    tm->u_modelview = glGetUniformLocation(tm->shader_program, "modelviewMat");
    tm->u_projection = glGetUniformLocation(tm->shader_program, "projMat");
    
    glGenVertexArrays(1, &tm->vao); glGenBuffers(1, &tm->vbo);
    glBindVertexArray(tm->vao);
    glBindBuffer(GL_ARRAY_BUFFER, tm->vbo);
    glBufferData(GL_ARRAY_BUFFER, MAX_TILES * 6 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    
    glGenVertexArrays(1, &tm->sprite_vao); glGenBuffers(1, &tm->sprite_vbo);
    glBindVertexArray(tm->sprite_vao);
    glBindBuffer(GL_ARRAY_BUFFER, tm->sprite_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glBindVertexArray(0);
    return 0;
}

static ATLAS* find_atlas_for_gid(TILEMAP *tm, int gid) {
    ATLAS *best = NULL;
    for (int i = 0; i < tm->atlas_count; i++)
        if (tm->atlases[i].firstgid <= gid)
            if (!best || tm->atlases[i].firstgid > best->firstgid)
                best = &tm->atlases[i];
    return best;
}

static void get_tile_uvs(ATLAS *a, int gid, float *u0, float *v0, float *u1, float *v1) {
    int local = gid - a->firstgid;
    if (local < 0) local = 0;
    int col = local % a->cols, row = local / a->cols;
    *u0 = col * a->tile_u; *v0 = row * a->tile_v;
    *u1 = *u0 + a->tile_u; *v1 = *v0 + a->tile_v;
}

static void rebuild_vbo(TILEMAP *tm) {
    if (tm->tile_count == 0) return;
    float *v = malloc(tm->tile_count * 6 * 4 * sizeof(float));
    int vi = 0;
    for (int i = 0; i < tm->tile_count; i++) {
        TILE_INSTANCE *t = &tm->tiles[i];
        float x0 = t->x - t->w*0.5f, y0 = t->y - t->h*0.5f;
        float x1 = t->x + t->w*0.5f, y1 = t->y + t->h*0.5f;
        v[vi++]=x0; v[vi++]=y0; v[vi++]=t->u0; v[vi++]=t->v1;
        v[vi++]=x1; v[vi++]=y0; v[vi++]=t->u1; v[vi++]=t->v1;
        v[vi++]=x1; v[vi++]=y1; v[vi++]=t->u1; v[vi++]=t->v0;
        v[vi++]=x0; v[vi++]=y0; v[vi++]=t->u0; v[vi++]=t->v1;
        v[vi++]=x1; v[vi++]=y1; v[vi++]=t->u1; v[vi++]=t->v0;
        v[vi++]=x0; v[vi++]=y1; v[vi++]=t->u0; v[vi++]=t->v0;
    }
    glBindBuffer(GL_ARRAY_BUFFER, tm->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vi * sizeof(float), v);
    free(v);
    tm->tiles_dirty = 0;
}

static void build_sprite_verts(SPRITE *sp, float *v) {
    float hw = sp->w*0.5f, hh = sp->h*0.5f;
    float c = cosf(sp->angle), s = sinf(sp->angle);
    float corners[4][2] = {{-hw,-hh},{hw,-hh},{hw,hh},{-hw,hh}};
    float r[4][2];
    for (int i = 0; i < 4; i++) {
        r[i][0] = sp->x + corners[i][0]*c - corners[i][1]*s;
        r[i][1] = sp->y + corners[i][0]*s + corners[i][1]*c;
    }
    int vi = 0;
    v[vi++]=r[0][0]; v[vi++]=r[0][1]; v[vi++]=sp->u0; v[vi++]=sp->v1;
    v[vi++]=r[1][0]; v[vi++]=r[1][1]; v[vi++]=sp->u1; v[vi++]=sp->v1;
    v[vi++]=r[2][0]; v[vi++]=r[2][1]; v[vi++]=sp->u1; v[vi++]=sp->v0;
    v[vi++]=r[0][0]; v[vi++]=r[0][1]; v[vi++]=sp->u0; v[vi++]=sp->v1;
    v[vi++]=r[2][0]; v[vi++]=r[2][1]; v[vi++]=sp->u1; v[vi++]=sp->v0;
    v[vi++]=r[3][0]; v[vi++]=r[3][1]; v[vi++]=sp->u0; v[vi++]=sp->v0;
}

static void camera_init(CAMERA *cam) {
    cam->x = 0;
    cam->y = 0;
    cam->target_x = 0;
    cam->target_y = 0;
    cam->smooth_speed = 0;  /* instant by default */
    cam->mode = CAM_LOCKED;
    cam->scroll_vx = 0;
    cam->scroll_vy = 0;
    cam->follow_sprite = -1;
    cam->deadzone_w = 2.0f;
    cam->deadzone_h = 1.5f;
    cam->lookahead_x = 2.0f;
    cam->lookahead_y = 1.0f;
    cam->min_x = 0;
    cam->max_x = 0;
    cam->min_y = 0;
    cam->max_y = 0;
    cam->use_bounds = 0;
}

static void camera_update(TILEMAP *tm, float dt) {
    CAMERA *cam = &tm->camera;
    
    switch (cam->mode) {
        case CAM_LOCKED:
            /* target stays where it is */
            break;
            
        case CAM_FIXED_SCROLL:
            cam->target_x += cam->scroll_vx * dt;
            cam->target_y += cam->scroll_vy * dt;
            break;
            
        case CAM_FOLLOW:
            if (cam->follow_sprite >= 0 && cam->follow_sprite < tm->sprite_count) {
                SPRITE *sp = &tm->sprites[cam->follow_sprite];
                cam->target_x = sp->x;
                cam->target_y = sp->y;
            }
            break;
            
        case CAM_FOLLOW_DEADZONE:
            if (cam->follow_sprite >= 0 && cam->follow_sprite < tm->sprite_count) {
                SPRITE *sp = &tm->sprites[cam->follow_sprite];
                /* Only update target if sprite outside deadzone */
                float dx = sp->x - cam->target_x;
                float dy = sp->y - cam->target_y;
                float hw = cam->deadzone_w * 0.5f;
                float hh = cam->deadzone_h * 0.5f;
                if (dx > hw)
                    cam->target_x = sp->x - hw;
                else if (dx < -hw)
                    cam->target_x = sp->x + hw;
                if (dy > hh)
                    cam->target_y = sp->y - hh;
                else if (dy < -hh)
                    cam->target_y = sp->y + hh;
            }
            break;
            
        case CAM_FOLLOW_LOOKAHEAD:
            if (cam->follow_sprite >= 0 && cam->follow_sprite < tm->sprite_count) {
                SPRITE *sp = &tm->sprites[cam->follow_sprite];
                /* Get velocity direction for lookahead */
                float look_offset_x = 0, look_offset_y = 0;
                if (sp->has_body && b2Body_IsValid(sp->body)) {
                    b2Vec2 vel = b2Body_GetLinearVelocity(sp->body);
                    /* Gradual lookahead based on velocity magnitude */
                    if (vel.x > 0.5f) look_offset_x = cam->lookahead_x;
                    else if (vel.x < -0.5f) look_offset_x = -cam->lookahead_x;
                    if (vel.y > 0.5f) look_offset_y = cam->lookahead_y;
                    else if (vel.y < -0.5f) look_offset_y = -cam->lookahead_y;
                }
                cam->target_x = sp->x + look_offset_x;
                cam->target_y = sp->y + look_offset_y;
            }
            break;
    }
    
    /* Clamp to bounds */
    if (cam->use_bounds) {
        if (cam->target_x < cam->min_x) cam->target_x = cam->min_x;
        if (cam->target_x > cam->max_x) cam->target_x = cam->max_x;
        if (cam->target_y < cam->min_y) cam->target_y = cam->min_y;
        if (cam->target_y > cam->max_y) cam->target_y = cam->max_y;
    }
    
    /* Smooth interpolation toward target */
    if (cam->smooth_speed <= 0) {
        cam->x = cam->target_x;
        cam->y = cam->target_y;
    } else {
        /* Exponential smoothing - feels natural */
        float t = 1.0f - expf(-cam->smooth_speed * dt);
        cam->x += (cam->target_x - cam->x) * t;
        cam->y += (cam->target_y - cam->y) * t;
    }
}

static void tilemap_draw(GR_OBJ *obj) {
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(obj);
    if (tm->tile_count == 0 && tm->sprite_count == 0) return;
    if (tm->tiles_dirty) rebuild_vbo(tm);
    
    float mv[16], pr[16];
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(tm->shader_program);
    stimGetMatrix(STIM_MODELVIEW_MATRIX, mv);
    stimGetMatrix(STIM_PROJECTION_MATRIX, pr);
    
    /* Apply camera offset to modelview matrix */
    mv[12] -= tm->camera.x;
    mv[13] -= tm->camera.y;
    
    glUniformMatrix4fv(tm->u_modelview, 1, GL_FALSE, mv);
    glUniformMatrix4fv(tm->u_projection, 1, GL_FALSE, pr);
    
    if (tm->tile_count > 0 && tm->atlas_count > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tm->atlases[0].texture);
        glUniform1i(tm->u_texture, 0);
        glBindVertexArray(tm->vao);
        glDrawArrays(GL_TRIANGLES, 0, tm->tile_count * 6);
    }
    
    if (tm->sprite_count > 0) {
        float sv[24];
        glBindVertexArray(tm->sprite_vao);
        for (int i = 0; i < tm->sprite_count; i++) {
            SPRITE *sp = &tm->sprites[i];
            // NOTE: originally we just continue'd here if not visible
            //       but this actually slowed down our render loop so we
            //       do the next binds and then just skip the DrawArrays call
            //if (!sp->visible) continue;
            if (sp->atlas_id >= 0 && sp->atlas_id < tm->atlas_count)
                glBindTexture(GL_TEXTURE_2D, tm->atlases[sp->atlas_id].texture);
            build_sprite_verts(sp, sv);
            glBindBuffer(GL_ARRAY_BUFFER, tm->sprite_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(sv), sv);
            if (sp->visible) glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }
    glBindVertexArray(0);
    glUseProgram(0);
}

static const char* find_name_from_body(TILEMAP *tm, b2BodyId bodyId) {
    if (bodyId.index1 == 0) return "invalid"; // index1 is 1-based in Box2D v3

    /* 1. Check Sprites (The Player, NPCs, etc) */
    for (int i = 0; i < tm->sprite_count; i++) {
        if (tm->sprites[i].has_body) {
            b2BodyId spriteBody = tm->sprites[i].body;
            if (spriteBody.index1 == bodyId.index1 && 
                spriteBody.generation == bodyId.generation) {
                return tm->sprites[i].name;
            }
        }
    }

    /* 2. Check Static Tile/Object Table */
    Tcl_HashEntry *e;
    Tcl_HashSearch s;
    for (e = Tcl_FirstHashEntry(&tm->body_table, &s); e; e = Tcl_NextHashEntry(&s)) {
        b2BodyId *stored = (b2BodyId *)Tcl_GetHashValue(e);
        if (stored->index1 == bodyId.index1 && 
            stored->generation == bodyId.generation) {
            return Tcl_GetHashKey(&tm->body_table, e);
        }
    }

    return "unknown";
}

static void tilemap_update(GR_OBJ *obj) {
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(obj);
    if (!tm->has_world) return;
    float dt = getFrameDuration() / 1000.0f;
    if (dt > 0.1f) dt = 0.016f;

    /* Update camera */
    camera_update(tm, dt);

    b2World_Step(tm->world_id, dt, tm->substep_count);
    
    /* Update sprites from physics and handle animation */
    for (int i = 0; i < tm->sprite_count; i++) {
        SPRITE *sp = &tm->sprites[i];  
        
        if (sp->has_body && b2Body_IsValid(sp->body)) {
            b2Vec2 pos = b2Body_GetPosition(sp->body);
            sp->x = pos.x - sp->body_offset_x;
            sp->y = pos.y - sp->body_offset_y;
            sp->angle = b2Rot_GetAngle(b2Body_GetRotation(sp->body));
        }
        
        /* Update animation */
        if (sp->anim_playing && sp->anim_frame_count > 0) {
            sp->anim_time += dt;
            float frame_duration = 1.0f / sp->anim_fps;
            if (sp->anim_time >= frame_duration) {
                sp->anim_time -= frame_duration;
                sp->anim_current_frame++;
                if (sp->anim_current_frame >= sp->anim_frame_count) {
                    if (sp->anim_loop) {
                        sp->anim_current_frame = 0;
                    } else {
                        sp->anim_current_frame = sp->anim_frame_count - 1;
                        sp->anim_playing = 0;
                    }
                }
                /* Update tile UVs */
                sp->tile_id = sp->anim_frames[sp->anim_current_frame];
                if (sp->atlas_id < tm->atlas_count) {
                    ATLAS *a = &tm->atlases[sp->atlas_id];
                    get_tile_uvs(a, sp->tile_id,
                                 &sp->u0, &sp->v0, &sp->u1, &sp->v1);
                }
            }
        }
    }
    
    /* Process collision callbacks */
    if (tm->collision_callback[0] != '\0') {
        
        // --- 1. Process Regular Contact Events ---
        b2ContactEvents ev = b2World_GetContactEvents(tm->world_id);
        for (int i = 0; i < ev.beginCount; i++) {
            // Box2D v3.1: Get names directly from Shape UserData
            const char *nameA = (const char*)b2Shape_GetUserData(ev.beginEvents[i].shapeIdA);
            const char *nameB = (const char*)b2Shape_GetUserData(ev.beginEvents[i].shapeIdB);
            
            // Fallback to searching if UserData wasn't set (e.g., dynamic bodies without names)
            if (!nameA) nameA = find_name_from_body(tm, b2Shape_GetBody(ev.beginEvents[i].shapeIdA));
            if (!nameB) nameB = find_name_from_body(tm, b2Shape_GetBody(ev.beginEvents[i].shapeIdB));
            
            char script[512];
            snprintf(script, sizeof(script), "%s {%s} {%s}",
                     tm->collision_callback, nameA, nameB);
			int result = Tcl_Eval(tm->interp, script);
			if (result != TCL_OK) {
				fprintf(stderr, "Collision callback error: %s\n", 
						Tcl_GetStringResult(tm->interp));
				// Optionally disable callback to prevent error spam
				// tm->collision_callback[0] = '\0';
			}
        }
        
        // --- 2. Process Sensor Events (Triggers/Goals) ---
        b2SensorEvents sev = b2World_GetSensorEvents(tm->world_id);
        for (int i = 0; i < sev.beginCount; i++) {
            // The sensorShapeId is the 'goal'/'trigger' object
            // The visitorShapeId is usually the player or an NPC
            const char *sensorName = (const char*)b2Shape_GetUserData(sev.beginEvents[i].sensorShapeId);
            const char *visitorName = (const char*)b2Shape_GetUserData(sev.beginEvents[i].visitorShapeId);
            
            // If visitor is NULL (common if the player is a sprite with a generic name), find it
            if (!visitorName) {
                visitorName = find_name_from_body(tm, b2Shape_GetBody(sev.beginEvents[i].visitorShapeId));
            }
            
            // Ensure we have valid names before calling Tcl
            if (sensorName && visitorName) {
                char script[512];
                snprintf(script, sizeof(script), "%s {%s} {%s}",
                         tm->collision_callback, visitorName, sensorName);
			   int result = Tcl_Eval(tm->interp, script);
				if (result != TCL_OK) {
					fprintf(stderr, "Sensor callback error: %s\n", 
							Tcl_GetStringResult(tm->interp));
					// Optionally disable callback to prevent error spam
					// tm->collision_callback[0] = '\0';
				}
			}
        }
    }
}

static void tilemap_delete(GR_OBJ *obj) {
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(obj);
    if (tm->vao) glDeleteVertexArrays(1, &tm->vao);
    if (tm->vbo) glDeleteBuffers(1, &tm->vbo);
    if (tm->sprite_vao) glDeleteVertexArrays(1, &tm->sprite_vao);
    if (tm->sprite_vbo) glDeleteBuffers(1, &tm->sprite_vbo);
    if (tm->shader_program) glDeleteProgram(tm->shader_program);
    for (int i = 0; i < tm->atlas_count; i++)
        if (tm->atlases[i].texture) glDeleteTextures(1, &tm->atlases[i].texture);
    if (tm->has_world) b2DestroyWorld(tm->world_id);
    Tcl_HashEntry *e; Tcl_HashSearch s;
    for (e = Tcl_FirstHashEntry(&tm->body_table, &s); e; e = Tcl_NextHashEntry(&s))
        free(Tcl_GetHashValue(e));
    Tcl_DeleteHashTable(&tm->body_table);
    free(tm);
}

static int tilemap_reset(GR_OBJ *obj) { return TCL_OK; }

static int load_atlas(TILEMAP *tm, const char *file, int tw, int th, int firstgid) {
    if (tm->atlas_count >= MAX_ATLASES) return -1;
    char path[MAX_PATH_LEN];
    join_path(path, MAX_PATH_LEN, tm->base_path, file);
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) { fprintf(stderr, "tilemap: can't load %s\n", path); return -1; }
    ATLAS *a = &tm->atlases[tm->atlas_count];
    strncpy(a->filename, file, MAX_PATH_LEN - 1);
    a->width = w; a->height = h;
    a->tile_width = tw; a->tile_height = th;
    a->cols = w / tw; a->rows = h / th;
    a->tile_u = (float)tw / w; a->tile_v = (float)th / h;
    a->firstgid = firstgid;
    glGenTextures(1, &a->texture);
    glBindTexture(GL_TEXTURE_2D, a->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return tm->atlas_count++;
}

static int* parse_csv(const char *csv, int w, int h) {
    if (!csv) return NULL;
    int *tiles = calloc(w * h, sizeof(int));
    char *copy = strdup(csv), *p = copy;
    int idx = 0, max = w * h;
    while (*p && idx < max) {
        while (*p && (*p==' '||*p=='\n'||*p=='\r'||*p=='\t')) p++;
        if (*p >= '0' && *p <= '9') tiles[idx++] = atoi(p);
        while (*p && *p != ',' && *p != '\n' && *p != '\r') p++;
        if (*p == ',') p++;
    }
    free(copy);
    return tiles;
}

/* Callback context for point query */
typedef struct {
    int hit;
    b2BodyId ignore_body;  /* optional body to ignore (e.g., the player) */
    int use_ignore;
} PointQueryContext;

/* Box2D query callback - called for each overlapping shape */
static bool point_query_callback(b2ShapeId shapeId, void *context) {
    PointQueryContext *ctx = (PointQueryContext *)context;
    
    /* Optionally ignore a specific body (like the player itself) */
    if (ctx->use_ignore) {
        b2BodyId body = b2Shape_GetBody(shapeId);
        if (body.index1 == ctx->ignore_body.index1) {
            return true;  /* continue searching */
        }
    }
    
    /* Found a hit */
    ctx->hit = 1;
    return false;  /* stop searching */
}

/* ============================================================
 * Helper function to find sprite tileset by name
 * ============================================================ */

static SPRITE_TILESET* find_sprite_tileset(TILEMAP *tm, const char *name) {
    if (!tm || !name) return NULL;
    for (int i = 0; i < tm->sprite_tileset_count; i++) {
        if (strcmp(tm->sprite_tilesets[i].name, name) == 0) {
            return &tm->sprite_tilesets[i];
        }
    }
    return NULL;
}

static SPRITE_TILESET* find_sprite_tileset_by_firstgid(TILEMAP *tm, int gid) {
    SPRITE_TILESET *best = NULL;
    for (int i = 0; i < tm->sprite_tileset_count; i++) {
        if (tm->sprite_tilesets[i].firstgid <= gid) {
            if (!best || tm->sprite_tilesets[i].firstgid > best->firstgid) {
                best = &tm->sprite_tilesets[i];
            }
        }
    }
    return best;
}

/*
 * Parse polygon points string "x1,y1 x2,y2 x3,y3 ..."
 * obj_x, obj_y: object position within tile (pixels)
 * Returns number of vertices parsed
 */
static int parse_polygon_points(const char *points_str, 
                                float obj_x, float obj_y,
                                int tile_w, int tile_h,
                                float *out_x, float *out_y, 
                                int max_verts)
{
    if (!points_str) return 0;
    
    int count = 0;
    const char *p = points_str;
    
    while (*p && count < max_verts) {
        float x, y;
        char *end;
        
        /* Parse x */
        x = strtof(p, &end);
        if (end == p) break;
        p = end;
        
        if (*p == ',') p++;
        
        /* Parse y */
        y = strtof(p, &end);
        if (end == p) break;
        p = end;
        
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        
        /* Normalize to 0.0-1.0 range */
        out_x[count] = (obj_x + x) / tile_w;
        out_y[count] = (obj_y + y) / tile_h;
        count++;
    }
    
    return count;
}

/*
 * Load all collision shapes for all tiles in a tileset
 */
static void load_tile_collisions(void *tileset_xml, SPRITE_TILESET *sts)
{
    sts->tile_collision_count = 0;
    
    /* Initialize all tiles to no collision */
    for (int i = 0; i < MAX_TILE_COLLISIONS; i++) {
        sts->tile_collisions[i].shape_count = 0;
    }
    
    /* Iterate through <tile> elements */
    for (void *tile = tmx_xml_tileset_first_tile(tileset_xml);
         tile != NULL;
         tile = tmx_xml_tileset_next_tile(tile)) {
        
        int tile_id = tmx_xml_tile_get_id(tile);
        if (tile_id < 0 || tile_id >= MAX_TILE_COLLISIONS) continue;
        
        void *objgroup = tmx_xml_tile_get_objectgroup(tile);
        if (!objgroup) continue;
        
        TILE_COLLISION *tc = &sts->tile_collisions[tile_id];
        tc->shape_count = 0;
        
        /* Iterate through all collision objects in this tile */
        for (void *obj = tmx_xml_first_object(objgroup);
             obj != NULL && tc->shape_count < MAX_SHAPES_PER_TILE;
             obj = tmx_xml_next_object(obj)) {
            
            COLLISION_SHAPE *shape = &tc->shapes[tc->shape_count];
            
            float obj_x = tmx_xml_object_get_float(obj, "x", 0);
            float obj_y = tmx_xml_object_get_float(obj, "y", 0);
            
            if (tmx_xml_object_has_polygon(obj)) {
                /* Polygon shape */
                const char *points = tmx_xml_object_get_polygon_points(obj);
                shape->vert_count = parse_polygon_points(points, obj_x, obj_y,
                                                         sts->tile_width, sts->tile_height,
                                                         shape->verts_x, shape->verts_y,
                                                         MAX_COLLISION_VERTS);
                if (shape->vert_count >= 3) {
                    shape->type = SHAPE_POLYGON;
                    tc->shape_count++;
                }
            } else {
                /* Rectangle shape */
                float w = tmx_xml_object_get_float(obj, "width", sts->tile_width);
                float h = tmx_xml_object_get_float(obj, "height", sts->tile_height);
                
                shape->type = SHAPE_BOX;
                shape->box_x = obj_x / sts->tile_width;
                shape->box_y = obj_y / sts->tile_height;
                shape->box_w = w / sts->tile_width;
                shape->box_h = h / sts->tile_height;
                tc->shape_count++;
            }
        }
        
        if (tc->shape_count > 0) {
            sts->tile_collision_count++;
        }
    }
    
    if (sts->tile_collision_count > 0) {
        fprintf(stderr, "tilemap: loaded collision shapes for %d tiles in '%s'\n",
                sts->tile_collision_count, sts->name);
    }
}

/*
 * Look up collision data for a GID
 * Returns pointer to TILE_COLLISION or NULL if no custom collision
 */
static TILE_COLLISION* get_tile_collision(TILEMAP *tm, int gid)
{
    /* Find which tileset this GID belongs to */
    SPRITE_TILESET *best_sts = NULL;
    for (int i = 0; i < tm->sprite_tileset_count; i++) {
        SPRITE_TILESET *sts = &tm->sprite_tilesets[i];
        if (sts->firstgid <= gid) {
            if (!best_sts || sts->firstgid > best_sts->firstgid) {
                best_sts = sts;
            }
        }
    }
    
    if (!best_sts) return NULL;
    
    int local_id = gid - best_sts->firstgid;
    if (local_id < 0 || local_id >= MAX_TILE_COLLISIONS) return NULL;
    
    TILE_COLLISION *tc = &best_sts->tile_collisions[local_id];
    if (tc->shape_count == 0) return NULL;
    
    return tc;
}

/*============================================================================
 * COLLISION SHAPE CREATION
 *============================================================================*/

/*
 * Create all collision shapes for a tile on a body
 * Returns number of shapes created
 */
static int create_tile_collision_shapes(TILEMAP *tm, b2BodyId body,
                                        float tile_w, float tile_h,
                                        int gid, const char *name)
{
    TILE_COLLISION *tc = get_tile_collision(tm, gid);
    
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = 1.0f;
    sd.userData = (void *)name;
    
    if (!tc) {
        /* No custom collision - create default full-tile box */
        b2Polygon box = b2MakeBox(tile_w * 0.5f, tile_h * 0.5f);
        b2ShapeId shape = b2CreatePolygonShape(body, &sd, &box);
        b2Shape_SetFriction(shape, 0.3f);
        return 1;
    }
    
    /* Create each shape defined for this tile */
    int created = 0;
    for (int i = 0; i < tc->shape_count; i++) {
        COLLISION_SHAPE *cs = &tc->shapes[i];
        b2ShapeId shape;
        
        if (cs->type == SHAPE_POLYGON) {
            b2Vec2 points[MAX_COLLISION_VERTS];
            for (int v = 0; v < cs->vert_count; v++) {
                float nx = cs->verts_x[v] - 0.5f;
                float ny = 0.5f - cs->verts_y[v];
                points[v].x = nx * tile_w;
                points[v].y = ny * tile_h;
            }
            
            b2Hull hull = b2ComputeHull(points, cs->vert_count);
            b2Polygon poly = b2MakePolygon(&hull, 0.0f);
            shape = b2CreatePolygonShape(body, &sd, &poly);
            
        } else if (cs->type == SHAPE_BOX) {
            float cx = (cs->box_x + cs->box_w * 0.5f - 0.5f) * tile_w;
            float cy = (0.5f - (cs->box_y + cs->box_h * 0.5f)) * tile_h;
            float hw = cs->box_w * tile_w * 0.5f;
            float hh = cs->box_h * tile_h * 0.5f;
            
            b2Polygon box = b2MakeOffsetBox(hw, hh, (b2Vec2){cx, cy}, b2Rot_identity);
            shape = b2CreatePolygonShape(body, &sd, &box);
            
        } else {
            continue;
        }
        
        b2Shape_SetFriction(shape, 0.3f);
        b2Shape_SetRestitution(shape, 0.0f);
        created++;
    }
    
    return created;
}

/*========================================================================
 * Tcl Commands
 *========================================================================*/

static int tilemapCreateCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    GR_OBJ *obj = gobjCreateObj();
    if (!obj) { Tcl_SetResult(interp, "create failed", TCL_STATIC); return TCL_ERROR; }
    GR_OBJTYPE(obj) = TilemapID;
    strcpy(GR_NAME(obj), "Tilemap");
    TILEMAP *tm = (TILEMAP *)calloc(1, sizeof(TILEMAP));
    tm->interp = interp;
    tm->sprite_tileset_count = 0;
    tm->tile_size = 1.0f;
    tm->pixels_per_meter = 32.0f;
    tm->gravity = (b2Vec2){0, -10};
    tm->substep_count = 4;
    tm->auto_center = 1;  /* default to auto-center */
    tm->collision_callback[0] = '\0';
    Tcl_InitHashTable(&tm->body_table, TCL_STRING_KEYS);
    camera_init(&tm->camera);
    if (tilemap_init_gl(tm) < 0) { free(tm); return TCL_ERROR; }
    GR_CLIENTDATA(obj) = tm;
    GR_ACTIONFUNCP(obj) = tilemap_draw;
    GR_UPDATEFUNCP(obj) = (UPDATE_FUNC)tilemap_update;
    GR_DELETEFUNCP(obj) = tilemap_delete;
    GR_RESETFUNCP(obj) = (RESET_FUNC)tilemap_reset;
    Tcl_SetObjResult(interp, Tcl_NewIntObj(gobjAddObj(olist, obj)));
    return TCL_OK;
}

static int *decode_base64_tiles(const char *text, int width, int height) {
    /* Skip whitespace and get clean base64 string */
    size_t len = strlen(text);
    char *clean = malloc(len + 1);
    size_t clean_len = 0;
    for (size_t i = 0; i < len; i++) {
        if (!isspace(text[i])) clean[clean_len++] = text[i];
    }
    clean[clean_len] = '\0';
    
    /* Decode base64 - each tile is 4 bytes (little-endian uint32) */
    size_t decoded_size = (clean_len * 3) / 4;
    unsigned char *decoded = malloc(decoded_size);
    
    static const int b64_table[256] = {
        ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
        ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
        ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
        ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
        ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
        ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
        ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
        ['4']=56,['5']=57,['6']=58,['7']=59,['+']=60,['/']=61
    };
    
    size_t j = 0;
    for (size_t i = 0; i < clean_len; i += 4) {
        uint32_t n = (b64_table[(int)clean[i]] << 18) |
                     (b64_table[(int)clean[i+1]] << 12) |
                     (b64_table[(int)clean[i+2]] << 6) |
                      b64_table[(int)clean[i+3]];
        if (j < decoded_size) decoded[j++] = (n >> 16) & 0xFF;
        if (j < decoded_size && clean[i+2] != '=') decoded[j++] = (n >> 8) & 0xFF;
        if (j < decoded_size && clean[i+3] != '=') decoded[j++] = n & 0xFF;
    }
    free(clean);
    
    /* Convert to tile array (little-endian uint32) */
    int *tiles = malloc(width * height * sizeof(int));
    for (int i = 0; i < width * height; i++) {
        tiles[i] = decoded[i*4] | (decoded[i*4+1] << 8) | 
                   (decoded[i*4+2] << 16) | (decoded[i*4+3] << 24);
    }
    free(decoded);
    return tiles;
}

static int tilemapLoadTMXCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
            " tilemap filename ?-pixels_per_meter N? ?-collision_layer NAME?", NULL);
        return TCL_ERROR;
    }
    int id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) {
        Tcl_AppendResult(interp, "invalid tilemap", NULL);
        return TCL_ERROR;
    }
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    float ppm = 32.0f;
    const char *collision_layer = "Collision";
    int normalize = 0;
    float load_scale = 1.0f;
    
    for (int i = 3; i < argc - 1; i += 2) {
        if (strcmp(argv[i], "-pixels_per_meter") == 0) {
            double d; Tcl_GetDouble(interp, argv[i+1], &d); ppm = (float)d;
        } else if (strcmp(argv[i], "-collision_layer") == 0) {
            collision_layer = argv[i+1];
        } else if (strcmp(argv[i], "-normalize") == 0) {
            int n; Tcl_GetInt(interp, argv[i+1], &n); normalize = n;
        } else if (strcmp(argv[i], "-scale") == 0) {
            double d; Tcl_GetDouble(interp, argv[i+1], &d); load_scale = (float)d;
        }
    }
    tm->pixels_per_meter = ppm;
    tm->normalize = normalize;
    get_directory(argv[2], tm->base_path, MAX_PATH_LEN);
    tmx_xml_set_base_path(tm->base_path);

    void *doc = tmx_xml_load(argv[2]);
    if (!doc) { Tcl_AppendResult(interp, "can't load ", argv[2], NULL); return TCL_ERROR; }
    void *map = tmx_xml_get_map(doc);
    if (!map) { tmx_xml_free(doc); Tcl_AppendResult(interp, "no map element", NULL); return TCL_ERROR; }
    
    tm->map_width = tmx_xml_map_get_int(map, "width");
    tm->map_height = tmx_xml_map_get_int(map, "height");
    tm->tile_pixel_width = tmx_xml_map_get_int(map, "tilewidth");
    tm->tile_pixel_height = tmx_xml_map_get_int(map, "tileheight");
    tm->tile_size = tm->tile_pixel_width / ppm;
    
    /* Compute normalization scale factor if requested */
    float norm_scale = 1.0f;
    float world_w = tm->map_width * tm->tile_size;
    float world_h = tm->map_height * tm->tile_size;
    if (normalize) {
        norm_scale = load_scale / world_w;  /* normalize width to load_scale (e.g., degrees) */
        tm->norm_scale = norm_scale;
    }
    
    /* Create physics world */
    if (!tm->has_world) {
        b2WorldDef wd = b2DefaultWorldDef();
        wd.gravity = tm->gravity;
        tm->world_id = b2CreateWorld(&wd);
        tm->has_world = 1;
    }
    
    /* Load tilesets */
    for (void *ts = tmx_xml_first_tileset(map); ts; ts = tmx_xml_next_tileset(ts)) {
        int firstgid = tmx_xml_tileset_get_int(ts, "firstgid");
        int tw = tmx_xml_tileset_get_int(ts, "tilewidth");
        int th = tmx_xml_tileset_get_int(ts, "tileheight");
        const char *name = tmx_xml_tileset_get_name(ts);
        const char *src = tmx_xml_tileset_get_string(ts, "source");
        const char *aseprite_json = tmx_xml_tileset_get_property(ts, "aseprite_json");
        
        int atlas_id = -1;
        if (src) {
            atlas_id = load_atlas(tm, src, tw, th, firstgid);
            if (atlas_id < 0) {
                fprintf(stderr, "tilemap: failed to load atlas '%s'\n", src);
            }
        }
        
        // Register as sprite tileset if it has a name
        if (name && tm->sprite_tileset_count < MAX_SPRITE_TILESETS) {
            SPRITE_TILESET *sts = &tm->sprite_tilesets[tm->sprite_tileset_count];
            strncpy(sts->name, name, 63);
            sts->name[63] = '\0';
            sts->firstgid = firstgid;
            sts->tile_width = tw;
            sts->tile_height = th;
            sts->atlas_id = atlas_id;
            sts->has_aseprite = 0;
            sts->tile_collision_count = 0; 
            
            /* Load tile collision shapes - ADD THIS */
            load_tile_collisions(ts, sts);

            // Load Aseprite JSON if specified
            if (aseprite_json) {
                char json_path[MAX_PATH_LEN];
                join_path(json_path, MAX_PATH_LEN, tm->base_path, aseprite_json);
                if (aseprite_load(json_path, firstgid, &sts->aseprite) == 0) {
                    sts->has_aseprite = 1;
                    fprintf(stderr, "tilemap: loaded %d animations from '%s'\n", 
                            sts->aseprite.animation_count, aseprite_json);
                }
            }
            
            tm->sprite_tileset_count++;
        }
    }
    
    /* Process tile layers */
    for (void *layer = tmx_xml_first_layer(map); layer; layer = tmx_xml_next_layer(layer)) {
        const char *name = tmx_xml_layer_get_name(layer);
        int is_collision = (name && strcmp(name, collision_layer) == 0);
        int lw = tmx_xml_layer_get_int(layer, "width");
        int lh = tmx_xml_layer_get_int(layer, "height");
        void *data = tmx_xml_layer_get_data(layer);
        if (!data) continue;
        const char *enc = tmx_xml_data_get_encoding(data);

        int *tiles = NULL;
        if (strcmp(enc, "csv") == 0) {
            tiles = parse_csv(tmx_xml_data_get_text(data), lw, lh);
        } else if (strcmp(enc, "base64") == 0) {
            const char *comp = tmx_xml_data_get_compression(data);
            if (comp) {
                fprintf(stderr, "tilemap: base64+%s compression not supported\n", comp);
                continue;
            }
            tiles = decode_base64_tiles(tmx_xml_data_get_text(data), lw, lh);
        }
        if (!tiles) continue;
        
        for (int ty = 0; ty < lh; ty++) {
            for (int tx = 0; tx < lw; tx++) {
                int gid = tiles[ty * lw + tx];
                if (gid == 0 || tm->tile_count >= MAX_TILES) continue;
                ATLAS *atlas = find_atlas_for_gid(tm, gid);
                if (!atlas) continue;
                
                /* Compute tile position in world coords */
                float px = (tx + 0.5f) * tm->tile_pixel_width;
                float py = (ty + 0.5f) * tm->tile_pixel_height;
                float tile_x = px / ppm;
                float tile_y = (tm->map_height * tm->tile_pixel_height - py) / ppm;
                float tile_w = tm->tile_size;
                float tile_h = tm->tile_size;
                
                /* Apply normalization: center then scale */
                if (normalize) {
                    tile_x = (tile_x - world_w * 0.5f) * norm_scale;
                    tile_y = (tile_y - world_h * 0.5f) * norm_scale;
                    tile_w *= norm_scale;
                    tile_h *= norm_scale;
                }
                
                /* Create visual tile */
                TILE_INSTANCE *t = &tm->tiles[tm->tile_count++];
                t->x = tile_x;
                t->y = tile_y;
                t->w = tile_w;
                t->h = tile_h;
                t->atlas_id = (int)(atlas - tm->atlases);
                get_tile_uvs(atlas, gid, &t->u0, &t->v0, &t->u1, &t->v1);
                t->has_body = 0;
                
                /* Create collision bodies */
                if (is_collision) {
                    TILE_COLLISION *tc_debug = get_tile_collision(tm, gid);

                    int has_custom = (get_tile_collision(tm, gid) != NULL);
                    
                    if (has_custom) {
                        /* Custom collision - create individual body with all shapes */
                        snprintf(t->name, sizeof(t->name), "tile_%d_%d", tx, ty);
                        
                        b2BodyDef bd = b2DefaultBodyDef();
                        bd.type = b2_staticBody;
                        bd.position = (b2Vec2){tile_x, tile_y};
                        b2BodyId body = b2CreateBody(tm->world_id, &bd);
                        
                        create_tile_collision_shapes(tm, body, tile_w, tile_h, gid, t->name);
                        t->has_body = 1;
                        
                        /* Store in body table */
                        int newentry;
                        Tcl_HashEntry *e = Tcl_CreateHashEntry(&tm->body_table, t->name, &newentry);
                        b2BodyId *stored = malloc(sizeof(b2BodyId));
                        *stored = body;
                        Tcl_SetHashValue(e, stored);
                        tm->body_count++;                        
                    } else {
                        /* Default collision - use run-length optimization */
                        /* A run starts if previous tile is empty OR has custom collision */
                        int prev_gid = (tx > 0) ? tiles[ty * lw + tx - 1] : 0;
                        int prev_has_custom = (prev_gid != 0 && get_tile_collision(tm, prev_gid) != NULL);
                        int is_run_start = (tx == 0 || prev_gid == 0 || prev_has_custom);
                        
                        if (is_run_start) {
                            /* Count run length - only include tiles without custom collision */
                            int run_length = 1;
                            while (tx + run_length < lw) {
                                int next_gid = tiles[ty * lw + tx + run_length];
                                if (next_gid == 0) break;
                                if (get_tile_collision(tm, next_gid) != NULL) break;
                                run_length++;
                            }
                            
                            snprintf(t->name, sizeof(t->name), "tile_%d_%d", tx, ty);
                            
                            /* Compute body center and size in world coords */
                            float center_tile_x = tx + (run_length - 1) * 0.5f;
                            float center_px = (center_tile_x + 0.5f) * tm->tile_pixel_width;
                            float body_x = center_px / ppm;
                            float body_y = (tm->map_height * tm->tile_pixel_height -
                                           (ty + 0.5f) * tm->tile_pixel_height) / ppm;
                            float body_hw = (run_length * tm->tile_size) * 0.5f;
                            float body_hh = tm->tile_size * 0.5f;
                            
                            /* Apply normalization */
                            if (normalize) {
                                body_x = (body_x - world_w * 0.5f) * norm_scale;
                                body_y = (body_y - world_h * 0.5f) * norm_scale;
                                body_hw *= norm_scale;
                                body_hh *= norm_scale;
                            }
                            
                            b2BodyDef bd = b2DefaultBodyDef();
                            bd.type = b2_staticBody;
                            bd.position = (b2Vec2){body_x, body_y};
                            b2BodyId body = b2CreateBody(tm->world_id, &bd);
                            
                            b2Polygon box = b2MakeBox(body_hw, body_hh);
                            b2ShapeDef sd = b2DefaultShapeDef();
                            sd.density = 1.0f;
                            sd.userData = (void *)t->name;
                            b2ShapeId shape = b2CreatePolygonShape(body, &sd, &box);
                            b2Shape_SetFriction(shape, 0.3f);
                            t->has_body = 1;
                            
                            int newentry;
                            Tcl_HashEntry *e = Tcl_CreateHashEntry(&tm->body_table,
                                                                    t->name, &newentry);
                            b2BodyId *stored = malloc(sizeof(b2BodyId));
                            *stored = body;
                            Tcl_SetHashValue(e, stored);
                            tm->body_count++;
                        }
                    }
                }            
            }
        }
        free(tiles);
    }
    
    /* Process object layers */
    for (void *og = tmx_xml_first_objectgroup(map); og; og = tmx_xml_next_objectgroup(og)) {
        for (void *obj = tmx_xml_first_object(og); obj; obj = tmx_xml_next_object(obj)) {
            if (tm->object_count >= MAX_OBJECTS) break;
            TMX_OBJECT *to = &tm->objects[tm->object_count++];
            const char *n = tmx_xml_object_get_string(obj, "name");
            const char *t = tmx_xml_object_get_string(obj, "type");
            if (!t || strlen(t) == 0) {
                t = tmx_xml_object_get_string(obj, "class");
            }
            strncpy(to->name, n ? n : "", 63);
            strncpy(to->type, t ? t : "", 63);
            float px = tmx_xml_object_get_float(obj, "x", 0);
            float py = tmx_xml_object_get_float(obj, "y", 0);
            float ow = tmx_xml_object_get_float(obj, "width", 0);
            float oh = tmx_xml_object_get_float(obj, "height", 0);
            
            /* Convert to world coords */
            float obj_x = px / ppm;
            float obj_y = (tm->map_height * tm->tile_pixel_height - py) / ppm;
            float obj_w = ow / ppm;
            float obj_h = oh / ppm;
            
            /* Apply normalization */
            if (normalize) {
                obj_x = (obj_x - world_w * 0.5f) * norm_scale;
                obj_y = (obj_y - world_h * 0.5f) * norm_scale;
                obj_w *= norm_scale;
                obj_h *= norm_scale;
            }
            
            to->x = obj_x;
            to->y = obj_y;
            to->width = obj_w;
            to->height = obj_h;
            to->is_point = tmx_xml_object_is_point(obj);
            
            /* Parse custom properties */
            to->prop_count = 0;
            void *props = tmx_xml_first_properties(obj);
            if (props) {
                for (void *prop = tmx_xml_first_property(props); 
                     prop && to->prop_count < MAX_OBJECT_PROPS; 
                     prop = tmx_xml_next_property(prop)) {
                    TMX_PROPERTY *p = &to->props[to->prop_count++];
                    const char *pn = tmx_xml_property_get_name(prop);
                    const char *pv = tmx_xml_property_get_value(prop);
                    const char *pt = tmx_xml_property_get_type(prop);
                    strncpy(p->name, pn ? pn : "", 31);
                    strncpy(p->value, pv ? pv : "", 255);
                    strncpy(p->type, pt ? pt : "string", 15);
                }
            }
        }
    }
    
    tmx_xml_free(doc);
    tm->tiles_dirty = 1;
    
    /* Apply auto_center if not normalizing (legacy behavior) */
    if (!normalize && tm->auto_center) {
        float ox = -(tm->map_width * tm->tile_size) / 2.0f;
        float oy = -(tm->map_height * tm->tile_size) / 2.0f;
        tm->offset_x = ox;
        tm->offset_y = oy;
        for (int i = 0; i < tm->tile_count; i++) {
            tm->tiles[i].x += ox;
            tm->tiles[i].y += oy;
        }
        Tcl_HashEntry *e; Tcl_HashSearch s;
        for (e = Tcl_FirstHashEntry(&tm->body_table, &s); e; e = Tcl_NextHashEntry(&s)) {
            b2BodyId *body = Tcl_GetHashValue(e);
            b2Vec2 pos = b2Body_GetPosition(*body);
            pos.x += ox;
            pos.y += oy;
            b2Body_SetTransform(*body, pos, b2Body_GetRotation(*body));
        }
        for (int i = 0; i < tm->object_count; i++) {
            tm->objects[i].x += ox;
            tm->objects[i].y += oy;
        }
    }
    
    /* Update tile_size for sprite creation consistency */
    if (normalize) {
        tm->tile_size *= norm_scale;
        tm->offset_x = 0;
        tm->offset_y = 0;
    }
    
    Tcl_Obj *result = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("tiles",-1), Tcl_NewIntObj(tm->tile_count));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("bodies",-1), Tcl_NewIntObj(tm->body_count));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("objects",-1), Tcl_NewIntObj(tm->object_count));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("atlases",-1), Tcl_NewIntObj(tm->atlas_count));
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

static int tilemapSetGravityCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm gx gy", NULL); return TCL_ERROR; }
    int id; if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    double gx, gy;
    if (Tcl_GetDouble(interp, argv[2], &gx) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &gy) != TCL_OK) return TCL_ERROR;
    tm->gravity = (b2Vec2){(float)gx, (float)gy};
    if (tm->has_world) b2World_SetGravity(tm->world_id, tm->gravity);
    return TCL_OK;
}

static int tilemapCreateSpriteCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 8) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm name tile_id x y w h ?atlas?", NULL);
        return TCL_ERROR;
    }
    int id; if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (tm->sprite_count >= MAX_SPRITES) { Tcl_AppendResult(interp, "max sprites", NULL); return TCL_ERROR; }
    
    int tile_id, atlas_id = 0;
    double x, y, w, h;
    if (Tcl_GetInt(interp, argv[3], &tile_id) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &y) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[6], &w) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[7], &h) != TCL_OK) return TCL_ERROR;
    if (argc > 8) Tcl_GetInt(interp, argv[8], &atlas_id);
    
    SPRITE *sp = &tm->sprites[tm->sprite_count];
    strncpy(sp->name, argv[2], 63);
    sp->x = (float)x; sp->y = (float)y;
    sp->w = (float)w; sp->h = (float)h;
    sp->angle = 0; sp->tile_id = tile_id; sp->atlas_id = atlas_id;
    sp->visible = 1; sp->has_body = 0;
    sp->body_offset_x = 0;
    sp->body_offset_y = 0;

    if (atlas_id < tm->atlas_count) {
        ATLAS *a = &tm->atlases[atlas_id];
        get_tile_uvs(a, tile_id, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(tm->sprite_count++));
    return TCL_OK;
}

/*
 * Create collision shapes for a sprite from tile collision data
 */
static void create_sprite_collision_shapes(TILEMAP *tm, SPRITE *sp, 
                                           TILE_COLLISION *tc,
                                           float friction, float restitution,
                                           float density, int is_sensor)
{
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = density;
    sd.userData = (void *)sp->name;
    sd.isSensor = is_sensor ? true : false;
    sd.enableContactEvents = !is_sensor;
    sd.enableSensorEvents = true;
    
    for (int i = 0; i < tc->shape_count; i++) {
        COLLISION_SHAPE *cs = &tc->shapes[i];
        b2ShapeId shape;
        
        if (cs->type == SHAPE_POLYGON) {
            b2Vec2 points[MAX_COLLISION_VERTS];
            for (int v = 0; v < cs->vert_count; v++) {
                /* Convert normalized coords to sprite size */
                float nx = cs->verts_x[v] - 0.5f;
                float ny = 0.5f - cs->verts_y[v];  /* Flip Y */
                points[v].x = nx * sp->w;
                points[v].y = ny * sp->h;
            }
            
            b2Hull hull = b2ComputeHull(points, cs->vert_count);
            b2Polygon poly = b2MakePolygon(&hull, 0.0f);
            shape = b2CreatePolygonShape(sp->body, &sd, &poly);
            
        } else if (cs->type == SHAPE_BOX) {
            float cx = (cs->box_x + cs->box_w * 0.5f - 0.5f) * sp->w;
            float cy = (0.5f - (cs->box_y + cs->box_h * 0.5f)) * sp->h;
            float hw = cs->box_w * sp->w * 0.5f;
            float hh = cs->box_h * sp->h * 0.5f;
            
            b2Polygon box = b2MakeOffsetBox(hw, hh, (b2Vec2){cx, cy}, b2Rot_identity);
            shape = b2CreatePolygonShape(sp->body, &sd, &box);
            
        } else {
            continue;
        }
        
        b2Shape_SetFriction(shape, friction);
        b2Shape_SetRestitution(shape, restitution);
    }
}

static int tilemapSpriteAddBodyCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
            " tm sprite ?type? ?-fixedrotation 0/1? ?-damping N? ?-friction N? ?-density N? ?-restitution N? ?-sensor 0/1? ?-hitbox_w N? ?-hitbox_h N? ?-hitbox_offset_x N? ?-hitbox_offset_y N?", NULL); 
        return TCL_ERROR;
    }
    
    int id, sid;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    
    SPRITE *sp = &tm->sprites[sid];
    
    /* Parse options */
    b2BodyType bt = b2_dynamicBody;
    int fixed_rotation = 0, is_sensor = 0;
    double damping = 0, friction = 0.3, density = 1.0, restitution = 0;
    double hitbox_w = -1, hitbox_h = -1;           /* -1 = use auto/sprite size */
    double hitbox_offset_x = 0, hitbox_offset_y = 0;
    int hitbox_w_set = 0, hitbox_h_set = 0;
    int offset_x_set = 0, offset_y_set = 0;
    double corner_radius = 0.0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "static") == 0) bt = b2_staticBody;
        else if (strcmp(argv[i], "dynamic") == 0) bt = b2_dynamicBody;
        else if (strcmp(argv[i], "kinematic") == 0) bt = b2_kinematicBody;
        else if (i + 1 < argc) {
            if (strcmp(argv[i], "-fixedrotation") == 0) {
                Tcl_GetInt(interp, argv[++i], &fixed_rotation);
            } else if (strcmp(argv[i], "-damping") == 0) {
                Tcl_GetDouble(interp, argv[++i], &damping);
            } else if (strcmp(argv[i], "-friction") == 0) {
                Tcl_GetDouble(interp, argv[++i], &friction);
            } else if (strcmp(argv[i], "-density") == 0) {
                Tcl_GetDouble(interp, argv[++i], &density);
            } else if (strcmp(argv[i], "-restitution") == 0) {
                Tcl_GetDouble(interp, argv[++i], &restitution);
            } else if (strcmp(argv[i], "-sensor") == 0) {
                Tcl_GetInt(interp, argv[++i], &is_sensor);
            } else if (strcmp(argv[i], "-hitbox_w") == 0) {
                Tcl_GetDouble(interp, argv[++i], &hitbox_w);
                hitbox_w_set = 1;
            } else if (strcmp(argv[i], "-hitbox_h") == 0) {
                Tcl_GetDouble(interp, argv[++i], &hitbox_h);
                hitbox_h_set = 1;
            } else if (strcmp(argv[i], "-hitbox_offset_x") == 0) {
                Tcl_GetDouble(interp, argv[++i], &hitbox_offset_x);
                offset_x_set = 1;
            } else if (strcmp(argv[i], "-hitbox_offset_y") == 0) {
                Tcl_GetDouble(interp, argv[++i], &hitbox_offset_y);
                offset_y_set = 1;
            } else if (strcmp(argv[i], "-corner_radius") == 0) {
                Tcl_GetDouble(interp, argv[++i], &corner_radius);
            }
        }
    }
    
    /* Determine hitbox size and offset */
    float hw, hh, off_x, off_y;
    int use_tile_collision = 0;
    
    /* Check for tile collision shapes (only if no manual hitbox specified) */
TILE_COLLISION *tc = NULL;
if (!hitbox_w_set && !hitbox_h_set) {
    fprintf(stderr, "DEBUG: sprite '%s' tile_id=%d\n", sp->name, sp->tile_id);
    tc = get_tile_collision(tm, sp->tile_id);
    fprintf(stderr, "DEBUG: get_tile_collision returned %p\n", (void*)tc);
    if (tc && tc->shape_count > 0) {
        use_tile_collision = 1;
        fprintf(stderr, "USING tile collision\n");
    }
}
    if (use_tile_collision) {
        /* Will create shapes from tile collision data below */
        off_x = offset_x_set ? (float)hitbox_offset_x : 0;
        off_y = offset_y_set ? (float)hitbox_offset_y : 0;
    } else if (sp->has_hitbox_data && !hitbox_w_set && !hitbox_h_set) {
        /* Use Aseprite hitbox data */
        hw = sp->w * sp->hitbox_w_ratio * 0.5f;
        hh = sp->h * sp->hitbox_h_ratio * 0.5f;
        off_x = offset_x_set ? (float)hitbox_offset_x : sp->w * sp->hitbox_offset_x;
        off_y = offset_y_set ? (float)hitbox_offset_y : sp->h * sp->hitbox_offset_y;
    } else {
        /* Use manual values or sprite size */
        hw = (hitbox_w_set && hitbox_w > 0) ? (float)hitbox_w * 0.5f : sp->w * 0.5f;
        hh = (hitbox_h_set && hitbox_h > 0) ? (float)hitbox_h * 0.5f : sp->h * 0.5f;
        off_x = (float)hitbox_offset_x;
        off_y = (float)hitbox_offset_y;
    }

    /* Store offset for position sync in tilemap_update */
    sp->body_offset_x = off_x;
    sp->body_offset_y = off_y;

    /* Create body at sprite position plus offset */
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = bt;
    bd.position = (b2Vec2){sp->x + off_x, sp->y + off_y};
    bd.linearDamping = (float)damping;
    bd.angularDamping = 0.05f;
    bd.motionLocks.angularZ = fixed_rotation ? true : false;
    
    sp->body = b2CreateBody(tm->world_id, &bd);
    
    if (use_tile_collision) {
        /* Create shapes from tile collision data */
        create_sprite_collision_shapes(tm, sp, tc, 
                                       (float)friction, (float)restitution,
                                       (float)density, is_sensor);
    } else {
        /* Create single box shape */
        b2Polygon box;
        if (corner_radius > 0) {
            box = b2MakeRoundedBox(hw, hh, (float)corner_radius);
        } else {
            box = b2MakeBox(hw, hh);
        }
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.density = (float)density;
        sd.userData = (void *)sp->name;
        sd.isSensor = is_sensor ? true : false;
        sd.enableContactEvents = !is_sensor;
        sd.enableSensorEvents = true;
        b2ShapeId shape = b2CreatePolygonShape(sp->body, &sd, &box);
        b2Shape_SetFriction(shape, (float)friction);
        b2Shape_SetRestitution(shape, (float)restitution);
    }
    
    sp->has_body = 1;
    
    /* Store in hash table */
    int newentry;
    Tcl_HashEntry *e = Tcl_CreateHashEntry(&tm->body_table, sp->name, &newentry);
    b2BodyId *stored = malloc(sizeof(b2BodyId));
    *stored = sp->body;
    Tcl_SetHashValue(e, stored);
    tm->body_count++;
    
    return TCL_OK;
}

static int tilemapRemoveSpriteCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite_id", NULL);
        return TCL_ERROR;
    }
    int id, sid;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    
    SPRITE *sp = &tm->sprites[sid];
    
    /* Destroy the physics body if it exists */
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        /* Remove from hash table */
        Tcl_HashEntry *e = Tcl_FindHashEntry(&tm->body_table, sp->name);
        if (e) {
            free(Tcl_GetHashValue(e));
            Tcl_DeleteHashEntry(e);
            tm->body_count--;
        }
        /* Destroy the Box2D body */
        b2DestroyBody(sp->body);
        sp->has_body = 0;
    }
    
    /* Mark sprite as invisible - don't remove from array to keep indices stable */
    sp->visible = 0;
    
    return TCL_OK;
}
static int tilemapGetObjectsCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm ?type?", NULL); return TCL_ERROR; }
    int id; if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    const char *filter = argc > 2 ? argv[2] : NULL;
    
    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    for (int i = 0; i < tm->object_count; i++) {
        TMX_OBJECT *o = &tm->objects[i];
        if (filter && strcmp(o->type, filter) != 0) continue;
        Tcl_Obj *d = Tcl_NewDictObj();
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("name",-1), Tcl_NewStringObj(o->name,-1));
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("type",-1), Tcl_NewStringObj(o->type,-1));
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("x",-1), Tcl_NewDoubleObj(o->x));
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("y",-1), Tcl_NewDoubleObj(o->y));
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("width",-1), Tcl_NewDoubleObj(o->width));
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("height",-1), Tcl_NewDoubleObj(o->height));
        
        /* Add custom properties as a nested dict */
        if (o->prop_count > 0) {
            Tcl_Obj *props = Tcl_NewDictObj();
            for (int j = 0; j < o->prop_count; j++) {
                TMX_PROPERTY *p = &o->props[j];
                /* Convert value based on type */
                Tcl_Obj *val;
                if (strcmp(p->type, "int") == 0) {
                    val = Tcl_NewIntObj(atoi(p->value));
                } else if (strcmp(p->type, "float") == 0) {
                    val = Tcl_NewDoubleObj(atof(p->value));
                } else if (strcmp(p->type, "bool") == 0) {
                    val = Tcl_NewBooleanObj(strcmp(p->value, "true") == 0);
                } else {
                    val = Tcl_NewStringObj(p->value, -1);
                }
                Tcl_DictObjPut(interp, props, Tcl_NewStringObj(p->name,-1), val);
            }
            Tcl_DictObjPut(interp, d, Tcl_NewStringObj("properties",-1), props);
        }
        
        Tcl_ListObjAppendElement(interp, result, d);
    }
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

static int tilemapGetContactsCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) return TCL_ERROR;
    int id; if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (!tm->has_world) { Tcl_SetResult(interp, "", TCL_STATIC); return TCL_OK; }
    
    b2ContactEvents ev = b2World_GetContactEvents(tm->world_id);
    Tcl_Obj *result = Tcl_NewDictObj();
    Tcl_Obj *begins = Tcl_NewListObj(0, NULL);
    for (int i = 0; i < ev.beginCount; i++) {
        Tcl_Obj *pair = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, pair, Tcl_NewIntObj(ev.beginEvents[i].shapeIdA.index1));
        Tcl_ListObjAppendElement(interp, pair, Tcl_NewIntObj(ev.beginEvents[i].shapeIdB.index1));
        Tcl_ListObjAppendElement(interp, begins, pair);
    }
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("begin",-1), begins);
    Tcl_Obj *ends = Tcl_NewListObj(0, NULL);
    for (int i = 0; i < ev.endCount; i++) {
        Tcl_Obj *pair = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, pair, Tcl_NewIntObj(ev.endEvents[i].shapeIdA.index1));
        Tcl_ListObjAppendElement(interp, pair, Tcl_NewIntObj(ev.endEvents[i].shapeIdB.index1));
        Tcl_ListObjAppendElement(interp, ends, pair);
    }
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("end",-1), ends);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

static int tilemapGetSpriteByNameCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite_name", NULL);
        return TCL_ERROR;
    }
    int id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    const char *name = argv[2];
    
    // Search for sprite by name
    for (int i = 0; i < tm->sprite_count; i++) {
        if (strcmp(tm->sprites[i].name, name) == 0) {
            Tcl_SetObjResult(interp, Tcl_NewIntObj(i));
            return TCL_OK;
        }
    }
    
    Tcl_AppendResult(interp, "sprite not found: ", name, NULL);
    return TCL_ERROR;
}

static int tilemapGetSpriteCountCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm", NULL);
        return TCL_ERROR;
    }
    int id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(tm->sprite_count));
    return TCL_OK;
}

/* tilemapGetSpriteInfo - get sprite position, angle, etc. for debugging */
static int tilemapGetSpriteInfoCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite_id", NULL); return TCL_ERROR; }
    int id, sid;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    
    SPRITE *sp = &tm->sprites[sid];
    Tcl_Obj *result = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("name",-1), Tcl_NewStringObj(sp->name,-1));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("x",-1), Tcl_NewDoubleObj(sp->x));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("y",-1), Tcl_NewDoubleObj(sp->y));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("angle",-1), Tcl_NewDoubleObj(sp->angle));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("has_body",-1), Tcl_NewIntObj(sp->has_body));
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Vec2 pos = b2Body_GetPosition(sp->body);
        b2Vec2 vel = b2Body_GetLinearVelocity(sp->body);
        Tcl_DictObjPut(interp, result, Tcl_NewStringObj("body_x",-1), Tcl_NewDoubleObj(pos.x));
        Tcl_DictObjPut(interp, result, Tcl_NewStringObj("body_y",-1), Tcl_NewDoubleObj(pos.y));
        Tcl_DictObjPut(interp, result, Tcl_NewStringObj("vel_x",-1), Tcl_NewDoubleObj(vel.x));
        Tcl_DictObjPut(interp, result, Tcl_NewStringObj("vel_y",-1), Tcl_NewDoubleObj(vel.y));
    }
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* tilemapSetOffset - offset all rendering by x,y (for centering) */
static int tilemapSetOffsetCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm offset_x offset_y", NULL); return TCL_ERROR; }
    int id;
    double ox, oy;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetDouble(interp, argv[2], &ox) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &oy) != TCL_OK) return TCL_ERROR;
    
    /* Offset all tiles */
    for (int i = 0; i < tm->tile_count; i++) {
        tm->tiles[i].x += (float)ox;
        tm->tiles[i].y += (float)oy;
    }
    /* Offset all sprites (and their bodies) */
    for (int i = 0; i < tm->sprite_count; i++) {
        tm->sprites[i].x += (float)ox;
        tm->sprites[i].y += (float)oy;
        if (tm->sprites[i].has_body && b2Body_IsValid(tm->sprites[i].body)) {
            b2Vec2 pos = b2Body_GetPosition(tm->sprites[i].body);
            pos.x += (float)ox;
            pos.y += (float)oy;
            b2Body_SetTransform(tm->sprites[i].body, pos, b2Body_GetRotation(tm->sprites[i].body));
        }
    }
    /* Offset all static bodies too */
    Tcl_HashEntry *e; Tcl_HashSearch s;
    for (e = Tcl_FirstHashEntry(&tm->body_table, &s); e; e = Tcl_NextHashEntry(&s)) {
        b2BodyId *body = Tcl_GetHashValue(e);
        if (b2Body_GetType(*body) == b2_staticBody) {
            b2Vec2 pos = b2Body_GetPosition(*body);
            pos.x += (float)ox;
            pos.y += (float)oy;
            b2Body_SetTransform(*body, pos, b2Body_GetRotation(*body));
        }
    }
    tm->tiles_dirty = 1;
    return TCL_OK;
}

/* tilemapSetSpriteVisible - show/hide a sprite */
static int tilemapSetSpriteVisibleCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite visible(0/1)", NULL); return TCL_ERROR; }
    int id, sid, visible;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    if (Tcl_GetInt(interp, argv[3], &visible) != TCL_OK) return TCL_ERROR;
    
    tm->sprites[sid].visible = visible;
    return TCL_OK;
}


static int tilemapGetSpriteTilesetsCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) {
        Tcl_AppendResult(interp, "invalid tilemap", NULL);
        return TCL_ERROR;
    }
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    Tcl_Obj *list = Tcl_NewListObj(0, NULL);
    
    for (int i = 0; i < tm->sprite_tileset_count; i++) {
        SPRITE_TILESET *sts = &tm->sprite_tilesets[i];
        Tcl_Obj *dict = Tcl_NewDictObj();
        
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("name", -1), 
                       Tcl_NewStringObj(sts->name, -1));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("firstgid", -1), 
                       Tcl_NewIntObj(sts->firstgid));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("tile_width", -1), 
                       Tcl_NewIntObj(sts->tile_width));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("tile_height", -1), 
                       Tcl_NewIntObj(sts->tile_height));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("atlas_id", -1), 
                       Tcl_NewIntObj(sts->atlas_id));
        
        /* Add animation names if available */
        if (sts->has_aseprite) {
            Tcl_Obj *anim_list = Tcl_NewListObj(0, NULL);
            for (int j = 0; j < sts->aseprite.animation_count; j++) {
                Tcl_ListObjAppendElement(interp, anim_list, 
                    Tcl_NewStringObj(sts->aseprite.animations[j].name, -1));
            }
            Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("animations", -1), anim_list);
        }
        
        Tcl_ListObjAppendElement(interp, list, dict);
    }
    
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

/* tilemapGetAnimationFrames - get frame GIDs for a named animation */
static int tilemapGetAnimationFramesCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm tileset_name animation_name", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) {
        Tcl_AppendResult(interp, "invalid tilemap", NULL);
        return TCL_ERROR;
    }
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    SPRITE_TILESET *sts = find_sprite_tileset(tm, argv[2]);
    if (!sts) {
        Tcl_AppendResult(interp, "tileset not found: ", argv[2], NULL);
        return TCL_ERROR;
    }
    
    if (!sts->has_aseprite) {
        Tcl_AppendResult(interp, "tileset has no animation data: ", argv[2], NULL);
        return TCL_ERROR;
    }
    
    AsepriteAnimation *anim = aseprite_find_animation(&sts->aseprite, argv[3]);
    if (!anim) {
        Tcl_AppendResult(interp, "animation not found: ", argv[3], NULL);
        return TCL_ERROR;
    }
    
    Tcl_Obj *list = Tcl_NewListObj(0, NULL);
    for (int i = 0; i < anim->frame_count; i++) {
        Tcl_ListObjAppendElement(interp, list, Tcl_NewIntObj(anim->frames[i]));
    }
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

/* tilemapSetSpriteAnimationByName - set sprite animation using tileset/animation names */
static int tilemapSetSpriteAnimationByNameCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) {
        Tcl_AppendResult(interp, "usage: ", argv[0], 
                         " tm sprite tileset_name animation_name ?fps? ?loop?", NULL);
        return TCL_ERROR;
    }
    
    int id, sid;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) {
        Tcl_AppendResult(interp, "invalid tilemap", NULL);
        return TCL_ERROR;
    }
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) {
        Tcl_AppendResult(interp, "invalid sprite", NULL);
        return TCL_ERROR;
    }
    
    SPRITE_TILESET *sts = find_sprite_tileset(tm, argv[3]);
    if (!sts) {
        Tcl_AppendResult(interp, "tileset not found: ", argv[3], NULL);
        return TCL_ERROR;
    }
    
    if (!sts->has_aseprite) {
        Tcl_AppendResult(interp, "tileset has no animation data: ", argv[3], NULL);
        return TCL_ERROR;
    }
    
    AsepriteAnimation *anim = aseprite_find_animation(&sts->aseprite, argv[4]);
    if (!anim) {
        Tcl_AppendResult(interp, "animation not found: ", argv[4], NULL);
        return TCL_ERROR;
    }
    
    /* Get optional fps and loop */
    float fps = anim->default_fps;
    int loop = 1;
    if (argc > 5) {
        double d;
        if (Tcl_GetDouble(interp, argv[5], &d) == TCL_OK) fps = (float)d;
    }
    if (argc > 6) {
        Tcl_GetInt(interp, argv[6], &loop);
    }
    
    /* Apply to sprite */
    SPRITE *sp = &tm->sprites[sid];
    sp->anim_frame_count = anim->frame_count > 32 ? 32 : anim->frame_count;
    for (int i = 0; i < sp->anim_frame_count; i++) {
        sp->anim_frames[i] = anim->frames[i];
    }
    sp->anim_fps = fps;
    sp->anim_loop = loop;
    sp->anim_current_frame = 0;
    sp->anim_time = 0;
    sp->anim_playing = 1;  /* auto-start */
    
    /* Update sprite's atlas to match tileset */
    sp->atlas_id = sts->atlas_id;
    
    /* Set initial tile */
    if (sp->anim_frame_count > 0) {
        int tile_id = sp->anim_frames[0];
        sp->tile_id = tile_id;
        ATLAS *atlas = &tm->atlases[sp->atlas_id];
        get_tile_uvs(atlas, tile_id, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
    }
    
    return TCL_OK;
}

/* tilemapCreateSpriteFromTileset - create sprite using tileset name */
static int tilemapCreateSpriteFromTilesetCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 8) {
        Tcl_AppendResult(interp, "usage: ", argv[0], 
                         " tm name tileset_name x y w h ?animation?", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) {
        Tcl_AppendResult(interp, "invalid tilemap", NULL);
        return TCL_ERROR;
    }
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (tm->sprite_count >= MAX_SPRITES) {
        Tcl_AppendResult(interp, "max sprites reached", NULL);
        return TCL_ERROR;
    }
    
    SPRITE_TILESET *sts = find_sprite_tileset(tm, argv[3]);
    if (!sts) {
        Tcl_AppendResult(interp, "tileset not found: ", argv[3], NULL);
        return TCL_ERROR;
    }
    
    double x, y, w, h;
    if (Tcl_GetDouble(interp, argv[4], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &y) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[6], &w) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[7], &h) != TCL_OK) return TCL_ERROR;
    
    /* Create sprite */
    int sid = tm->sprite_count;
    SPRITE *sp = &tm->sprites[tm->sprite_count++];
    memset(sp, 0, sizeof(SPRITE));
    
    strncpy(sp->name, argv[2], 63);
    sp->name[63] = '\0';
    sp->x = (float)x;
    sp->y = (float)y;
    sp->w = (float)w;
    sp->h = (float)h;
    sp->angle = 0;
    sp->atlas_id = sts->atlas_id;
    sp->tile_id = sts->firstgid;  /* default to first tile */
    sp->visible = 1;
    sp->has_body = 0;
    sp->body_offset_x = 0;
    sp->body_offset_y = 0;

    if (sts->has_aseprite && sts->aseprite.has_hitbox) {
        sp->has_hitbox_data = 1;
        sp->hitbox_w_ratio = sts->aseprite.hitbox_width_ratio;
        sp->hitbox_h_ratio = sts->aseprite.hitbox_height_ratio;
        sp->hitbox_offset_x = sts->aseprite.hitbox_offset_x;
        sp->hitbox_offset_y = sts->aseprite.hitbox_offset_y;
        
        /* Scale sprite size so hitbox portion matches requested size */
        float old_w = sp->w;
        float old_h = sp->h;
        sp->w = sp->w / sp->hitbox_w_ratio;
        sp->h = sp->h / sp->hitbox_h_ratio;
        
        /* Adjust position so the visible character (hitbox region) stays centered
           at the user-specified position, not the frame center */
        float w_increase = sp->w - old_w;  // 5.455 - 1.875 = 3.58
        float h_increase = sp->h - old_h;  // 5.455 - 1.875 = 3.58
        
        /* Shift by half the increase to keep hitbox centered at original position */
        sp->x += w_increase * 0.5f * sp->hitbox_offset_x;
        sp->y += h_increase * 0.5f * sp->hitbox_offset_y;
    } else {
        sp->has_hitbox_data = 0;
    }

    /* Set initial UVs */
    if (sts->atlas_id >= 0 && sts->atlas_id < tm->atlas_count) {
        ATLAS *atlas = &tm->atlases[sts->atlas_id];
        get_tile_uvs(atlas, sp->tile_id, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
    }
    
    /* Apply animation if specified */
    if (argc > 8 && sts->has_aseprite) {
        AsepriteAnimation *anim = aseprite_find_animation(&sts->aseprite, argv[8]);
        if (anim) {
            sp->anim_frame_count = anim->frame_count > 32 ? 32 : anim->frame_count;
            for (int i = 0; i < sp->anim_frame_count; i++) {
                sp->anim_frames[i] = anim->frames[i];
            }
            sp->anim_fps = anim->default_fps;
            sp->anim_loop = 1;
            sp->anim_current_frame = 0;
            sp->anim_time = 0;
            sp->anim_playing = 1;
            
            /* Set initial tile from animation */
            if (sp->anim_frame_count > 0) {
                sp->tile_id = sp->anim_frames[0];
                ATLAS *atlas = &tm->atlases[sp->atlas_id];
                get_tile_uvs(atlas, sp->tile_id, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
            }
        }
    }
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(sid));
    return TCL_OK;
}

/* tilemapGetMapInfo - get map dimensions for auto-centering */
static int tilemapGetMapInfoCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm", NULL); return TCL_ERROR; }
    int id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    Tcl_Obj *result = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("width_tiles",-1), Tcl_NewIntObj(tm->map_width));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("height_tiles",-1), Tcl_NewIntObj(tm->map_height));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("tile_size",-1), Tcl_NewDoubleObj(tm->tile_size));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("world_width",-1), Tcl_NewDoubleObj(tm->map_width * tm->tile_size));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("world_height",-1), Tcl_NewDoubleObj(tm->map_height * tm->tile_size));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("pixels_per_meter",-1), Tcl_NewDoubleObj(tm->pixels_per_meter));
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

static int tilemapSetSpritePositionCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite x y", NULL); return TCL_ERROR; }
    int id, sid;
    double x, y;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &y) != TCL_OK) return TCL_ERROR;
    
    SPRITE *sp = &tm->sprites[sid];
    sp->x = (float)x; sp->y = (float)y;
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Body_SetTransform(sp->body, (b2Vec2){sp->x, sp->y}, b2Body_GetRotation(sp->body));
    }
    return TCL_OK;
}

static int tilemapSetSpriteRotationCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite_id angle_radians", NULL);
        return TCL_ERROR;
    }
    int id, sid;
    double angle;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &angle) != TCL_OK) return TCL_ERROR;
    
    SPRITE *sp = &tm->sprites[sid];
    sp->angle = (float)angle;
    
    // If sprite has a body, rotate the body too
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Vec2 pos = b2Body_GetPosition(sp->body);
        b2Body_SetTransform(sp->body, pos, b2MakeRot((float)angle));
    }
    
    return TCL_OK;
}

static int tilemapSetSpriteTileCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite tile_id", NULL); return TCL_ERROR; }
    int id, sid, tile_id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    if (Tcl_GetInt(interp, argv[3], &tile_id) != TCL_OK) return TCL_ERROR;
    
    SPRITE *sp = &tm->sprites[sid];
    sp->tile_id = tile_id;
    if (sp->atlas_id < tm->atlas_count) {
        ATLAS *a = &tm->atlases[sp->atlas_id];
        get_tile_uvs(a, tile_id, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
    }
    return TCL_OK;
}

/* tilemapApplyImpulse - apply impulse to sprite body (for jumping, etc.) */
static int tilemapApplyImpulseCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite ix iy", NULL); return TCL_ERROR; }
    int id, sid;
    double ix, iy;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &ix) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &iy) != TCL_OK) return TCL_ERROR;
    
    SPRITE *sp = &tm->sprites[sid];
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Vec2 impulse = {(float)ix, (float)iy};
        b2Vec2 center = b2Body_GetPosition(sp->body);
        b2Body_ApplyLinearImpulseToCenter(sp->body, impulse, true);
    }
    return TCL_OK;
}

/* tilemapSetLinearVelocity - set sprite body velocity directly */
static int tilemapSetLinearVelocityCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite vx vy", NULL); return TCL_ERROR; }
    int id, sid;
    double vx, vy;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &vx) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &vy) != TCL_OK) return TCL_ERROR;
    
    SPRITE *sp = &tm->sprites[sid];
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Vec2 vel = {(float)vx, (float)vy};
        b2Body_SetLinearVelocity(sp->body, vel);
    }
    return TCL_OK;
}

/* tilemapApplyForce - apply continuous force to sprite body */
static int tilemapApplyForceCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite fx fy", NULL); return TCL_ERROR; }
    int id, sid;
    double fx, fy;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &fx) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &fy) != TCL_OK) return TCL_ERROR;
    
    SPRITE *sp = &tm->sprites[sid];
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Vec2 force = {(float)fx, (float)fy};
        b2Body_ApplyForceToCenter(sp->body, force, true);
    }
    return TCL_OK;
}

/* tilemapSetSpriteAnimation - set animation frames for sprite */
static int tilemapSetSpriteAnimationCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) { 
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite fps frame_list ?loop?", NULL); 
        return TCL_ERROR; 
    }
    int id, sid, loop = 1;
    double fps;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &fps) != TCL_OK) return TCL_ERROR;
    if (argc > 5) Tcl_GetInt(interp, argv[5], &loop);
    
    /* Parse frame list */
    Tcl_Size listc;
    const char **listv;
    if (Tcl_SplitList(interp, argv[4], &listc, &listv) != TCL_OK) return TCL_ERROR;
    
    SPRITE *sp = &tm->sprites[sid];
    sp->anim_frame_count = listc > 32 ? 32 : listc;
    for (int i = 0; i < sp->anim_frame_count; i++) {
        sp->anim_frames[i] = atoi(listv[i]);
    }
    Tcl_Free((char *)listv);
    
    sp->anim_fps = (float)fps;
    sp->anim_loop = loop;
    sp->anim_current_frame = 0;
    sp->anim_time = 0;
    sp->anim_playing = 0;  /* start paused, use tilemapPlayAnimation */
    
    return TCL_OK;
}

/* tilemapPlayAnimation - start/stop sprite animation */
static int tilemapPlayAnimationCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm sprite play(0/1)", NULL); return TCL_ERROR; }
    int id, sid, play;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    if (Tcl_GetInt(interp, argv[3], &play) != TCL_OK) return TCL_ERROR;
    
    SPRITE *sp = &tm->sprites[sid];
    sp->anim_playing = play;
    if (play) {
        sp->anim_time = 0;
        sp->anim_current_frame = 0;
    }
    return TCL_OK;
}

/* tilemapSetCollisionCallback - set Tcl proc to call on collisions */
static int tilemapSetCollisionCallbackCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm callback_proc", NULL); return TCL_ERROR; }
    int id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    strncpy(tm->collision_callback, argv[2], sizeof(tm->collision_callback) - 1);
    tm->collision_callback[sizeof(tm->collision_callback) - 1] = '\0';
    
    /* Enable contact events on all sprite bodies */
    for (int i = 0; i < tm->sprite_count; i++) {
        if (tm->sprites[i].has_body && b2Body_IsValid(tm->sprites[i].body)) {
            int shapeCount = b2Body_GetShapeCount(tm->sprites[i].body);
            b2ShapeId shapes[16];
            b2Body_GetShapes(tm->sprites[i].body, shapes, shapeCount > 16 ? 16 : shapeCount);
            for (int j = 0; j < shapeCount && j < 16; j++) {
                b2Shape_EnableContactEvents(shapes[j], true);
            }
        }
    }
    return TCL_OK;
}

/* tilemapSetAutoCenter - enable/disable auto-centering on load */
static int tilemapSetAutoCenterCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) { Tcl_AppendResult(interp, "usage: ", argv[0], " tm enabled(0/1)", NULL); return TCL_ERROR; }
    int id, enabled;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetInt(interp, argv[2], &enabled) != TCL_OK) return TCL_ERROR;
    
    tm->auto_center = enabled;
    return TCL_OK;
}

/* tilemapSetCameraMode - set camera mode and parameters
 * Usage:
 *   tilemapSetCameraMode $tm locked
 *   tilemapSetCameraMode $tm scroll $vx $vy
 *   tilemapSetCameraMode $tm follow $sprite_id
 *   tilemapSetCameraMode $tm deadzone $sprite_id $width $height
 *   tilemapSetCameraMode $tm lookahead $sprite_id $look_x $look_y
 */
static int tilemapSetCameraModeCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], 
            " tm mode ?args?\n"
            "  modes: locked, scroll vx vy, follow sprite, deadzone sprite w h, lookahead sprite lx ly",
            NULL);
        return TCL_ERROR;
    }
    int id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    CAMERA *cam = &tm->camera;
    
    const char *mode = argv[2];
    
    if (strcmp(mode, "locked") == 0) {
        cam->mode = CAM_LOCKED;
    }
    else if (strcmp(mode, "scroll") == 0) {
        if (argc < 5) {
            Tcl_AppendResult(interp, "scroll mode requires: vx vy", NULL);
            return TCL_ERROR;
        }
        double vx, vy;
        if (Tcl_GetDouble(interp, argv[3], &vx) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[4], &vy) != TCL_OK) return TCL_ERROR;
        cam->mode = CAM_FIXED_SCROLL;
        cam->scroll_vx = (float)vx;
        cam->scroll_vy = (float)vy;
    }
    else if (strcmp(mode, "follow") == 0) {
        if (argc < 4) {
            Tcl_AppendResult(interp, "follow mode requires: sprite_id", NULL);
            return TCL_ERROR;
        }
        int sid;
        if (Tcl_GetInt(interp, argv[3], &sid) != TCL_OK) return TCL_ERROR;
        cam->mode = CAM_FOLLOW;
        cam->follow_sprite = sid;
    }
    else if (strcmp(mode, "deadzone") == 0) {
        if (argc < 6) {
            Tcl_AppendResult(interp, "deadzone mode requires: sprite_id width height", NULL);
            return TCL_ERROR;
        }
        int sid;
        double w, h;
        if (Tcl_GetInt(interp, argv[3], &sid) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[4], &w) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[5], &h) != TCL_OK) return TCL_ERROR;
        cam->mode = CAM_FOLLOW_DEADZONE;
        cam->follow_sprite = sid;
        cam->deadzone_w = (float)w;
        cam->deadzone_h = (float)h;
    }
    else if (strcmp(mode, "lookahead") == 0) {
        if (argc < 6) {
            Tcl_AppendResult(interp, "lookahead mode requires: sprite_id look_x look_y", NULL);
            return TCL_ERROR;
        }
        int sid;
        double lx, ly;
        if (Tcl_GetInt(interp, argv[3], &sid) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[4], &lx) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[5], &ly) != TCL_OK) return TCL_ERROR;
        cam->mode = CAM_FOLLOW_LOOKAHEAD;
        cam->follow_sprite = sid;
        cam->lookahead_x = (float)lx;
        cam->lookahead_y = (float)ly;
    }
    else {
        Tcl_AppendResult(interp, "unknown mode: ", mode, NULL);
        return TCL_ERROR;
    }
    
    return TCL_OK;
}

/* tilemapSetCameraSmooth - set camera smoothing factor */
static int tilemapSetCameraSmoothCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm smooth_speed (0=instant, higher=smoother)", NULL);
        return TCL_ERROR;
    }
    int id;
    double smooth;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetDouble(interp, argv[2], &smooth) != TCL_OK) return TCL_ERROR;
    
    tm->camera.smooth_speed = (float)smooth;
    return TCL_OK;
}

/* tilemapSetCameraBounds - set camera movement bounds */
static int tilemapSetCameraBoundsCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 6) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm min_x max_x min_y max_y", NULL);
        return TCL_ERROR;
    }
    int id;
    double minx, maxx, miny, maxy;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetDouble(interp, argv[2], &minx) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &maxx) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &miny) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &maxy) != TCL_OK) return TCL_ERROR;
    
    tm->camera.min_x = (float)minx;
    tm->camera.max_x = (float)maxx;
    tm->camera.min_y = (float)miny;
    tm->camera.max_y = (float)maxy;
    tm->camera.use_bounds = 1;
    return TCL_OK;
}

/* tilemapClearCameraBounds - disable camera bounds */
static int tilemapClearCameraBoundsCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm", NULL);
        return TCL_ERROR;
    }
    int id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    tm->camera.use_bounds = 0;
    return TCL_OK;
}

/* tilemapSetCameraPos - manually set camera position */
static int tilemapSetCameraPosCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm x y", NULL);
        return TCL_ERROR;
    }
    int id;
    double x, y;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;
    
    tm->camera.x = (float)x;
    tm->camera.y = (float)y;
    tm->camera.target_x = (float)x;
    tm->camera.target_y = (float)y;
    return TCL_OK;
}

/* tilemapGetCameraInfo - get current camera state */
static int tilemapGetCameraInfoCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm", NULL);
        return TCL_ERROR;
    }
    int id;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    CAMERA *cam = &tm->camera;
    
    const char *mode_str = "locked";
    switch (cam->mode) {
        case CAM_LOCKED: mode_str = "locked"; break;
        case CAM_FIXED_SCROLL: mode_str = "scroll"; break;
        case CAM_FOLLOW: mode_str = "follow"; break;
        case CAM_FOLLOW_DEADZONE: mode_str = "deadzone"; break;
        case CAM_FOLLOW_LOOKAHEAD: mode_str = "lookahead"; break;
    }
    
    Tcl_Obj *result = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("x",-1), Tcl_NewDoubleObj(cam->x));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("y",-1), Tcl_NewDoubleObj(cam->y));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("target_x",-1), Tcl_NewDoubleObj(cam->target_x));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("target_y",-1), Tcl_NewDoubleObj(cam->target_y));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("mode",-1), Tcl_NewStringObj(mode_str,-1));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("follow_sprite",-1), Tcl_NewIntObj(cam->follow_sprite));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("scroll_vx",-1), Tcl_NewDoubleObj(cam->scroll_vx));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("scroll_vy",-1), Tcl_NewDoubleObj(cam->scroll_vy));
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* tilemapQueryPoint - check if a point overlaps any collision body
 * 
 * Usage: tilemapQueryPoint $tm $x $y ?-ignore $sprite_id?
 * 
 * Returns: 1 if point overlaps a solid body, 0 if clear
 * 
 * The optional -ignore flag excludes a sprite's body from the query,
 * useful when checking if the player can move somewhere without
 * detecting collision with themselves.
 * 
 * Note: Uses a tiny AABB around the point since Box2D v3 doesn't have
 * a direct point query - uses b2World_OverlapAABB instead.
 */
static int tilemapQueryPointCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm x y ?-ignore sprite_id?", NULL);
        return TCL_ERROR;
    }
    
    int id;
    double x, y;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) {
        Tcl_AppendResult(interp, "invalid tilemap", NULL);
        return TCL_ERROR;
    }
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (!tm->has_world) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;
    
    /* Set up query context */
    PointQueryContext ctx = {0, {0}, 0};
    
    /* Parse optional -ignore flag */
    for (int i = 4; i < argc - 1; i++) {
        if (strcmp(argv[i], "-ignore") == 0) {
            int sid;
            if (Tcl_GetInt(interp, argv[i+1], &sid) != TCL_OK) return TCL_ERROR;
            if (sid >= 0 && sid < tm->sprite_count && tm->sprites[sid].has_body) {
                ctx.ignore_body = tm->sprites[sid].body;
                ctx.use_ignore = 1;
            }
        }
    }
    
    /* Create a tiny AABB around the point for query */
    float epsilon = 0.01f;
    b2AABB aabb;
    aabb.lowerBound = (b2Vec2){(float)x - epsilon, (float)y - epsilon};
    aabb.upperBound = (b2Vec2){(float)x + epsilon, (float)y + epsilon};
    
    /* Query the world using Box2D v3 API */
    b2QueryFilter filter = b2DefaultQueryFilter();
    b2World_OverlapAABB(tm->world_id, aabb, filter, point_query_callback, &ctx);
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(ctx.hit));
    return TCL_OK;
}

/* tilemapQueryAABB - check if any collision body overlaps a rectangle
 * 
 * Usage: tilemapQueryAABB $tm $x1 $y1 $x2 $y2 ?-ignore $sprite_id?
 * 
 * Returns: 1 if any body overlaps the rectangle, 0 if clear
 * 
 * Useful for checking if a larger area is clear (e.g., can a player fit here?)
 */
static int tilemapQueryAABBCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 6) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " tm x1 y1 x2 y2 ?-ignore sprite_id?", NULL);
        return TCL_ERROR;
    }
    
    int id;
    double x1, y1, x2, y2;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) {
        Tcl_AppendResult(interp, "invalid tilemap", NULL);
        return TCL_ERROR;
    }
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (!tm->has_world) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &x1) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &y1) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &x2) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &y2) != TCL_OK) return TCL_ERROR;
    
    /* Set up query context */
    PointQueryContext ctx = {0, {0}, 0};
    
    /* Parse optional -ignore flag */
    for (int i = 6; i < argc - 1; i++) {
        if (strcmp(argv[i], "-ignore") == 0) {
            int sid;
            if (Tcl_GetInt(interp, argv[i+1], &sid) != TCL_OK) return TCL_ERROR;
            if (sid >= 0 && sid < tm->sprite_count && tm->sprites[sid].has_body) {
                ctx.ignore_body = tm->sprites[sid].body;
                ctx.use_ignore = 1;
            }
        }
    }
    
    /* Create AABB - ensure min/max are correct */
    b2AABB aabb;
    aabb.lowerBound = (b2Vec2){(float)(x1 < x2 ? x1 : x2), (float)(y1 < y2 ? y1 : y2)};
    aabb.upperBound = (b2Vec2){(float)(x1 > x2 ? x1 : x2), (float)(y1 > y2 ? y1 : y2)};
    
    /* Query the world */
    b2QueryFilter filter = b2DefaultQueryFilter();
    b2World_OverlapAABB(tm->world_id, aabb, filter, point_query_callback, &ctx);
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(ctx.hit));
    return TCL_OK;
}

/*========================================================================
 * Module Init
 *========================================================================*/

#ifdef _WIN32
EXPORT(int, Tilemap_Init)(Tcl_Interp *interp)
#else
int Tilemap_Init(Tcl_Interp *interp)
#endif
{
    OBJ_LIST *OBJList = getOBJList();
    if (
#ifdef USE_TCL_STUBS
        Tcl_InitStubs(interp, "8.5-", 0)
#else
        Tcl_PkgRequire(interp, "Tcl", "8.5-", 0)
#endif
        == NULL) return TCL_ERROR;
    
    if (TilemapID < 0) { TilemapID = gobjRegisterType(); gladLoadGL(); }
    
    Tcl_CreateCommand(interp, "tilemapCreate", (Tcl_CmdProc*)tilemapCreateCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapLoadTMX", (Tcl_CmdProc*)tilemapLoadTMXCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetGravity", (Tcl_CmdProc*)tilemapSetGravityCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapCreateSprite", 
(Tcl_CmdProc*)tilemapCreateSpriteCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapRemoveSprite", 
(Tcl_CmdProc*)tilemapRemoveSpriteCmd, (ClientData) OBJList, NULL);
	Tcl_CreateCommand(interp, "tilemapSpriteAddBody", (Tcl_CmdProc*)tilemapSpriteAddBodyCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetSpritePosition", (Tcl_CmdProc*)tilemapSetSpritePositionCmd, (ClientData)OBJList, NULL);
        Tcl_CreateCommand(interp, "tilemapSetSpriteRotation", (Tcl_CmdProc*)tilemapSetSpriteRotationCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetSpriteTile", (Tcl_CmdProc*)tilemapSetSpriteTileCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapGetObjects", (Tcl_CmdProc*)tilemapGetObjectsCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapGetContacts", (Tcl_CmdProc*)tilemapGetContactsCmd, (ClientData)OBJList, NULL);
        Tcl_CreateCommand(interp, "tilemapGetSpriteCount", (Tcl_CmdProc*)tilemapGetSpriteCountCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapGetSpriteInfo", (Tcl_CmdProc*)tilemapGetSpriteInfoCmd, (ClientData)OBJList, NULL);
        Tcl_CreateCommand(interp, "tilemapGetSpriteByName", (Tcl_CmdProc*)tilemapGetSpriteByNameCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetOffset", (Tcl_CmdProc*)tilemapSetOffsetCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapGetMapInfo", (Tcl_CmdProc*)tilemapGetMapInfoCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapApplyImpulse", (Tcl_CmdProc*)tilemapApplyImpulseCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetLinearVelocity", (Tcl_CmdProc*)tilemapSetLinearVelocityCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapApplyForce", (Tcl_CmdProc*)tilemapApplyForceCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetSpriteAnimation", (Tcl_CmdProc*)tilemapSetSpriteAnimationCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapPlayAnimation", (Tcl_CmdProc*)tilemapPlayAnimationCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetCollisionCallback", (Tcl_CmdProc*)tilemapSetCollisionCallbackCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetAutoCenter", (Tcl_CmdProc*)tilemapSetAutoCenterCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetSpriteVisible", (Tcl_CmdProc*)tilemapSetSpriteVisibleCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetCameraMode", (Tcl_CmdProc*)tilemapSetCameraModeCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetCameraSmooth", (Tcl_CmdProc*)tilemapSetCameraSmoothCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetCameraBounds", (Tcl_CmdProc*)tilemapSetCameraBoundsCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapClearCameraBounds", (Tcl_CmdProc*)tilemapClearCameraBoundsCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetCameraPos", (Tcl_CmdProc*)tilemapSetCameraPosCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapGetCameraInfo", (Tcl_CmdProc*)tilemapGetCameraInfoCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapQueryPoint", (Tcl_CmdProc*)tilemapQueryPointCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapQueryAABB", (Tcl_CmdProc*)tilemapQueryAABBCmd, (ClientData)OBJList, NULL);
        Tcl_CreateCommand(interp, "tilemapGetSpriteTilesets", 
        (Tcl_CmdProc*)tilemapGetSpriteTilesetsCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapGetAnimationFrames", 
        (Tcl_CmdProc*)tilemapGetAnimationFramesCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetSpriteAnimationByName", 
        (Tcl_CmdProc*)tilemapSetSpriteAnimationByNameCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapCreateSpriteFromTileset", 
        (Tcl_CmdProc*)tilemapCreateSpriteFromTilesetCmd, (ClientData)OBJList, NULL);


    
    return TCL_OK;
}

#ifdef WIN32
BOOL APIENTRY DllEntryPoint(HINSTANCE hInst, DWORD reason, LPVOID reserved) { return TRUE; }
#endif