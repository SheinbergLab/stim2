/*
 * world.c
 *
 * 2D World module - main orchestrator.
 * Handles world creation, update loop, physics stepping, and collision callbacks.
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

#include "world_internal.h"

int WorldID = -1;

/*========================================================================
 * Utility Functions (shared with submodules)
 *========================================================================*/

void world_get_directory(const char *path, char *dir, int max)
{
    strncpy(dir, path, max - 1);
    dir[max - 1] = '\0';
    char *s = strrchr(dir, '/'), *b = strrchr(dir, '\\');
    char *last = s > b ? s : b;
    if (last) *(last + 1) = '\0';
    else dir[0] = '\0';
}

void world_join_path(char *dest, int max, const char *dir, const char *file)
{
    if (dir[0] && file[0] != '/' && file[0] != '\\')
        snprintf(dest, max, "%s%s", dir, file);
    else {
        strncpy(dest, file, max - 1);
        dest[max - 1] = '\0';
    }
}

/*========================================================================
 * GR_OBJ Callbacks
 *========================================================================*/

static void world_draw_callback(GR_OBJ *obj)
{
  World *w = (World *)GR_CLIENTDATA(obj);
  if (maze3d_is_enabled(w->maze3d)) {
    maze3d_render(w, w->maze3d);
  } else {
    world_render(w);
    maze3d_render_2d_marker(w, w->maze3d);
  }
}

static const char* world_find_name_from_body(World *w, b2BodyId bodyId)
{
    if (bodyId.index1 == 0) return "invalid";

    for (int i = 0; i < w->sprite_count; i++) {
        if (w->sprites[i].has_body) {
            b2BodyId spriteBody = w->sprites[i].body;
            if (spriteBody.index1 == bodyId.index1 &&
                spriteBody.generation == bodyId.generation) {
                return w->sprites[i].name;
            }
        }
    }

    Tcl_HashEntry *e;
    Tcl_HashSearch s;
    for (e = Tcl_FirstHashEntry(&w->body_table, &s); e; e = Tcl_NextHashEntry(&s)) {
        b2BodyId *stored = (b2BodyId *)Tcl_GetHashValue(e);
        if (stored->index1 == bodyId.index1 &&
            stored->generation == bodyId.generation) {
            return Tcl_GetHashKey(&w->body_table, e);
        }
    }

    return "unknown";
}

static void world_update_callback(GR_OBJ *obj)
{
    World *w = (World *)GR_CLIENTDATA(obj);
    if (!w->has_world) return;

    float dt = getFrameDuration() / 1000.0f;
    if (dt > 0.1f) dt = 0.016f;

    world_camera_update(w, dt);
    b2World_Step(w->world_id, dt, w->substep_count);

    for (int i = 0; i < w->sprite_count; i++) {
        Sprite *sp = &w->sprites[i];
        world_sprite_sync_physics(w, sp);
        world_sprite_update_animation(w, sp, dt);
    }

    /* Sync maze camera from physics body + update 2D camera position */
    if (w->maze3d) {
      maze3d_sync_camera(w, w->maze3d);
      maze3d_update_items(w, w->maze3d, dt);
    }
    
    if (w->collision_callback[0] != '\0') {
        b2ContactEvents ev = b2World_GetContactEvents(w->world_id);
        for (int i = 0; i < ev.beginCount; i++) {
            const char *nameA = (const char*)b2Shape_GetUserData(ev.beginEvents[i].shapeIdA);
            const char *nameB = (const char*)b2Shape_GetUserData(ev.beginEvents[i].shapeIdB);
            if (!nameA) nameA = world_find_name_from_body(w, b2Shape_GetBody(ev.beginEvents[i].shapeIdA));
            if (!nameB) nameB = world_find_name_from_body(w, b2Shape_GetBody(ev.beginEvents[i].shapeIdB));

            char script[512];
            snprintf(script, sizeof(script), "%s {%s} {%s}", w->collision_callback, nameA, nameB);
            if (Tcl_Eval(w->interp, script) != TCL_OK) {
                fprintf(stderr, "Collision callback error: %s\n", Tcl_GetStringResult(w->interp));
            }
        }

        b2SensorEvents sev = b2World_GetSensorEvents(w->world_id);
        for (int i = 0; i < sev.beginCount; i++) {
            const char *sensorName = (const char*)b2Shape_GetUserData(sev.beginEvents[i].sensorShapeId);
            const char *visitorName = (const char*)b2Shape_GetUserData(sev.beginEvents[i].visitorShapeId);
            if (!visitorName) {
                visitorName = world_find_name_from_body(w, b2Shape_GetBody(sev.beginEvents[i].visitorShapeId));
            }
            if (sensorName && visitorName) {
                char script[512];
                snprintf(script, sizeof(script), "%s {%s} {%s}", w->collision_callback, visitorName, sensorName);
                if (Tcl_Eval(w->interp, script) != TCL_OK) {
                    fprintf(stderr, "Sensor callback error: %s\n", Tcl_GetStringResult(w->interp));
                }
            }
        }
    }
}

static void world_delete_callback(GR_OBJ *obj)
{
    World *w = (World *)GR_CLIENTDATA(obj);
    if (w->vao) glDeleteVertexArrays(1, &w->vao);
    if (w->vbo) glDeleteBuffers(1, &w->vbo);
    if (w->sprite_vao) glDeleteVertexArrays(1, &w->sprite_vao);
    if (w->sprite_vbo) glDeleteBuffers(1, &w->sprite_vbo);
    if (w->shader_program) glDeleteProgram(w->shader_program);

    if (w->maze3d) maze3d_destroy(w->maze3d);
 
    for (int i = 0; i < w->atlas_count; i++)
        if (w->atlases[i].texture) glDeleteTextures(1, &w->atlases[i].texture);
    /* Clean up sprite sheet frame name hash tables */
    for (int i = 0; i < w->sprite_sheet_count; i++) {
        if (w->sprite_sheets[i].frame_names_init)
            Tcl_DeleteHashTable(&w->sprite_sheets[i].frame_names);
    }
    if (w->has_world) b2DestroyWorld(w->world_id);
    Tcl_HashEntry *e;
    Tcl_HashSearch s;
    for (e = Tcl_FirstHashEntry(&w->body_table, &s); e; e = Tcl_NextHashEntry(&s))
        free(Tcl_GetHashValue(e));
    Tcl_DeleteHashTable(&w->body_table);
    free(w);
}

static int world_reset_callback(GR_OBJ *obj) { return TCL_OK; }

/*========================================================================
 * Point Query
 *========================================================================*/

typedef struct {
    int hit;
    b2BodyId ignore_body;
    int use_ignore;
} PointQueryContext;

static bool world_point_query_callback(b2ShapeId shapeId, void *context)
{
    PointQueryContext *ctx = (PointQueryContext *)context;
    if (ctx->use_ignore) {
        b2BodyId body = b2Shape_GetBody(shapeId);
        if (body.index1 == ctx->ignore_body.index1) return true;
    }
    ctx->hit = 1;
    return false;
}

/*========================================================================
 * Tcl Commands
 *========================================================================*/

static int worldCreateCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    GR_OBJ *obj = gobjCreateObj();
    if (!obj) { Tcl_SetResult(interp, "create failed", TCL_STATIC); return TCL_ERROR; }

    GR_OBJTYPE(obj) = WorldID;
    strcpy(GR_NAME(obj), "World");

    World *w = (World *)calloc(1, sizeof(World));
    w->interp = interp;
    w->tile_size = 1.0f;
    w->pixels_per_meter = 32.0f;
    w->gravity = (b2Vec2){0, -10};
    w->substep_count = 4;
    w->auto_center = 1;
    w->maze3d = NULL;
    
    Tcl_InitHashTable(&w->body_table, TCL_STRING_KEYS);
    world_camera_init(&w->camera);

    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = w->gravity;
    w->world_id = b2CreateWorld(&worldDef);
    w->has_world = 1;

    if (world_init_gl(w) < 0) { free(w); return TCL_ERROR; }

    GR_CLIENTDATA(obj) = w;
    GR_ACTIONFUNCP(obj) = world_draw_callback;
    GR_UPDATEFUNCP(obj) = (UPDATE_FUNC)world_update_callback;
    GR_DELETEFUNCP(obj) = (RESET_FUNC)world_delete_callback;
    GR_RESETFUNCP(obj) = (RESET_FUNC)world_reset_callback;

    Tcl_SetObjResult(interp, Tcl_NewIntObj(gobjAddObj(olist, obj)));
    return TCL_OK;
}

static int worldSetGravityCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) { Tcl_AppendResult(interp, "usage: ", argv[0], " world gx gy", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    double gx, gy;
    if (Tcl_GetDouble(interp, argv[2], &gx) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &gy) != TCL_OK) return TCL_ERROR;
    w->gravity = (b2Vec2){(float)gx, (float)gy};
    if (w->has_world) b2World_SetGravity(w->world_id, w->gravity);
    return TCL_OK;
}

static int worldGetContactsCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) { Tcl_AppendResult(interp, "usage: ", argv[0], " world", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (!w->has_world) { Tcl_SetResult(interp, "", TCL_STATIC); return TCL_OK; }

    b2ContactEvents ev = b2World_GetContactEvents(w->world_id);
    Tcl_Obj *result = Tcl_NewDictObj();
    Tcl_Obj *begins = Tcl_NewListObj(0, NULL);
    for (int i = 0; i < ev.beginCount; i++) {
        Tcl_Obj *pair = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, pair, Tcl_NewIntObj(ev.beginEvents[i].shapeIdA.index1));
        Tcl_ListObjAppendElement(interp, pair, Tcl_NewIntObj(ev.beginEvents[i].shapeIdB.index1));
        Tcl_ListObjAppendElement(interp, begins, pair);
    }
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("begin", -1), begins);
    Tcl_Obj *ends = Tcl_NewListObj(0, NULL);
    for (int i = 0; i < ev.endCount; i++) {
        Tcl_Obj *pair = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, pair, Tcl_NewIntObj(ev.endEvents[i].shapeIdA.index1));
        Tcl_ListObjAppendElement(interp, pair, Tcl_NewIntObj(ev.endEvents[i].shapeIdB.index1));
        Tcl_ListObjAppendElement(interp, ends, pair);
    }
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("end", -1), ends);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

static int worldSetCollisionCallbackCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) { Tcl_AppendResult(interp, "usage: ", argv[0], " world callback", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));

    strncpy(w->collision_callback, argv[2], sizeof(w->collision_callback) - 1);
    for (int i = 0; i < w->sprite_count; i++) {
        if (w->sprites[i].has_body && b2Body_IsValid(w->sprites[i].body)) {
            int n = b2Body_GetShapeCount(w->sprites[i].body);
            b2ShapeId shapes[WORLD_MAX_SHAPES_PER_BODY];
            b2Body_GetShapes(w->sprites[i].body, shapes, n > WORLD_MAX_SHAPES_PER_BODY ? WORLD_MAX_SHAPES_PER_BODY : n);
            for (int j = 0; j < n && j < WORLD_MAX_SHAPES_PER_BODY; j++)
                b2Shape_EnableContactEvents(shapes[j], true);
        }
    }
    return TCL_OK;
}

static int worldSetAutoCenterCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) { Tcl_AppendResult(interp, "usage: ", argv[0], " world 0/1", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    int enabled;
    if (Tcl_GetInt(interp, argv[2], &enabled) != TCL_OK) return TCL_ERROR;
    w->auto_center = enabled;
    return TCL_OK;
}

static int worldQueryPointCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) { Tcl_AppendResult(interp, "usage: ", argv[0], " world x y ?-ignore sid?", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (!w->has_world) { Tcl_SetObjResult(interp, Tcl_NewIntObj(0)); return TCL_OK; }
    
    double x, y;
    if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;

    PointQueryContext ctx = {0, {0}, 0};
    for (int i = 4; i < argc - 1; i++) {
        if (strcmp(argv[i], "-ignore") == 0) {
            int sid; Tcl_GetInt(interp, argv[i+1], &sid);
            if (sid >= 0 && sid < w->sprite_count && w->sprites[sid].has_body) {
                ctx.ignore_body = w->sprites[sid].body; ctx.use_ignore = 1;
            }
        }
    }

    float eps = 0.01f;
    b2AABB aabb = {{(float)x - eps, (float)y - eps}, {(float)x + eps, (float)y + eps}};
    b2World_OverlapAABB(w->world_id, aabb, b2DefaultQueryFilter(), world_point_query_callback, &ctx);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(ctx.hit));
    return TCL_OK;
}

static int worldQueryAABBCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 6) { Tcl_AppendResult(interp, "usage: ", argv[0], " world x1 y1 x2 y2 ?-ignore sid?", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (!w->has_world) { Tcl_SetObjResult(interp, Tcl_NewIntObj(0)); return TCL_OK; }
    
    double x1, y1, x2, y2;
    if (Tcl_GetDouble(interp, argv[2], &x1) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &y1) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &x2) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &y2) != TCL_OK) return TCL_ERROR;

    PointQueryContext ctx = {0, {0}, 0};
    for (int i = 6; i < argc - 1; i++) {
        if (strcmp(argv[i], "-ignore") == 0) {
            int sid; Tcl_GetInt(interp, argv[i+1], &sid);
            if (sid >= 0 && sid < w->sprite_count && w->sprites[sid].has_body) {
                ctx.ignore_body = w->sprites[sid].body; ctx.use_ignore = 1;
            }
        }
    }

    b2AABB aabb = {
        {(float)(x1<x2?x1:x2), (float)(y1<y2?y1:y2)},
        {(float)(x1>x2?x1:x2), (float)(y1>y2?y1:y2)}
    };
    b2World_OverlapAABB(w->world_id, aabb, b2DefaultQueryFilter(), world_point_query_callback, &ctx);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(ctx.hit));
    return TCL_OK;
}

/*========================================================================
 * Module Init
 *========================================================================*/

#ifdef _WIN32
EXPORT(int, World_Init)(Tcl_Interp *interp)
#else
int World_Init(Tcl_Interp *interp)
#endif
{
    OBJ_LIST *OBJList = getOBJList();
#ifdef USE_TCL_STUBS
    if (Tcl_InitStubs(interp, "8.5-", 0) == NULL) return TCL_ERROR;
#else
    if (Tcl_PkgRequire(interp, "Tcl", "8.5-", 0) == NULL) return TCL_ERROR;
#endif
    if (WorldID < 0) { WorldID = gobjRegisterType(); gladLoadGL(); }

    Tcl_CreateCommand(interp, "worldCreate", (Tcl_CmdProc*)worldCreateCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "worldSetGravity", (Tcl_CmdProc*)worldSetGravityCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "worldGetContacts", (Tcl_CmdProc*)worldGetContactsCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "worldSetCollisionCallback", (Tcl_CmdProc*)worldSetCollisionCallbackCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "worldSetAutoCenter", (Tcl_CmdProc*)worldSetAutoCenterCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "worldQueryPoint", (Tcl_CmdProc*)worldQueryPointCmd, (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "worldQueryAABB", (Tcl_CmdProc*)worldQueryAABBCmd, (ClientData)OBJList, NULL);
    
    world_camera_register_commands(interp, OBJList);
    world_sprite_register_commands(interp, OBJList);
    world_spritesheet_register_commands(interp, OBJList);
    world_tilemap_register_commands(interp, OBJList);
    world_maze3d_register_commands(interp, OBJList);
 
    return TCL_OK;
}

#ifdef WIN32
BOOL APIENTRY DllEntryPoint(HINSTANCE hInst, DWORD reason, LPVOID reserved) { return TRUE; }
#endif
