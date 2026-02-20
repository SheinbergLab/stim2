/*
 * world_sprite.c
 *
 * Sprite instance management for 2D world module.
 * Handles sprite creation, physics bodies, animation, and runtime manipulation.
 */

#include "world_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*========================================================================
 * Collision Shape Creation
 *========================================================================*/

static void create_sprite_collision_shapes(World *w, Sprite *sp, 
                                           TileCollision *tc,
                                           float friction, float restitution,
                                           float density, int is_sensor)
{
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = density;
    sd.userData = (void *)sp->name;
    sd.isSensor = is_sensor ? true : false;
    sd.enableContactEvents = !is_sensor;
    sd.enableSensorEvents = true;
    
    float sprite_w, sprite_h;
    
    if (sp->uses_sprite_sheet) {
        SpriteSheet *ss = &w->sprite_sheets[sp->sprite_sheet_id];
        /* Use frame dimensions - collision data was normalized against these */
        sprite_w = ss->frames[sp->current_frame].w / w->pixels_per_meter;
        sprite_h = ss->frames[sp->current_frame].h / w->pixels_per_meter;
    } else {
        sprite_w = sp->w;
        sprite_h = sp->h;
    }
    
    for (int i = 0; i < tc->shape_count; i++) {
        CollisionShape *cs = &tc->shapes[i];
        b2ShapeId shape;
        
        if (cs->type == SHAPE_POLYGON) {
            b2Vec2 points[WORLD_MAX_COLLISION_VERTS];
            for (int v = 0; v < cs->vert_count; v++) {
                float nx = cs->verts_x[v] - 0.5f;
                float ny = 0.5f - cs->verts_y[v];
                points[v].x = nx * sprite_w;
                points[v].y = ny * sprite_h;
            }
            b2Hull hull = b2ComputeHull(points, cs->vert_count);
            b2Polygon poly = b2MakePolygon(&hull, 0.0f);
            shape = b2CreatePolygonShape(sp->body, &sd, &poly);
        } else if (cs->type == SHAPE_BOX) {
            float cx = (cs->box_x + cs->box_w * 0.5f - 0.5f) * sprite_w;
            float cy = (0.5f - (cs->box_y + cs->box_h * 0.5f)) * sprite_h;
            float hw = cs->box_w * sprite_w * 0.5f;
            float hh = cs->box_h * sprite_h * 0.5f;
            b2Polygon box = b2MakeOffsetBox(hw, hh, (b2Vec2){cx, cy}, b2Rot_identity);
            shape = b2CreatePolygonShape(sp->body, &sd, &box);
        } else if (cs->type == SHAPE_CIRCLE) {
            float cx = (cs->circle_x - 0.5f) * sprite_w;
            float cy = (0.5f - cs->circle_y) * sprite_h;
            float radius = cs->circle_radius * sprite_w;
            b2Circle circle = { .center = {cx, cy}, .radius = radius };
            shape = b2CreateCircleShape(sp->body, &sd, &circle);
        } else {
            continue;
        }
        b2Shape_SetFriction(shape, friction);
        b2Shape_SetRestitution(shape, restitution);
    }
}

/*========================================================================
 * Animation Update (called from world update loop)
 *========================================================================*/

void world_sprite_update_animation(World *w, Sprite *sp, float dt)
{
    if (!sp->anim_playing || sp->anim_frame_count == 0) return;
    
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
        
        if (sp->uses_sprite_sheet) {
            sp->current_frame = sp->anim_frames[sp->anim_current_frame];
        } else {
            sp->tile_id = sp->anim_frames[sp->anim_current_frame];
            if (sp->atlas_id < w->atlas_count) {
                Atlas *a = &w->atlases[sp->atlas_id];
		if (a->cols > 0) { /* only grid atlases */
		  world_get_tile_uvs(a, sp->tile_id, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
		}
            }
        }
    }
}

/*========================================================================
 * Physics Sync (called from world update loop)
 *========================================================================*/

void world_sprite_sync_physics(World *w, Sprite *sp)
{
    (void)w;
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Vec2 pos = b2Body_GetPosition(sp->body);
        sp->x = pos.x - sp->body_offset_x;
        sp->y = pos.y - sp->body_offset_y;
        sp->angle = b2Rot_GetAngle(b2Body_GetRotation(sp->body));
    }
}

/*========================================================================
 * Tcl Commands - Creation/Deletion
 *========================================================================*/

static int worldCreateSpriteCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 8) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world name tile_id x y w h ?atlas?", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (w->sprite_count >= WORLD_MAX_SPRITES) {
        Tcl_AppendResult(interp, "max sprites", NULL);
        return TCL_ERROR;
    }
    
    int tile_id, atlas_id = 0;
    double x, y, width, height;
    if (Tcl_GetInt(interp, argv[3], &tile_id) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &y) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[6], &width) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[7], &height) != TCL_OK) return TCL_ERROR;
    if (argc > 8) Tcl_GetInt(interp, argv[8], &atlas_id);
    
    Sprite *sp = &w->sprites[w->sprite_count];
    memset(sp, 0, sizeof(Sprite));
    strncpy(sp->name, argv[2], 63);
    sp->x = (float)x; sp->y = (float)y;
    sp->w = (float)width; sp->h = (float)height;
    sp->tile_id = tile_id; sp->atlas_id = atlas_id;
    sp->visible = 1;
    
    if (atlas_id < w->atlas_count) {
        Atlas *a = &w->atlases[atlas_id];
	if (a->cols > 0)  // only grid atlases
	  world_get_tile_uvs(a, tile_id, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(w->sprite_count++));
    return TCL_OK;
}

static int worldRemoveSpriteCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) { Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite_id", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    
    Sprite *sp = &w->sprites[sid];
    if (sp->has_body && b2Body_IsValid(sp->body)) b2DestroyBody(sp->body);
    for (int i = sid; i < w->sprite_count - 1; i++) w->sprites[i] = w->sprites[i + 1];
    w->sprite_count--;
    return TCL_OK;
}

/*========================================================================
 * Tcl Commands - Physics Body
 *========================================================================*/

static int worldSpriteAddBodyCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite ?options...?", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    Sprite *sp = &w->sprites[sid];
    
    b2BodyType btype = b2_dynamicBody;
    float density = 1.0f, friction = 0.3f, restitution = 0.0f, gravityScale = 1.0f;
    int fixedRotation = 0, bullet = 0, is_sensor = 0;
    
    for (int i = 3; i < argc - 1; i++) {
        if (strcmp(argv[i], "-type") == 0) {
            if (strcmp(argv[i+1], "static") == 0) btype = b2_staticBody;
            else if (strcmp(argv[i+1], "kinematic") == 0) btype = b2_kinematicBody;
        } else if (strcmp(argv[i], "-density") == 0) { double d; Tcl_GetDouble(interp, argv[i+1], &d); density = (float)d; }
        else if (strcmp(argv[i], "-friction") == 0) { double d; Tcl_GetDouble(interp, argv[i+1], &d); friction = (float)d; }
        else if (strcmp(argv[i], "-restitution") == 0) { double d; Tcl_GetDouble(interp, argv[i+1], &d); restitution = (float)d; }
        else if (strcmp(argv[i], "-fixedRotation") == 0) Tcl_GetInt(interp, argv[i+1], &fixedRotation);
        else if (strcmp(argv[i], "-bullet") == 0) Tcl_GetInt(interp, argv[i+1], &bullet);
        else if (strcmp(argv[i], "-sensor") == 0) Tcl_GetInt(interp, argv[i+1], &is_sensor);
        else if (strcmp(argv[i], "-gravityScale") == 0) { double d; Tcl_GetDouble(interp, argv[i+1], &d); gravityScale = (float)d; }
    }
    
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = btype;
    bd.position = (b2Vec2){sp->x, sp->y};
    bd.motionLocks.angularZ = fixedRotation ? true : false;
    bd.isBullet = bullet ? true : false;
    bd.gravityScale = gravityScale;
    
    sp->body = b2CreateBody(w->world_id, &bd);
    sp->has_body = 1;
    
    /* Get collision data - different lookup for sprite sheet vs tile sprites */
    TileCollision *tc = NULL;
    if (sp->uses_sprite_sheet) {
        SpriteSheet *ss = &w->sprite_sheets[sp->sprite_sheet_id];
        tc = &ss->frame_collisions[sp->current_frame];
        if (tc->shape_count == 0) tc = NULL;
    } else {
        tc = world_get_tile_collision(w, sp->tile_id);
    }
    
    if (tc && tc->shape_count > 0) {
        create_sprite_collision_shapes(w, sp, tc, friction, restitution, density, is_sensor);
    } else {
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.density = density;
        sd.userData = (void *)sp->name;
        sd.isSensor = is_sensor ? true : false;
        sd.enableContactEvents = !is_sensor;
        sd.enableSensorEvents = true;
        
        float hw = sp->w * 0.5f, hh = sp->h * 0.5f;
        if (sp->has_hitbox_data) { hw = sp->w * sp->hitbox_w_ratio * 0.5f; hh = sp->h * sp->hitbox_h_ratio * 0.5f; }
        b2Polygon box = b2MakeBox(hw, hh);
        b2ShapeId shape = b2CreatePolygonShape(sp->body, &sd, &box);
        b2Shape_SetFriction(shape, friction);
        b2Shape_SetRestitution(shape, restitution);
    }
    return TCL_OK;
}

/*========================================================================
 * Tcl Commands - Position/Rotation/Visibility
 *========================================================================*/

static int worldSetSpritePositionCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) { Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite x y", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid;
    double x, y;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &y) != TCL_OK) return TCL_ERROR;
    
    Sprite *sp = &w->sprites[sid];
    sp->x = (float)x; sp->y = (float)y;
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Body_SetTransform(sp->body, (b2Vec2){sp->x + sp->body_offset_x, sp->y + sp->body_offset_y}, b2Body_GetRotation(sp->body));
    }
    return TCL_OK;
}

static int worldSetSpriteRotationCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) { Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite angle", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid;
    double angle;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &angle) != TCL_OK) return TCL_ERROR;
    
    Sprite *sp = &w->sprites[sid];
    sp->angle = (float)angle;
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Body_SetTransform(sp->body, b2Body_GetPosition(sp->body), b2MakeRot((float)angle));
    }
    return TCL_OK;
}

static int worldSetSpriteVisibleCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) { Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite visible", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid, visible;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    if (Tcl_GetInt(interp, argv[3], &visible) != TCL_OK) return TCL_ERROR;
    w->sprites[sid].visible = visible;
    return TCL_OK;
}

static int worldSetSpriteTileCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) { Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite tile_id", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid, tile_id;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    if (Tcl_GetInt(interp, argv[3], &tile_id) != TCL_OK) return TCL_ERROR;
    
    Sprite *sp = &w->sprites[sid];
    sp->tile_id = tile_id;
    if (sp->atlas_id < w->atlas_count) {
      if (w->atlases[sp->atlas_id].cols > 0)
        world_get_tile_uvs(&w->atlases[sp->atlas_id], tile_id, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
    }
    return TCL_OK;
}

/*========================================================================
 * Tcl Commands - Query
 *========================================================================*/

static int worldGetSpriteCountCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) { Tcl_AppendResult(interp, "usage: ", argv[0], " world", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Tcl_SetObjResult(interp, Tcl_NewIntObj(w->sprite_count));
    return TCL_OK;
}

static int worldGetSpriteByNameCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) { Tcl_AppendResult(interp, "usage: ", argv[0], " world name", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    for (int i = 0; i < w->sprite_count; i++) {
        if (strcmp(w->sprites[i].name, argv[2]) == 0) {
            Tcl_SetObjResult(interp, Tcl_NewIntObj(i));
            return TCL_OK;
        }
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(-1));
    return TCL_OK;
}

static int worldGetSpriteInfoCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) { Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    
    Sprite *sp = &w->sprites[sid];
    Tcl_Obj *dict = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("name", -1), Tcl_NewStringObj(sp->name, -1));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("x", -1), Tcl_NewDoubleObj(sp->x));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("y", -1), Tcl_NewDoubleObj(sp->y));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("w", -1), Tcl_NewDoubleObj(sp->w));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("h", -1), Tcl_NewDoubleObj(sp->h));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("angle", -1), Tcl_NewDoubleObj(sp->angle));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("visible", -1), Tcl_NewIntObj(sp->visible));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("has_body", -1), Tcl_NewIntObj(sp->has_body));
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Vec2 vel = b2Body_GetLinearVelocity(sp->body);
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("vx", -1), Tcl_NewDoubleObj(vel.x));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("vy", -1), Tcl_NewDoubleObj(vel.y));
    }
    Tcl_SetObjResult(interp, dict);
    return TCL_OK;
}

/*========================================================================
 * Tcl Commands - Physics Forces
 *========================================================================*/

static int worldApplyImpulseCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) { Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite ix iy", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid;
    double ix, iy;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &ix) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &iy) != TCL_OK) return TCL_ERROR;
    Sprite *sp = &w->sprites[sid];
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Body_ApplyLinearImpulseToCenter(sp->body, (b2Vec2){(float)ix, (float)iy}, true);
    }
    return TCL_OK;
}

static int worldSetLinearVelocityCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) { Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite vx vy", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid;
    double vx, vy;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &vx) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &vy) != TCL_OK) return TCL_ERROR;
    Sprite *sp = &w->sprites[sid];
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Body_SetLinearVelocity(sp->body, (b2Vec2){(float)vx, (float)vy});
    }
    return TCL_OK;
}

static int worldApplyForceCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) { Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite fx fy", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid;
    double fx, fy;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &fx) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &fy) != TCL_OK) return TCL_ERROR;
    Sprite *sp = &w->sprites[sid];
    if (sp->has_body && b2Body_IsValid(sp->body)) {
        b2Body_ApplyForceToCenter(sp->body, (b2Vec2){(float)fx, (float)fy}, true);
    }
    return TCL_OK;
}

/*========================================================================
 * Tcl Commands - Animation
 *========================================================================*/

static int worldSetSpriteAnimationCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) { Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite frames fps ?loop?", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    
    Sprite *sp = &w->sprites[sid];
    Tcl_Size listc; const char **listv;
    if (Tcl_SplitList(interp, argv[3], &listc, &listv) != TCL_OK) return TCL_ERROR;
    sp->anim_frame_count = listc > 32 ? 32 : (int)listc;
    for (int i = 0; i < sp->anim_frame_count; i++) Tcl_GetInt(interp, listv[i], &sp->anim_frames[i]);
    Tcl_Free((char *)listv);
    
    double fps;
    if (Tcl_GetDouble(interp, argv[4], &fps) != TCL_OK) return TCL_ERROR;
    sp->anim_fps = (float)fps;
    sp->anim_loop = 1;
    if (argc > 5) Tcl_GetInt(interp, argv[5], &sp->anim_loop);
    sp->anim_current_frame = 0;
    sp->anim_time = 0;
    sp->anim_playing = 1;
    return TCL_OK;
}

static int worldPlayAnimationCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) { Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite play(0/1)", NULL); return TCL_ERROR; }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int sid, play;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) return TCL_ERROR;
    if (Tcl_GetInt(interp, argv[3], &play) != TCL_OK) return TCL_ERROR;
    w->sprites[sid].anim_playing = play;
    if (play) { w->sprites[sid].anim_current_frame = 0; w->sprites[sid].anim_time = 0; }
    return TCL_OK;
}

/*========================================================================
 * Command Registration
 *========================================================================*/

void world_sprite_register_commands(Tcl_Interp *interp, OBJ_LIST *olist)
{
    Tcl_CreateCommand(interp, "worldCreateSprite", (Tcl_CmdProc*)worldCreateSpriteCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldRemoveSprite", (Tcl_CmdProc*)worldRemoveSpriteCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSpriteAddBody", (Tcl_CmdProc*)worldSpriteAddBodyCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetSpritePosition", (Tcl_CmdProc*)worldSetSpritePositionCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetSpriteRotation", (Tcl_CmdProc*)worldSetSpriteRotationCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetSpriteVisible", (Tcl_CmdProc*)worldSetSpriteVisibleCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetSpriteTile", (Tcl_CmdProc*)worldSetSpriteTileCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldGetSpriteCount", (Tcl_CmdProc*)worldGetSpriteCountCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldGetSpriteByName", (Tcl_CmdProc*)worldGetSpriteByNameCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldGetSpriteInfo", (Tcl_CmdProc*)worldGetSpriteInfoCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldApplyImpulse", (Tcl_CmdProc*)worldApplyImpulseCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetLinearVelocity", (Tcl_CmdProc*)worldSetLinearVelocityCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldApplyForce", (Tcl_CmdProc*)worldApplyForceCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetSpriteAnimation", (Tcl_CmdProc*)worldSetSpriteAnimationCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldPlayAnimation", (Tcl_CmdProc*)worldPlayAnimationCmd, (ClientData)olist, NULL);
}
