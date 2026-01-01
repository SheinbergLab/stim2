/*
 * world_tilemap.c
 *
 * TMX tilemap loading and tile management for 2D world module.
 * Handles Tiled Map Editor file parsing, tile layers, and object layers.
 */

#include "world_internal.h"
#include "tmx_xml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*========================================================================
 * Polygon Point Parsing
 *========================================================================*/

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
        
        x = strtof(p, &end);
        if (end == p) break;
        p = end;
        
        if (*p == ',') p++;
        
        y = strtof(p, &end);
        if (end == p) break;
        p = end;
        
        while (*p == ' ' || *p == '\t') p++;
        
        out_x[count] = (obj_x + x) / tile_w;
        out_y[count] = (obj_y + y) / tile_h;
        count++;
    }
    
    return count;
}

/*========================================================================
 * Tile Collision Loading
 *========================================================================*/

static void load_tile_collisions(void *tileset_xml, SpriteSheet *ss)
{
    ss->tile_collision_count = 0;
    
    if (ss->tile_width > 0 && ss->tile_height > 0) {
        ss->canonical_w = ss->tile_width;
        ss->canonical_h = ss->tile_height;
    }
    ss->frame_count = 0;
    
    for (int i = 0; i < WORLD_MAX_TILE_COLLISIONS; i++) {
        ss->frame_collisions[i].shape_count = 0;
    }
    
    for (void *tile = tmx_xml_tileset_first_tile(tileset_xml);
         tile != NULL;
         tile = tmx_xml_tileset_next_tile(tile)) {
        
        int tile_id = tmx_xml_tile_get_id(tile);
        if (tile_id < 0 || tile_id >= WORLD_MAX_TILE_COLLISIONS) continue;
        
        void *objgroup = tmx_xml_tile_get_objectgroup(tile);
        if (!objgroup) continue;
        
        TileCollision *tc = &ss->frame_collisions[tile_id];
        tc->shape_count = 0;
        
        for (void *obj = tmx_xml_first_object(objgroup);
             obj != NULL && tc->shape_count < WORLD_MAX_SHAPES_PER_TILE;
             obj = tmx_xml_next_object(obj)) {
            
            CollisionShape *shape = &tc->shapes[tc->shape_count];
            
            float obj_x = tmx_xml_object_get_float(obj, "x", 0);
            float obj_y = tmx_xml_object_get_float(obj, "y", 0);
            
            if (tmx_xml_object_has_polygon(obj)) {
                const char *points = tmx_xml_object_get_polygon_points(obj);
                shape->vert_count = parse_polygon_points(points, obj_x, obj_y,
                                                         ss->tile_width, ss->tile_height,
                                                         shape->verts_x, shape->verts_y,
                                                         WORLD_MAX_COLLISION_VERTS);
                if (shape->vert_count >= 3) {
                    shape->type = SHAPE_POLYGON;
                    tc->shape_count++;
                }
            } else {
                float w = tmx_xml_object_get_float(obj, "width", ss->tile_width);
                float h = tmx_xml_object_get_float(obj, "height", ss->tile_height);
                
                shape->type = SHAPE_BOX;
                shape->box_x = obj_x / ss->tile_width;
                shape->box_y = obj_y / ss->tile_height;
                shape->box_w = w / ss->tile_width;
                shape->box_h = h / ss->tile_height;
                tc->shape_count++;
            }
        }
        
        if (tc->shape_count > 0) {
            ss->tile_collision_count++;
        }
    }
}

/*========================================================================
 * Tile Collision Shape Creation
 *========================================================================*/

static int create_tile_collision_shapes(World *w, b2BodyId body,
                                        float tile_w, float tile_h,
                                        int gid, const char *name)
{
    TileCollision *tc = world_get_tile_collision(w, gid);
    
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = 1.0f;
    sd.userData = (void *)name;
    
    if (!tc) {
        b2Polygon box = b2MakeBox(tile_w * 0.5f, tile_h * 0.5f);
        b2ShapeId shape = b2CreatePolygonShape(body, &sd, &box);
        b2Shape_SetFriction(shape, 0.3f);
        return 1;
    }
    
    int created = 0;
    for (int i = 0; i < tc->shape_count; i++) {
        CollisionShape *cs = &tc->shapes[i];
        b2ShapeId shape;
        
        if (cs->type == SHAPE_POLYGON) {
            b2Vec2 points[WORLD_MAX_COLLISION_VERTS];
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
 * CSV/Base64 Parsing
 *========================================================================*/

static int* parse_csv(const char *csv, int w, int h)
{
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

static int* decode_base64_tiles(const char *text, int width, int height)
{
    size_t len = strlen(text);
    char *clean = malloc(len + 1);
    size_t clean_len = 0;
    for (size_t i = 0; i < len; i++) {
        if (!isspace(text[i])) clean[clean_len++] = text[i];
    }
    clean[clean_len] = '\0';
    
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
    
    int *tiles = malloc(width * height * sizeof(int));
    for (int i = 0; i < width * height; i++) {
        tiles[i] = decoded[i*4] | (decoded[i*4+1] << 8) | 
                   (decoded[i*4+2] << 16) | (decoded[i*4+3] << 24);
    }
    free(decoded);
    return tiles;
}

/*========================================================================
 * TMX Loading Command
 *========================================================================*/

int worldLoadTMXCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
            " world filename ?-pixels_per_meter N? ?-collision_layer NAME?", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
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
    w->pixels_per_meter = ppm;
    w->normalize = normalize;
    world_get_directory(argv[2], w->base_path, WORLD_MAX_PATH_LEN);
    tmx_xml_set_base_path(w->base_path);

    void *doc = tmx_xml_load(argv[2]);
    if (!doc) { Tcl_AppendResult(interp, "can't load ", argv[2], NULL); return TCL_ERROR; }
    void *map = tmx_xml_get_map(doc);
    if (!map) { tmx_xml_free(doc); Tcl_AppendResult(interp, "no map element", NULL); return TCL_ERROR; }
    
    w->map_width = tmx_xml_map_get_int(map, "width");
    w->map_height = tmx_xml_map_get_int(map, "height");
    w->tile_pixel_width = tmx_xml_map_get_int(map, "tilewidth");
    w->tile_pixel_height = tmx_xml_map_get_int(map, "tileheight");
    w->tile_size = w->tile_pixel_width / ppm;
    
    float norm_scale = 1.0f;
    float world_w = w->map_width * w->tile_size;
    float world_h = w->map_height * w->tile_size;
    if (normalize) {
        norm_scale = load_scale / world_w;
        w->norm_scale = norm_scale;
    }
    
    if (!w->has_world) {
        b2WorldDef wd = b2DefaultWorldDef();
        wd.gravity = w->gravity;
        w->world_id = b2CreateWorld(&wd);
        w->has_world = 1;
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
            atlas_id = world_load_atlas(w, src, tw, th, firstgid);
            if (atlas_id < 0) {
                fprintf(stderr, "world: failed to load atlas '%s'\n", src);
            }
        }
        
        if (name && w->sprite_sheet_count < WORLD_MAX_SPRITE_TILESETS) {
            SpriteSheet *ss = &w->sprite_sheets[w->sprite_sheet_count];
            strncpy(ss->name, name, 63);
            ss->name[63] = '\0';
            ss->firstgid = firstgid;
            ss->tile_width = tw;
            ss->tile_height = th;
            ss->atlas_id = atlas_id;
            ss->has_aseprite = 0;
            ss->tile_collision_count = 0;
            
            load_tile_collisions(ts, ss);

            if (aseprite_json) {
                char json_path[WORLD_MAX_PATH_LEN];
                world_join_path(json_path, WORLD_MAX_PATH_LEN, w->base_path, aseprite_json);
                if (aseprite_load(json_path, firstgid, &ss->aseprite) == 0) {
                    ss->has_aseprite = 1;
                }
            }
            
            w->sprite_sheet_count++;
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
                fprintf(stderr, "world: base64+%s compression not supported\n", comp);
                continue;
            }
            tiles = decode_base64_tiles(tmx_xml_data_get_text(data), lw, lh);
        }
        if (!tiles) continue;
        
        for (int ty = 0; ty < lh; ty++) {
            for (int tx = 0; tx < lw; tx++) {
                int gid = tiles[ty * lw + tx];
                if (gid == 0 || w->tile_count >= WORLD_MAX_TILES) continue;
                Atlas *atlas = world_find_atlas_for_gid(w, gid);
                if (!atlas) continue;
                
                float px = (tx + 0.5f) * w->tile_pixel_width;
                float py = (ty + 0.5f) * w->tile_pixel_height;
                float tile_x = px / ppm;
                float tile_y = (w->map_height * w->tile_pixel_height - py) / ppm;
                float tile_w = w->tile_size;
                float tile_h = w->tile_size;
                
                if (normalize) {
                    tile_x = (tile_x - world_w * 0.5f) * norm_scale;
                    tile_y = (tile_y - world_h * 0.5f) * norm_scale;
                    tile_w *= norm_scale;
                    tile_h *= norm_scale;
                }
                
                TileInstance *t = &w->tiles[w->tile_count++];
                t->x = tile_x;
                t->y = tile_y;
                t->w = tile_w;
                t->h = tile_h;
                t->atlas_id = (int)(atlas - w->atlases);
                world_get_tile_uvs(atlas, gid, &t->u0, &t->v0, &t->u1, &t->v1);
                t->has_body = 0;
                
                if (is_collision) {
                    int has_custom = (world_get_tile_collision(w, gid) != NULL);
                    
                    if (has_custom) {
                        snprintf(t->name, sizeof(t->name), "tile_%d_%d", tx, ty);
                        
                        b2BodyDef bd = b2DefaultBodyDef();
                        bd.type = b2_staticBody;
                        bd.position = (b2Vec2){tile_x, tile_y};
                        b2BodyId body = b2CreateBody(w->world_id, &bd);
                        
                        create_tile_collision_shapes(w, body, tile_w, tile_h, gid, t->name);
                        t->has_body = 1;
                        
                        int newentry;
                        Tcl_HashEntry *e = Tcl_CreateHashEntry(&w->body_table, t->name, &newentry);
                        b2BodyId *stored = malloc(sizeof(b2BodyId));
                        *stored = body;
                        Tcl_SetHashValue(e, stored);
                        w->body_count++;
                    } else {
                        int prev_gid = (tx > 0) ? tiles[ty * lw + tx - 1] : 0;
                        int prev_has_custom = (prev_gid != 0 && world_get_tile_collision(w, prev_gid) != NULL);
                        int is_run_start = (tx == 0 || prev_gid == 0 || prev_has_custom);
                        
                        if (is_run_start) {
                            int run_length = 1;
                            while (tx + run_length < lw) {
                                int next_gid = tiles[ty * lw + tx + run_length];
                                if (next_gid == 0) break;
                                if (world_get_tile_collision(w, next_gid) != NULL) break;
                                run_length++;
                            }
                            
                            snprintf(t->name, sizeof(t->name), "tile_%d_%d", tx, ty);
                            
                            float center_tile_x = tx + (run_length - 1) * 0.5f;
                            float center_px = (center_tile_x + 0.5f) * w->tile_pixel_width;
                            float body_x = center_px / ppm;
                            float body_y = (w->map_height * w->tile_pixel_height -
                                           (ty + 0.5f) * w->tile_pixel_height) / ppm;
                            float body_hw = (run_length * w->tile_size) * 0.5f;
                            float body_hh = w->tile_size * 0.5f;
                            
                            if (normalize) {
                                body_x = (body_x - world_w * 0.5f) * norm_scale;
                                body_y = (body_y - world_h * 0.5f) * norm_scale;
                                body_hw *= norm_scale;
                                body_hh *= norm_scale;
                            }
                            
                            b2BodyDef bd = b2DefaultBodyDef();
                            bd.type = b2_staticBody;
                            bd.position = (b2Vec2){body_x, body_y};
                            b2BodyId body = b2CreateBody(w->world_id, &bd);
                            
                            b2Polygon box = b2MakeBox(body_hw, body_hh);
                            b2ShapeDef sd = b2DefaultShapeDef();
                            sd.density = 1.0f;
                            sd.userData = (void *)t->name;
                            b2ShapeId shape = b2CreatePolygonShape(body, &sd, &box);
                            b2Shape_SetFriction(shape, 0.3f);
                            t->has_body = 1;
                            
                            int newentry;
                            Tcl_HashEntry *e = Tcl_CreateHashEntry(&w->body_table, t->name, &newentry);
                            b2BodyId *stored = malloc(sizeof(b2BodyId));
                            *stored = body;
                            Tcl_SetHashValue(e, stored);
                            w->body_count++;
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
            if (w->object_count >= WORLD_MAX_OBJECTS) break;
            TMXObject *to = &w->objects[w->object_count++];
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
            
            float obj_x = px / ppm;
            float obj_y = (w->map_height * w->tile_pixel_height - py) / ppm;
            float obj_w = ow / ppm;
            float obj_h = oh / ppm;
            
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
            
            to->prop_count = 0;
            void *props = tmx_xml_first_properties(obj);
            if (props) {
                for (void *prop = tmx_xml_first_property(props); 
                     prop && to->prop_count < WORLD_MAX_OBJECT_PROPS; 
                     prop = tmx_xml_next_property(prop)) {
                    TMXProperty *p = &to->props[to->prop_count++];
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
    w->tiles_dirty = 1;
    
    if (!normalize && w->auto_center) {
        float ox = -(w->map_width * w->tile_size) / 2.0f;
        float oy = -(w->map_height * w->tile_size) / 2.0f;
        w->offset_x = ox;
        w->offset_y = oy;
        for (int i = 0; i < w->tile_count; i++) {
            w->tiles[i].x += ox;
            w->tiles[i].y += oy;
        }
        Tcl_HashEntry *e; Tcl_HashSearch s;
        for (e = Tcl_FirstHashEntry(&w->body_table, &s); e; e = Tcl_NextHashEntry(&s)) {
            b2BodyId *body = Tcl_GetHashValue(e);
            b2Vec2 pos = b2Body_GetPosition(*body);
            pos.x += ox;
            pos.y += oy;
            b2Body_SetTransform(*body, pos, b2Body_GetRotation(*body));
        }
        for (int i = 0; i < w->object_count; i++) {
            w->objects[i].x += ox;
            w->objects[i].y += oy;
        }
    }
    
    if (normalize) {
        w->tile_size *= norm_scale;
        w->offset_x = 0;
        w->offset_y = 0;
    }
    
    Tcl_Obj *result = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("tiles",-1), Tcl_NewIntObj(w->tile_count));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("bodies",-1), Tcl_NewIntObj(w->body_count));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("objects",-1), Tcl_NewIntObj(w->object_count));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("atlases",-1), Tcl_NewIntObj(w->atlas_count));
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/*========================================================================
 * Object/Map Info Commands
 *========================================================================*/

int worldGetObjectsCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world ?type?", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    const char *filter = argc > 2 ? argv[2] : NULL;
    
    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    for (int i = 0; i < w->object_count; i++) {
        TMXObject *o = &w->objects[i];
        if (filter && strcmp(o->type, filter) != 0) continue;
        Tcl_Obj *d = Tcl_NewDictObj();
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("name",-1), Tcl_NewStringObj(o->name,-1));
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("type",-1), Tcl_NewStringObj(o->type,-1));
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("x",-1), Tcl_NewDoubleObj(o->x));
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("y",-1), Tcl_NewDoubleObj(o->y));
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("width",-1), Tcl_NewDoubleObj(o->width));
        Tcl_DictObjPut(interp, d, Tcl_NewStringObj("height",-1), Tcl_NewDoubleObj(o->height));
        
        if (o->prop_count > 0) {
            Tcl_Obj *props = Tcl_NewDictObj();
            for (int j = 0; j < o->prop_count; j++) {
                TMXProperty *p = &o->props[j];
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

int worldGetMapInfoCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
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
    
    Tcl_Obj *result = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("map_width",-1), Tcl_NewIntObj(w->map_width));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("map_height",-1), Tcl_NewIntObj(w->map_height));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("tile_pixel_width",-1), Tcl_NewIntObj(w->tile_pixel_width));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("tile_pixel_height",-1), Tcl_NewIntObj(w->tile_pixel_height));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("tile_size",-1), Tcl_NewDoubleObj(w->tile_size));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("pixels_per_meter",-1), Tcl_NewDoubleObj(w->pixels_per_meter));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("tile_count",-1), Tcl_NewIntObj(w->tile_count));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("sprite_count",-1), Tcl_NewIntObj(w->sprite_count));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("object_count",-1), Tcl_NewIntObj(w->object_count));
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("body_count",-1), Tcl_NewIntObj(w->body_count));
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

int worldSetOffsetCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world ox oy", NULL);
        return TCL_ERROR;
    }
    
    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist), argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;
    
    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    double ox, oy;
    if (Tcl_GetDouble(interp, argv[2], &ox) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &oy) != TCL_OK) return TCL_ERROR;
    
    for (int i = 0; i < w->tile_count; i++) {
        w->tiles[i].x += (float)ox;
        w->tiles[i].y += (float)oy;
    }
    for (int i = 0; i < w->sprite_count; i++) {
        w->sprites[i].x += (float)ox;
        w->sprites[i].y += (float)oy;
        if (w->sprites[i].has_body && b2Body_IsValid(w->sprites[i].body)) {
            b2Vec2 pos = b2Body_GetPosition(w->sprites[i].body);
            pos.x += (float)ox;
            pos.y += (float)oy;
            b2Body_SetTransform(w->sprites[i].body, pos, b2Body_GetRotation(w->sprites[i].body));
        }
    }
    Tcl_HashEntry *e; Tcl_HashSearch s;
    for (e = Tcl_FirstHashEntry(&w->body_table, &s); e; e = Tcl_NextHashEntry(&s)) {
        b2BodyId *body = Tcl_GetHashValue(e);
        if (b2Body_GetType(*body) == b2_staticBody) {
            b2Vec2 pos = b2Body_GetPosition(*body);
            pos.x += (float)ox;
            pos.y += (float)oy;
            b2Body_SetTransform(*body, pos, b2Body_GetRotation(*body));
        }
    }
    w->tiles_dirty = 1;
    return TCL_OK;
}

/*========================================================================
 * Command Registration
 *========================================================================*/

void world_tilemap_register_commands(Tcl_Interp *interp, OBJ_LIST *olist)
{
    Tcl_CreateCommand(interp, "worldLoadTMX",
        (Tcl_CmdProc*)worldLoadTMXCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldGetObjects",
        (Tcl_CmdProc*)worldGetObjectsCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldGetMapInfo",
        (Tcl_CmdProc*)worldGetMapInfoCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldSetOffset",
        (Tcl_CmdProc*)worldSetOffsetCmd, (ClientData)olist, NULL);
}
