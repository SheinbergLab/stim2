/*
 * tmx_xml.h
 *
 * C interface to TMX (Tiled Map Editor) XML parsing.
 * Implementation uses tinyxml2 in tmx_xml.cpp.
 */

#ifndef TMX_XML_H
#define TMX_XML_H

#ifdef __cplusplus
extern "C" {
#endif

/*========================================================================
 * Document Loading
 *========================================================================*/

void* tmx_xml_load(const char* filename);
void  tmx_xml_free(void* doc);
void  tmx_xml_set_base_path(const char* path);

/*========================================================================
 * Map Access
 *========================================================================*/

void*       tmx_xml_get_map(void* doc);
int         tmx_xml_map_get_int(void* map, const char* attr);
const char* tmx_xml_map_get_string(void* map, const char* attr);

/*========================================================================
 * Tileset Iteration
 *========================================================================*/

void*       tmx_xml_first_tileset(void* map);
void*       tmx_xml_next_tileset(void* tileset);
int         tmx_xml_tileset_get_int(void* tileset, const char* attr);
const char* tmx_xml_tileset_get_string(void* tileset, const char* attr);
const char* tmx_xml_tileset_get_name(void* tileset);
void*       tmx_xml_tileset_get_image(void* tileset);
void*       tmx_xml_tileset_get_properties(void* tileset);
const char* tmx_xml_tileset_get_property(void* tileset, const char* prop_name);

/*========================================================================
 * Tile Collision Shapes (within tileset)
 *========================================================================*/

void* tmx_xml_tileset_first_tile(void* tileset);
void* tmx_xml_tileset_next_tile(void* tile);
int   tmx_xml_tile_get_id(void* tile);
void* tmx_xml_tile_get_objectgroup(void* tile);

/*========================================================================
 * Layer Iteration
 *========================================================================*/

void*       tmx_xml_first_layer(void* map);
void*       tmx_xml_next_layer(void* layer);
const char* tmx_xml_layer_get_name(void* layer);
int         tmx_xml_layer_get_int(void* layer, const char* attr);
void*       tmx_xml_layer_get_data(void* layer);
const char* tmx_xml_data_get_text(void* data);
const char* tmx_xml_data_get_encoding(void* data);
const char* tmx_xml_data_get_compression(void* data);

/*========================================================================
 * Object Group / Object Iteration
 *========================================================================*/

void*       tmx_xml_first_objectgroup(void* map);
void*       tmx_xml_next_objectgroup(void* objgroup);
const char* tmx_xml_objectgroup_get_name(void* objgroup);

void*       tmx_xml_first_object(void* objgroup);
void*       tmx_xml_next_object(void* obj);
const char* tmx_xml_object_get_string(void* obj, const char* attr);
float       tmx_xml_object_get_float(void* obj, const char* attr, float def);
int         tmx_xml_object_is_point(void* obj);
int         tmx_xml_object_is_ellipse(void* obj);

/* Polygon/polyline collision shapes */
int         tmx_xml_object_has_polygon(void* obj);
const char* tmx_xml_object_get_polygon_points(void* obj);
int         tmx_xml_object_has_polyline(void* obj);
const char* tmx_xml_object_get_polyline_points(void* obj);

/*========================================================================
 * Properties
 *========================================================================*/

void*       tmx_xml_first_properties(void* element);
void*       tmx_xml_first_property(void* props);
void*       tmx_xml_next_property(void* prop);
const char* tmx_xml_property_get_name(void* prop);
const char* tmx_xml_property_get_value(void* prop);
const char* tmx_xml_property_get_type(void* prop);

#ifdef __cplusplus
}
#endif

#endif /* TMX_XML_H */
