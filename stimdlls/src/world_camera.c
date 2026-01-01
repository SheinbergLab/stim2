/*
 * world_camera.c
 *
 * Camera system for 2D world module.
 * Supports multiple follow modes, smoothing, and bounds clamping.
 */

#include "world_internal.h"
#include <math.h>
#include <string.h>

/*========================================================================
 * Camera Initialization
 *========================================================================*/

void world_camera_init(Camera *cam)
{
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

/*========================================================================
 * Camera Update
 *========================================================================*/

void world_camera_update(World *w, float dt)
{
    Camera *cam = &w->camera;
    
    switch (cam->mode) {
        case CAM_LOCKED:
            /* target stays where it is */
            break;
            
        case CAM_FIXED_SCROLL:
            cam->target_x += cam->scroll_vx * dt;
            cam->target_y += cam->scroll_vy * dt;
            break;
            
        case CAM_FOLLOW:
            if (cam->follow_sprite >= 0 && cam->follow_sprite < w->sprite_count) {
                Sprite *sp = &w->sprites[cam->follow_sprite];
                cam->target_x = sp->x;
                cam->target_y = sp->y;
            }
            break;
            
        case CAM_FOLLOW_DEADZONE:
            if (cam->follow_sprite >= 0 && cam->follow_sprite < w->sprite_count) {
                Sprite *sp = &w->sprites[cam->follow_sprite];
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
            if (cam->follow_sprite >= 0 && cam->follow_sprite < w->sprite_count) {
                Sprite *sp = &w->sprites[cam->follow_sprite];
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

/*========================================================================
 * Tcl Commands
 *========================================================================*/

int worldSetCameraModeCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], 
            " world mode ?sprite_id?\n"
            "  modes: locked, scroll, follow, deadzone, lookahead", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    const char *mode = argv[2];
    if (strcmp(mode, "locked") == 0) {
        w->camera.mode = CAM_LOCKED;
    } else if (strcmp(mode, "scroll") == 0) {
        w->camera.mode = CAM_FIXED_SCROLL;
    } else if (strcmp(mode, "follow") == 0) {
        w->camera.mode = CAM_FOLLOW;
        if (argc > 3) {
            int sid;
            if (Tcl_GetInt(interp, argv[3], &sid) == TCL_OK) {
                w->camera.follow_sprite = sid;
            }
        }
    } else if (strcmp(mode, "deadzone") == 0) {
        w->camera.mode = CAM_FOLLOW_DEADZONE;
        if (argc > 3) {
            int sid;
            if (Tcl_GetInt(interp, argv[3], &sid) == TCL_OK) {
                w->camera.follow_sprite = sid;
            }
        }
    } else if (strcmp(mode, "lookahead") == 0) {
        w->camera.mode = CAM_FOLLOW_LOOKAHEAD;
        if (argc > 3) {
            int sid;
            if (Tcl_GetInt(interp, argv[3], &sid) == TCL_OK) {
                w->camera.follow_sprite = sid;
            }
        }
    } else {
        Tcl_AppendResult(interp, "unknown camera mode: ", mode, NULL);
        return TCL_ERROR;
    }
    
    return TCL_OK;
}

int worldSetCameraSmoothCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world speed", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    double speed;
    if (Tcl_GetDouble(interp, argv[2], &speed) != TCL_OK) return TCL_ERROR;
    w->camera.smooth_speed = (float)speed;
    
    return TCL_OK;
}

int worldSetCameraBoundsCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 6) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world min_x max_x min_y max_y", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    double min_x, max_x, min_y, max_y;
    if (Tcl_GetDouble(interp, argv[2], &min_x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &max_x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &min_y) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &max_y) != TCL_OK) return TCL_ERROR;
    
    w->camera.min_x = (float)min_x;
    w->camera.max_x = (float)max_x;
    w->camera.min_y = (float)min_y;
    w->camera.max_y = (float)max_y;
    w->camera.use_bounds = 1;
    
    return TCL_OK;
}

int worldClearCameraBoundsCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    w->camera.use_bounds = 0;
    
    return TCL_OK;
}

int worldSetCameraPosCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world x y", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    double x, y;
    if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;
    
    w->camera.x = (float)x;
    w->camera.y = (float)y;
    w->camera.target_x = (float)x;
    w->camera.target_y = (float)y;
    
    return TCL_OK;
}

int worldGetCameraInfoCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Camera *cam = &w->camera;
    
    Tcl_Obj *result = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("x", -1), Tcl_NewDoubleObj(cam->x));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("y", -1), Tcl_NewDoubleObj(cam->y));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("target_x", -1), Tcl_NewDoubleObj(cam->target_x));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("target_y", -1), Tcl_NewDoubleObj(cam->target_y));
    
    const char *mode_str;
    switch (cam->mode) {
        case CAM_LOCKED: mode_str = "locked"; break;
        case CAM_FIXED_SCROLL: mode_str = "scroll"; break;
        case CAM_FOLLOW: mode_str = "follow"; break;
        case CAM_FOLLOW_DEADZONE: mode_str = "deadzone"; break;
        case CAM_FOLLOW_LOOKAHEAD: mode_str = "lookahead"; break;
        default: mode_str = "unknown"; break;
    }
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("mode", -1), Tcl_NewStringObj(mode_str, -1));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("follow_sprite", -1), Tcl_NewIntObj(cam->follow_sprite));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("smooth_speed", -1), Tcl_NewDoubleObj(cam->smooth_speed));
    
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

int worldSetAutoCenterCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world 0|1", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    int val;
    if (Tcl_GetInt(interp, argv[2], &val) != TCL_OK) return TCL_ERROR;
    w->auto_center = val;
    
    return TCL_OK;
}

/*========================================================================
 * Command Registration (called from world.c)
 *========================================================================*/

void world_camera_register_commands(Tcl_Interp *interp, OBJ_LIST *olist)
{
    Tcl_CreateCommand(interp, "worldSetCameraMode", 
        (Tcl_CmdProc*)worldSetCameraModeCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetCameraSmooth", 
        (Tcl_CmdProc*)worldSetCameraSmoothCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetCameraBounds", 
        (Tcl_CmdProc*)worldSetCameraBoundsCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldClearCameraBounds", 
        (Tcl_CmdProc*)worldClearCameraBoundsCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetCameraPos", 
        (Tcl_CmdProc*)worldSetCameraPosCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldGetCameraInfo", 
        (Tcl_CmdProc*)worldGetCameraInfoCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetAutoCenter", 
        (Tcl_CmdProc*)worldSetAutoCenterCmd, (ClientData)olist, NULL);
}
