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
 * Author: DLS 2024
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
#include <tcl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stim2.h>
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
#ifdef __cplusplus
}
#endif

#define MAX_TILES 8192
#define MAX_SPRITES 256
#define MAX_ATLASES 4
#define MAX_OBJECTS 256
#define MAX_PATH_LEN 512

typedef struct {
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
    char value[64];
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

typedef struct {
    TILE_INSTANCE tiles[MAX_TILES];
    int tile_count;
    int layer_counts[8];    /* tiles per layer for z-order rendering */
    int num_layers;
    SPRITE sprites[MAX_SPRITES];
    int sprite_count;
    TMX_OBJECT objects[MAX_OBJECTS];
    int object_count;
    ATLAS atlases[MAX_ATLASES];
    int atlas_count;
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
            if (!sp->visible) continue;
            if (sp->atlas_id >= 0 && sp->atlas_id < tm->atlas_count)
                glBindTexture(GL_TEXTURE_2D, tm->atlases[sp->atlas_id].texture);
            build_sprite_verts(sp, sv);
            glBindBuffer(GL_ARRAY_BUFFER, tm->sprite_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(sv), sv);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }
    glBindVertexArray(0);
    glUseProgram(0);
}

static void tilemap_update(GR_OBJ *obj) {
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(obj);
    if (!tm->has_world) return;
    float dt = getFrameDuration() / 1000.0f;
    if (dt > 0.1f) dt = 0.016f;
    
    b2World_Step(tm->world_id, dt, tm->substep_count);
    
    /* Update sprites from physics and handle animation */
    for (int i = 0; i < tm->sprite_count; i++) {
        SPRITE *sp = &tm->sprites[i];
        
        /* Update position from physics body */
        if (sp->has_body && b2Body_IsValid(sp->body)) {
            b2Vec2 pos = b2Body_GetPosition(sp->body);
            sp->x = pos.x; sp->y = pos.y;
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
                    get_tile_uvs(a, sp->tile_id + a->firstgid,
                                 &sp->u0, &sp->v0, &sp->u1, &sp->v1);
                }
            }
        }
    }
    
    /* Process collision callbacks */
    if (tm->collision_callback[0] != '\0') {
        b2ContactEvents ev = b2World_GetContactEvents(tm->world_id);
        for (int i = 0; i < ev.beginCount; i++) {
            /* Find body names from shapes */
            b2BodyId bodyA = b2Shape_GetBody(ev.beginEvents[i].shapeIdA);
            b2BodyId bodyB = b2Shape_GetBody(ev.beginEvents[i].shapeIdB);
            
            /* Find names by searching sprites and body table */
            const char *nameA = "unknown";
            const char *nameB = "unknown";
            
            for (int j = 0; j < tm->sprite_count; j++) {
                if (tm->sprites[j].has_body) {
                    if (tm->sprites[j].body.index1 == bodyA.index1)
                        nameA = tm->sprites[j].name;
                    if (tm->sprites[j].body.index1 == bodyB.index1)
                        nameB = tm->sprites[j].name;
                }
            }
            
            /* Call the Tcl callback: callback bodyA bodyB */
            char script[512];
            snprintf(script, sizeof(script), "%s {%s} {%s}",
                     tm->collision_callback, nameA, nameB);
            Tcl_Eval(tm->interp, script);
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
    tm->tile_size = 1.0f;
    tm->pixels_per_meter = 32.0f;
    tm->gravity = (b2Vec2){0, -10};
    tm->substep_count = 4;
    tm->auto_center = 1;  /* default to auto-center */
    tm->collision_callback[0] = '\0';
    Tcl_InitHashTable(&tm->body_table, TCL_STRING_KEYS);
    if (tilemap_init_gl(tm) < 0) { free(tm); return TCL_ERROR; }
    GR_CLIENTDATA(obj) = tm;
    GR_ACTIONFUNCP(obj) = tilemap_draw;
    GR_UPDATEFUNCP(obj) = (UPDATE_FUNC)tilemap_update;
    GR_DELETEFUNCP(obj) = tilemap_delete;
    GR_RESETFUNCP(obj) = (RESET_FUNC)tilemap_reset;
    Tcl_SetObjResult(interp, Tcl_NewIntObj(gobjAddObj(olist, obj)));
    return TCL_OK;
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
    for (int i = 3; i < argc - 1; i += 2) {
        if (strcmp(argv[i], "-pixels_per_meter") == 0) {
            double d; Tcl_GetDouble(interp, argv[i+1], &d); ppm = (float)d;
        } else if (strcmp(argv[i], "-collision_layer") == 0) {
            collision_layer = argv[i+1];
        }
    }
    tm->pixels_per_meter = ppm;
    get_directory(argv[2], tm->base_path, MAX_PATH_LEN);
    
    void *doc = tmx_xml_load(argv[2]);
    if (!doc) { Tcl_AppendResult(interp, "can't load ", argv[2], NULL); return TCL_ERROR; }
    void *map = tmx_xml_get_map(doc);
    if (!map) { tmx_xml_free(doc); Tcl_AppendResult(interp, "no map element", NULL); return TCL_ERROR; }
    
    tm->map_width = tmx_xml_map_get_int(map, "width");
    tm->map_height = tmx_xml_map_get_int(map, "height");
    tm->tile_pixel_width = tmx_xml_map_get_int(map, "tilewidth");
    tm->tile_pixel_height = tmx_xml_map_get_int(map, "tileheight");
    tm->tile_size = tm->tile_pixel_width / ppm;
    
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
        /* tmx_xml_tileset_get_string handles nested <image source="..."> */
        const char *src = tmx_xml_tileset_get_string(ts, "source");
        if (src) {
            if (load_atlas(tm, src, tw, th, firstgid) < 0) {
                fprintf(stderr, "tilemap: failed to load atlas '%s'\n", src);
            }
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
        if (!enc || strcmp(enc, "csv") != 0) continue;
        
        int *tiles = parse_csv(tmx_xml_data_get_text(data), lw, lh);
        if (!tiles) continue;
        
        for (int ty = 0; ty < lh; ty++) {
            for (int tx = 0; tx < lw; tx++) {
                int gid = tiles[ty * lw + tx];
                if (gid == 0 || tm->tile_count >= MAX_TILES) continue;
                ATLAS *atlas = find_atlas_for_gid(tm, gid);
                if (!atlas) continue;
                
                TILE_INSTANCE *t = &tm->tiles[tm->tile_count++];
                float px = (tx + 0.5f) * tm->tile_pixel_width;
                float py = (ty + 0.5f) * tm->tile_pixel_height;
                t->x = px / ppm;
                t->y = (tm->map_height * tm->tile_pixel_height - py) / ppm;
                t->w = t->h = tm->tile_size;
                t->atlas_id = (int)(atlas - tm->atlases);
                get_tile_uvs(atlas, gid, &t->u0, &t->v0, &t->u1, &t->v1);
                
                if (is_collision) {
                    char bname[64];
                    snprintf(bname, sizeof(bname), "tile_%d_%d", tx, ty);
                    b2BodyDef bd = b2DefaultBodyDef();
                    bd.type = b2_staticBody;
                    bd.position = (b2Vec2){t->x, t->y};
                    b2BodyId body = b2CreateBody(tm->world_id, &bd);
                    b2Polygon box = b2MakeBox(t->w * 0.5f, t->h * 0.5f);
                    b2ShapeDef sd = b2DefaultShapeDef();
                    sd.density = 1.0f;
                    b2ShapeId shape = b2CreatePolygonShape(body, &sd, &box);
                    b2Shape_SetFriction(shape, 0.3f);
                    t->has_body = 1;
                    
                    int newentry;
                    Tcl_HashEntry *e = Tcl_CreateHashEntry(&tm->body_table, bname, &newentry);
                    b2BodyId *stored = malloc(sizeof(b2BodyId));
                    *stored = body;
                    Tcl_SetHashValue(e, stored);
                    tm->body_count++;
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
            strncpy(to->name, n ? n : "", 63);
            strncpy(to->type, t ? t : "", 63);
            float px = tmx_xml_object_get_float(obj, "x", 0);
            float py = tmx_xml_object_get_float(obj, "y", 0);
            to->width = tmx_xml_object_get_float(obj, "width", 0);
            to->height = tmx_xml_object_get_float(obj, "height", 0);
            to->x = px / ppm;
            to->y = (tm->map_height * tm->tile_pixel_height - py) / ppm;
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
                    strncpy(p->value, pv ? pv : "", 63);
                    strncpy(p->type, pt ? pt : "string", 15);
                }
            }
        }
    }
    
    tmx_xml_free(doc);
    tm->tiles_dirty = 1;
    
    /* Auto-center the map */
    if (tm->auto_center) {
        float ox = -(tm->map_width * tm->tile_size) / 2.0f;
        float oy = -(tm->map_height * tm->tile_size) / 2.0f;
        tm->offset_x = ox;
        tm->offset_y = oy;
        /* Offset all tiles */
        for (int i = 0; i < tm->tile_count; i++) {
            tm->tiles[i].x += ox;
            tm->tiles[i].y += oy;
        }
        /* Offset all static bodies */
        Tcl_HashEntry *e; Tcl_HashSearch s;
        for (e = Tcl_FirstHashEntry(&tm->body_table, &s); e; e = Tcl_NextHashEntry(&s)) {
            b2BodyId *body = Tcl_GetHashValue(e);
            b2Vec2 pos = b2Body_GetPosition(*body);
            pos.x += ox;
            pos.y += oy;
            b2Body_SetTransform(*body, pos, b2Body_GetRotation(*body));
        }
        /* Offset objects */
        for (int i = 0; i < tm->object_count; i++) {
            tm->objects[i].x += ox;
            tm->objects[i].y += oy;
        }
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
    
    if (atlas_id < tm->atlas_count) {
        ATLAS *a = &tm->atlases[atlas_id];
        get_tile_uvs(a, tile_id + a->firstgid, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(tm->sprite_count++));
    return TCL_OK;
}

static int tilemapSpriteAddBodyCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) { 
        Tcl_AppendResult(interp, "usage: ", argv[0], 
            " tm sprite ?type? ?-fixedrotation 0/1? ?-damping N? ?-friction N? ?-density N?", NULL); 
        return TCL_ERROR; 
    }
    int id, sid;
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != TilemapID) return TCL_ERROR;
    TILEMAP *tm = (TILEMAP *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (!tm->has_world) { Tcl_AppendResult(interp, "no physics world", NULL); return TCL_ERROR; }
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= tm->sprite_count) return TCL_ERROR;
    
    /* Defaults - tuned for top-down feel */
    const char *type_str = "dynamic";
    int fixed_rotation = 1;      /* lock rotation by default */
    double damping = 5.0;        /* friction-like slowdown */
    double friction = 0.5;       /* surface friction */
    double density = 1.0;
    
    /* Parse arguments */
    int i = 3;
    if (argc > 3 && argv[3][0] != '-') {
        type_str = argv[3];
        i = 4;
    }
    for (; i < argc - 1; i += 2) {
        if (strcmp(argv[i], "-fixedrotation") == 0) {
            Tcl_GetInt(interp, argv[i+1], &fixed_rotation);
        } else if (strcmp(argv[i], "-damping") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &damping);
        } else if (strcmp(argv[i], "-friction") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &friction);
        } else if (strcmp(argv[i], "-density") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &density);
        }
    }
    
    b2BodyType bt = b2_dynamicBody;
    if (strcmp(type_str, "static") == 0) bt = b2_staticBody;
    else if (strcmp(type_str, "kinematic") == 0) bt = b2_kinematicBody;
    
    SPRITE *sp = &tm->sprites[sid];
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = bt;
    bd.position = (b2Vec2){sp->x, sp->y};
    bd.linearDamping = (float)damping;
    bd.angularDamping = 0.05f;
    bd.motionLocks.angularZ = fixed_rotation ? true : false;
    
    sp->body = b2CreateBody(tm->world_id, &bd);
    
    b2Polygon box = b2MakeBox(sp->w * 0.5f, sp->h * 0.5f);
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = (float)density;
    b2ShapeId shape = b2CreatePolygonShape(sp->body, &sd, &box);
    b2Shape_SetFriction(shape, (float)friction);
    sp->has_body = 1;
    
    int newentry;
    Tcl_HashEntry *e = Tcl_CreateHashEntry(&tm->body_table, sp->name, &newentry);
    b2BodyId *stored = malloc(sizeof(b2BodyId));
    *stored = sp->body;
    Tcl_SetHashValue(e, stored);
    tm->body_count++;
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
        get_tile_uvs(a, tile_id + a->firstgid, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
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
    Tcl_CreateCommand(interp, "tilemapCreateSprite", (Tcl_CmdProc*)tilemapCreateSpriteCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSpriteAddBody", (Tcl_CmdProc*)tilemapSpriteAddBodyCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetSpritePosition", (Tcl_CmdProc*)tilemapSetSpritePositionCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetSpriteTile", (Tcl_CmdProc*)tilemapSetSpriteTileCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapGetObjects", (Tcl_CmdProc*)tilemapGetObjectsCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapGetContacts", (Tcl_CmdProc*)tilemapGetContactsCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapGetSpriteInfo", (Tcl_CmdProc*)tilemapGetSpriteInfoCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetOffset", (Tcl_CmdProc*)tilemapSetOffsetCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapGetMapInfo", (Tcl_CmdProc*)tilemapGetMapInfoCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapApplyImpulse", (Tcl_CmdProc*)tilemapApplyImpulseCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetLinearVelocity", (Tcl_CmdProc*)tilemapSetLinearVelocityCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapApplyForce", (Tcl_CmdProc*)tilemapApplyForceCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetSpriteAnimation", (Tcl_CmdProc*)tilemapSetSpriteAnimationCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapPlayAnimation", (Tcl_CmdProc*)tilemapPlayAnimationCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetCollisionCallback", (Tcl_CmdProc*)tilemapSetCollisionCallbackCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "tilemapSetAutoCenter", (Tcl_CmdProc*)tilemapSetAutoCenterCmd, (ClientData)OBJList, NULL);
    
    return TCL_OK;
}

#ifdef WIN32
BOOL APIENTRY DllEntryPoint(HINSTANCE hInst, DWORD reason, LPVOID reserved) { return TRUE; }
#endif