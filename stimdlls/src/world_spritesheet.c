/*
 * world_spritesheet.c
 *
 * Sprite sheet (asset/template) management for 2D world module.
 * Handles loading, parsing, and querying sprite sheets and their animations.
 */

#include "world_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*========================================================================
 * Dict Helper - simplifies Tcl dict lookups
 *========================================================================*/

static Tcl_Obj* dict_get(Tcl_Interp *interp, Tcl_Obj *dict, const char *key)
{
    Tcl_Obj *val = NULL;
    Tcl_Obj *k = Tcl_NewStringObj(key, -1);
    Tcl_DictObjGet(interp, dict, k, &val);
    return val;
}

static int dict_get_int(Tcl_Interp *interp, Tcl_Obj *dict, const char *key, int def)
{
    Tcl_Obj *v = dict_get(interp, dict, key);
    if (v) { int i; if (Tcl_GetIntFromObj(interp, v, &i) == TCL_OK) return i; }
    return def;
}

static double dict_get_double(Tcl_Interp *interp, Tcl_Obj *dict, const char *key, double def)
{
    Tcl_Obj *v = dict_get(interp, dict, key);
    if (v) { double d; if (Tcl_GetDoubleFromObj(interp, v, &d) == TCL_OK) return d; }
    return def;
}

static const char* dict_get_str(Tcl_Interp *interp, Tcl_Obj *dict, const char *key, const char *def)
{
    Tcl_Obj *v = dict_get(interp, dict, key);
    return v ? Tcl_GetString(v) : def;
}

/*========================================================================
 * Sprite Sheet Lookup (used by other modules)
 *========================================================================*/

SpriteSheet* world_find_sprite_sheet(World *w, const char *name)
{
    if (!w || !name) return NULL;
    for (int i = 0; i < w->sprite_sheet_count; i++) {
        if (strcmp(w->sprite_sheets[i].name, name) == 0) {
            return &w->sprite_sheets[i];
        }
    }
    return NULL;
}

SpriteSheet* world_find_sprite_sheet_by_gid(World *w, int gid)
{
    SpriteSheet *best = NULL;
    for (int i = 0; i < w->sprite_sheet_count; i++) {
        if (w->sprite_sheets[i].firstgid <= gid) {
            if (!best || w->sprite_sheets[i].firstgid > best->firstgid) {
                best = &w->sprite_sheets[i];
            }
        }
    }
    return best;
}

int world_spritesheet_find_frame(SpriteSheet *ss, const char *name)
{
    if (!ss || !name || !ss->frame_names_init) return -1;
    Tcl_HashEntry *entry = Tcl_FindHashEntry(&ss->frame_names, name);
    if (entry) return (int)(intptr_t)Tcl_GetHashValue(entry);
    return -1;
}

/*========================================================================
 * Tile Collision Lookup
 *========================================================================*/

TileCollision* world_get_tile_collision(World *w, int gid)
{
    SpriteSheet *best = NULL;
    for (int i = 0; i < w->sprite_sheet_count; i++) {
        SpriteSheet *ss = &w->sprite_sheets[i];
        if (ss->firstgid <= gid) {
            if (!best || ss->firstgid > best->firstgid) {
                best = ss;
            }
        }
    }
    
    if (!best) return NULL;
    
    int local_id = gid - best->firstgid;
    if (local_id < 0 || local_id >= WORLD_MAX_TILE_COLLISIONS) return NULL;
    
    TileCollision *tc = &best->frame_collisions[local_id];
    return (tc->shape_count == 0) ? NULL : tc;
}

/*========================================================================
 * Tcl Commands - Query
 *========================================================================*/

static int worldGetSpriteSheetsCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
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
    Tcl_Obj *list = Tcl_NewListObj(0, NULL);

    for (int i = 0; i < w->sprite_sheet_count; i++) {
        SpriteSheet *ss = &w->sprite_sheets[i];
        Tcl_Obj *dict = Tcl_NewDictObj();

        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("name", -1), Tcl_NewStringObj(ss->name, -1));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("firstgid", -1), Tcl_NewIntObj(ss->firstgid));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("tile_width", -1), Tcl_NewIntObj(ss->tile_width));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("tile_height", -1), Tcl_NewIntObj(ss->tile_height));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("atlas_id", -1), Tcl_NewIntObj(ss->atlas_id));

        if (ss->has_aseprite) {
            Tcl_Obj *anim_list = Tcl_NewListObj(0, NULL);
            for (int j = 0; j < ss->aseprite.animation_count; j++) {
                Tcl_ListObjAppendElement(interp, anim_list,
                    Tcl_NewStringObj(ss->aseprite.animations[j].name, -1));
            }
            Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("animations", -1), anim_list);
        }
        Tcl_ListObjAppendElement(interp, list, dict);
    }

    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

static int worldGetAnimationFramesCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world sheet_name animation_name", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    SpriteSheet *ss = world_find_sprite_sheet(w, argv[2]);
    if (!ss) { Tcl_AppendResult(interp, "sprite sheet not found: ", argv[2], NULL); return TCL_ERROR; }
    if (!ss->has_aseprite) { Tcl_AppendResult(interp, "no animation data: ", argv[2], NULL); return TCL_ERROR; }

    AsepriteAnimation *anim = aseprite_find_animation(&ss->aseprite, argv[3]);
    if (!anim) { Tcl_AppendResult(interp, "animation not found: ", argv[3], NULL); return TCL_ERROR; }

    Tcl_Obj *list = Tcl_NewListObj(0, NULL);
    for (int i = 0; i < anim->frame_count; i++) {
        Tcl_ListObjAppendElement(interp, list, Tcl_NewIntObj(anim->frames[i]));
    }
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

/*========================================================================
 * Tcl Commands - Sprite Creation from Sheet
 *========================================================================*/

static int worldSetSpriteAnimationByNameCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world sprite sheet_name animation_name ?fps? ?loop?", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));

    int sid;
    if (Tcl_GetInt(interp, argv[2], &sid) != TCL_OK) return TCL_ERROR;
    if (sid < 0 || sid >= w->sprite_count) { Tcl_AppendResult(interp, "invalid sprite", NULL); return TCL_ERROR; }

    SpriteSheet *ss = world_find_sprite_sheet(w, argv[3]);
    if (!ss) { Tcl_AppendResult(interp, "sprite sheet not found: ", argv[3], NULL); return TCL_ERROR; }
    if (!ss->has_aseprite) { Tcl_AppendResult(interp, "no animation data: ", argv[3], NULL); return TCL_ERROR; }

    AsepriteAnimation *anim = aseprite_find_animation(&ss->aseprite, argv[4]);
    if (!anim) { Tcl_AppendResult(interp, "animation not found: ", argv[4], NULL); return TCL_ERROR; }

    float fps = anim->default_fps;
    int loop = 1;
    if (argc > 5) { double d; if (Tcl_GetDouble(interp, argv[5], &d) == TCL_OK) fps = (float)d; }
    if (argc > 6) Tcl_GetInt(interp, argv[6], &loop);

    Sprite *sp = &w->sprites[sid];
    sp->anim_frame_count = anim->frame_count > 32 ? 32 : anim->frame_count;
    for (int i = 0; i < sp->anim_frame_count; i++) sp->anim_frames[i] = anim->frames[i];
    sp->anim_fps = fps;
    sp->anim_loop = loop;
    sp->anim_current_frame = 0;
    sp->anim_time = 0;
    sp->anim_playing = 1;
    sp->atlas_id = ss->atlas_id;

    if (sp->anim_frame_count > 0) {
        sp->tile_id = sp->anim_frames[0];
        world_get_tile_uvs(&w->atlases[sp->atlas_id], sp->tile_id, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
    }
    return TCL_OK;
}

static int worldCreateSpriteFromTilesetCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 8) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world name sheet_name x y w h ?animation?", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));

    if (w->sprite_count >= WORLD_MAX_SPRITES) { Tcl_AppendResult(interp, "max sprites", NULL); return TCL_ERROR; }

    SpriteSheet *ss = world_find_sprite_sheet(w, argv[3]);
    if (!ss) { Tcl_AppendResult(interp, "sprite sheet not found: ", argv[3], NULL); return TCL_ERROR; }

    double x, y, width, height;
    if (Tcl_GetDouble(interp, argv[4], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &y) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[6], &width) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[7], &height) != TCL_OK) return TCL_ERROR;

    int sid = w->sprite_count;
    Sprite *sp = &w->sprites[w->sprite_count++];
    memset(sp, 0, sizeof(Sprite));

    strncpy(sp->name, argv[2], 63);
    sp->x = (float)x; sp->y = (float)y;
    sp->w = (float)width; sp->h = (float)height;
    sp->atlas_id = ss->atlas_id;
    sp->tile_id = ss->firstgid;
    sp->visible = 1;

    if (ss->has_aseprite && ss->aseprite.has_hitbox) {
        sp->has_hitbox_data = 1;
        sp->hitbox_w_ratio = ss->aseprite.hitbox_width_ratio;
        sp->hitbox_h_ratio = ss->aseprite.hitbox_height_ratio;
        sp->hitbox_offset_x = ss->aseprite.hitbox_offset_x;
        sp->hitbox_offset_y = ss->aseprite.hitbox_offset_y;

        float old_w = sp->w, old_h = sp->h;
        sp->w = sp->w / sp->hitbox_w_ratio;
        sp->h = sp->h / sp->hitbox_h_ratio;
        sp->x += (sp->w - old_w) * 0.5f * sp->hitbox_offset_x;
        sp->y += (sp->h - old_h) * 0.5f * sp->hitbox_offset_y;
    }

    if (ss->atlas_id >= 0 && ss->atlas_id < w->atlas_count) {
        world_get_tile_uvs(&w->atlases[ss->atlas_id], sp->tile_id, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
    }

    if (argc > 8 && ss->has_aseprite) {
        AsepriteAnimation *anim = aseprite_find_animation(&ss->aseprite, argv[8]);
        if (anim) {
            sp->anim_frame_count = anim->frame_count > 32 ? 32 : anim->frame_count;
            for (int i = 0; i < sp->anim_frame_count; i++) sp->anim_frames[i] = anim->frames[i];
            sp->anim_fps = anim->default_fps;
            sp->anim_loop = 1;
            sp->anim_playing = 1;
            if (sp->anim_frame_count > 0) {
                sp->tile_id = sp->anim_frames[0];
                world_get_tile_uvs(&w->atlases[sp->atlas_id], sp->tile_id, &sp->u0, &sp->v0, &sp->u1, &sp->v1);
            }
        }
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(sid));
    return TCL_OK;
}

/*========================================================================
 * Sprite Sheet Parsing Helpers
 *========================================================================*/

static void parse_sheet_metadata(Tcl_Interp *interp, Tcl_Obj *meta, SpriteSheet *ss,
                                 int *tex_w, int *tex_h, const char **png_file)
{
    *tex_w = dict_get_int(interp, meta, "texture_width", *tex_w);
    *tex_h = dict_get_int(interp, meta, "texture_height", *tex_h);
    
    Tcl_Obj *img = dict_get(interp, meta, "image");
    if (img) *png_file = Tcl_GetString(img);

    Tcl_Obj *canvas = dict_get(interp, meta, "canonical_canvas");
    if (canvas) {
        ss->canonical_w = dict_get_int(interp, canvas, "w", 0);
        ss->canonical_h = dict_get_int(interp, canvas, "h", 0);
    }

    Tcl_Obj *anims = dict_get(interp, meta, "animations");
    if (anims) {
        ss->has_aseprite = 1;
        memset(&ss->aseprite, 0, sizeof(AsepriteData));

        Tcl_DictSearch search;
        Tcl_Obj *key, *value;
        int done, count = 0;

        if (Tcl_DictObjFirst(interp, anims, &search, &key, &value, &done) == TCL_OK) {
            for (; !done && count < 32; Tcl_DictObjNext(&search, &key, &value, &done)) {
                AsepriteAnimation *anim = &ss->aseprite.animations[count];
                strncpy(anim->name, Tcl_GetString(key), sizeof(anim->name) - 1);

                Tcl_Obj *frames = dict_get(interp, value, "frames");
                if (frames) {
                    Tcl_Size fc; Tcl_Obj **fv;
                    if (Tcl_ListObjGetElements(interp, frames, &fc, &fv) == TCL_OK) {
                        anim->frame_count = (fc > 32) ? 32 : fc;
                        for (int f = 0; f < anim->frame_count; f++)
                            Tcl_GetIntFromObj(interp, fv[f], &anim->frames[f]);
                    }
                }

                anim->default_fps = (float)dict_get_double(interp, value, "fps", 10.0);
                
                const char *dir = dict_get_str(interp, value, "direction", "forward");
                if (strcmp(dir, "reverse") == 0) anim->direction = ANIM_REVERSE;
                else if (strcmp(dir, "pingpong") == 0) anim->direction = ANIM_PINGPONG;
                else anim->direction = ANIM_FORWARD;

                count++;
            }
            ss->aseprite.animation_count = count;
            Tcl_DictObjDone(&search);
        }
    }
}

static void parse_frame_collision(Tcl_Interp *interp, Tcl_Obj *fixtures_obj,
                                  TileCollision *coll, int frame_w, int frame_h)
{
    Tcl_Size fc; Tcl_Obj **fixtures;
    if (Tcl_ListObjGetElements(interp, fixtures_obj, &fc, &fixtures) != TCL_OK) return;

    for (int i = 0; i < fc && coll->shape_count < WORLD_MAX_SHAPES_PER_TILE; i++) {
        Tcl_Obj *fix = fixtures[i];
        CollisionShape *shape = &coll->shapes[coll->shape_count];
        const char *type = dict_get_str(interp, fix, "shape", "polygon");
        Tcl_Obj *data = dict_get(interp, fix, "data");
        if (!data) continue;

        if (strcmp(type, "circle") == 0) {
            shape->type = SHAPE_CIRCLE;
            shape->circle_x = dict_get_double(interp, data, "center_x", 0) / frame_w;
            shape->circle_y = dict_get_double(interp, data, "center_y", 0) / frame_h;
            shape->circle_radius = dict_get_double(interp, data, "radius", 0) / frame_w;
            coll->shape_count++;

        } else if (strcmp(type, "box") == 0) {
            shape->type = SHAPE_BOX;
            shape->box_x = dict_get_double(interp, data, "x", 0) / frame_w;
            shape->box_y = dict_get_double(interp, data, "y", 0) / frame_h;
            shape->box_w = dict_get_double(interp, data, "w", 0) / frame_w;
            shape->box_h = dict_get_double(interp, data, "h", 0) / frame_h;
            coll->shape_count++;

        } else {
            /* Polygon - data is list of {x y} dicts */
            Tcl_Size vc; Tcl_Obj **verts;
            if (Tcl_ListObjGetElements(interp, data, &vc, &verts) != TCL_OK) continue;

            shape->type = SHAPE_POLYGON;
            shape->vert_count = 0;
            for (int v = 0; v < vc && v < WORLD_MAX_COLLISION_VERTS; v++) {
                shape->verts_x[shape->vert_count] = dict_get_double(interp, verts[v], "x", 0) / frame_w;
                shape->verts_y[shape->vert_count] = dict_get_double(interp, verts[v], "y", 0) / frame_h;
                shape->vert_count++;
            }
            if (shape->vert_count >= 3) coll->shape_count++;
        }
    }
}

/*========================================================================
 * Tcl Commands - Sheet Loading
 *========================================================================*/

static int worldAddSpriteSheetCmd(ClientData cd, Tcl_Interp *interp,
                                   int objc, Tcl_Obj *const objv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (objc != 4) { Tcl_WrongNumArgs(interp, 1, objv, "world name sheetDict"); return TCL_ERROR; }

    int id;
    const char *idstr = Tcl_GetString(objv[1]);
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), idstr, WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    const char *name = Tcl_GetString(objv[2]);

    if (w->sprite_sheet_count >= WORLD_MAX_SPRITE_TILESETS) {
        Tcl_SetResult(interp, "Too many sprite sheets", TCL_STATIC);
        return TCL_ERROR;
    }

    SpriteSheet *ss = &w->sprite_sheets[w->sprite_sheet_count];
    memset(ss, 0, sizeof(SpriteSheet));
    strncpy(ss->name, name, sizeof(ss->name) - 1);

    /* Initialize frame name hash table */
    Tcl_InitHashTable(&ss->frame_names, TCL_STRING_KEYS);
    ss->frame_names_init = 1;

    Tcl_Obj *sheet = objv[3];
    int tex_w = 1024, tex_h = 1024;
    const char *png_file = NULL;

    /* Parse metadata */
    Tcl_Obj *meta = dict_get(interp, sheet, "_metadata");
    if (meta) parse_sheet_metadata(interp, meta, ss, &tex_w, &tex_h, &png_file);

    /* Parse frames */
    Tcl_DictSearch search;
    Tcl_Obj *key, *value;
    int done;

    ss->frame_count = 0;
    if (Tcl_DictObjFirst(interp, sheet, &search, &key, &value, &done) == TCL_OK) {
        for (; !done && ss->frame_count < WORLD_MAX_FRAMES; Tcl_DictObjNext(&search, &key, &value, &done)) {
            const char *frame_name = Tcl_GetString(key);
            if (frame_name[0] == '_') continue;

            Tcl_Obj *rect = dict_get(interp, value, "frame_rect");
            if (rect) {
                int x = dict_get_int(interp, rect, "x", 0);
                int y = dict_get_int(interp, rect, "y", 0);
                int fw = dict_get_int(interp, rect, "w", 0);
                int fh = dict_get_int(interp, rect, "h", 0);

                SpriteFrame *sf = &ss->frames[ss->frame_count];
                sf->x = x; sf->y = y; sf->w = fw; sf->h = fh;
                sf->u0 = (float)x / tex_w;
                sf->v0 = (float)y / tex_h;
                sf->u1 = (float)(x + fw) / tex_w;
                sf->v1 = (float)(y + fh) / tex_h;

                /* Add frame name to hash table */
                int is_new;
                Tcl_HashEntry *entry = Tcl_CreateHashEntry(&ss->frame_names, frame_name, &is_new);
                if (is_new) Tcl_SetHashValue(entry, (ClientData)(intptr_t)ss->frame_count);

                TileCollision *coll = &ss->frame_collisions[ss->frame_count];
                coll->shape_count = 0;

                Tcl_Obj *fixtures = dict_get(interp, value, "fixtures");
                if (fixtures) parse_frame_collision(interp, fixtures, coll, fw, fh);

                ss->frame_count++;
            }
        }
        Tcl_DictObjDone(&search);
    }

    /* Load texture */
    if (png_file) {
        int atlas_id = world_load_packed_atlas(w, png_file);
        ss->atlas_id = (atlas_id >= 0) ? atlas_id : -1;
    } else {
        ss->atlas_id = -1;
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(w->sprite_sheet_count));
    w->sprite_sheet_count++;
    return TCL_OK;
}

static int worldCreateSpriteFromSheetCmd(ClientData cd, Tcl_Interp *interp,
                                          int objc, Tcl_Obj *const objv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (objc < 3 || objc > 6) {
        Tcl_WrongNumArgs(interp, 1, objv, "world sheetName ?x y? ?frameIdx?");
        return TCL_ERROR;
    }

    int id;
    const char *idstr = Tcl_GetString(objv[1]);
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), idstr, WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    const char *sheet_name = Tcl_GetString(objv[2]);

    int sheet_id = -1;
    for (int i = 0; i < w->sprite_sheet_count; i++) {
        if (strcmp(w->sprite_sheets[i].name, sheet_name) == 0) { sheet_id = i; break; }
    }
    if (sheet_id < 0) { Tcl_SetResult(interp, "Sprite sheet not found", TCL_STATIC); return TCL_ERROR; }
    if (w->sprite_count >= WORLD_MAX_SPRITES) { Tcl_SetResult(interp, "Too many sprites", TCL_STATIC); return TCL_ERROR; }

    Sprite *sp = &w->sprites[w->sprite_count];
    memset(sp, 0, sizeof(Sprite));

    sp->sprite_sheet_id = sheet_id;
    sp->uses_sprite_sheet = 1;
    sp->current_frame = 0;

    if (objc >= 5) {
        double x, y;
        Tcl_GetDoubleFromObj(interp, objv[3], &x);
        Tcl_GetDoubleFromObj(interp, objv[4], &y);
        sp->x = x; sp->y = y;
    }

    if (objc >= 6) {
        int frame_idx;
        Tcl_GetIntFromObj(interp, objv[5], &frame_idx);
        SpriteSheet *ss = &w->sprite_sheets[sheet_id];
        if (frame_idx >= 0 && frame_idx < ss->frame_count) sp->current_frame = frame_idx;
    }

    sp->visible = 1;
    sp->atlas_id = w->sprite_sheets[sheet_id].atlas_id;
    strncpy(sp->name, sheet_name, sizeof(sp->name) - 1);

    Tcl_SetObjResult(interp, Tcl_NewIntObj(w->sprite_count));
    w->sprite_count++;
    return TCL_OK;
}

static int worldSetSpriteFrameCmd(ClientData cd, Tcl_Interp *interp,
                                   int objc, Tcl_Obj *const objv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (objc != 4) { Tcl_WrongNumArgs(interp, 1, objv, "world spriteIdx frameIdx"); return TCL_ERROR; }

    int id;
    const char *idstr = Tcl_GetString(objv[1]);
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), idstr, WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));

    int sprite_idx, frame_idx;
    Tcl_GetIntFromObj(interp, objv[2], &sprite_idx);
    Tcl_GetIntFromObj(interp, objv[3], &frame_idx);

    if (sprite_idx < 0 || sprite_idx >= w->sprite_count) {
        Tcl_SetResult(interp, "Invalid sprite index", TCL_STATIC); return TCL_ERROR;
    }

    Sprite *sp = &w->sprites[sprite_idx];
    if (!sp->uses_sprite_sheet) { Tcl_SetResult(interp, "Not a sprite sheet sprite", TCL_STATIC); return TCL_ERROR; }

    SpriteSheet *ss = &w->sprite_sheets[sp->sprite_sheet_id];
    if (frame_idx < 0 || frame_idx >= ss->frame_count) {
        Tcl_SetResult(interp, "Invalid frame index", TCL_STATIC); return TCL_ERROR;
    }

    sp->current_frame = frame_idx;
    return TCL_OK;
}

static int worldSetSpriteFrameByNameCmd(ClientData cd, Tcl_Interp *interp,
                                         int objc, Tcl_Obj *const objv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (objc != 4) { Tcl_WrongNumArgs(interp, 1, objv, "world spriteIdx frameName"); return TCL_ERROR; }

    int id;
    const char *idstr = Tcl_GetString(objv[1]);
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), idstr, WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));

    int sprite_idx;
    Tcl_GetIntFromObj(interp, objv[2], &sprite_idx);
    const char *frame_name = Tcl_GetString(objv[3]);

    if (sprite_idx < 0 || sprite_idx >= w->sprite_count) {
        Tcl_SetResult(interp, "Invalid sprite index", TCL_STATIC); return TCL_ERROR;
    }

    Sprite *sp = &w->sprites[sprite_idx];
    if (!sp->uses_sprite_sheet) { Tcl_SetResult(interp, "Not a sprite sheet sprite", TCL_STATIC); return TCL_ERROR; }

    SpriteSheet *ss = &w->sprite_sheets[sp->sprite_sheet_id];
    int frame_idx = world_spritesheet_find_frame(ss, frame_name);
    if (frame_idx < 0) {
        Tcl_SetResult(interp, "Frame not found", TCL_STATIC); return TCL_ERROR;
    }

    sp->current_frame = frame_idx;
    return TCL_OK;
}

/*========================================================================
 * Command Registration
 *========================================================================*/

void world_spritesheet_register_commands(Tcl_Interp *interp, OBJ_LIST *olist)
{
    Tcl_CreateCommand(interp, "worldGetSpriteSheets", (Tcl_CmdProc*)worldGetSpriteSheetsCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldGetAnimationFrames", (Tcl_CmdProc*)worldGetAnimationFramesCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetSpriteAnimationByName", (Tcl_CmdProc*)worldSetSpriteAnimationByNameCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldCreateSpriteFromTileset", (Tcl_CmdProc*)worldCreateSpriteFromTilesetCmd, (ClientData)olist, NULL);
    Tcl_CreateObjCommand(interp, "worldAddSpriteSheet", worldAddSpriteSheetCmd, (ClientData)olist, NULL);
    Tcl_CreateObjCommand(interp, "worldCreateSpriteFromSheet", worldCreateSpriteFromSheetCmd, (ClientData)olist, NULL);
    Tcl_CreateObjCommand(interp, "worldSetSpriteFrame", worldSetSpriteFrameCmd, (ClientData)olist, NULL);
    Tcl_CreateObjCommand(interp, "worldSetSpriteFrameByName", worldSetSpriteFrameByNameCmd, (ClientData)olist, NULL);
}
