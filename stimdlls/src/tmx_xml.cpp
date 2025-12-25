/*
 * tmx_xml.cpp
 *
 * C++ wrapper around tinyxml2 for parsing TMX files.
 * Provides a C-callable interface for use by tilemap.c and tmxloader.c
 *
 * This keeps all tinyxml2/C++ details isolated from the C code.
 */

#include "tinyxml2.h"
#include <cstring>
#include <cstdlib>

using namespace tinyxml2;

extern "C" {

/*
 * Load TMX file, returns XMLDocument pointer (or NULL on error)
 */
void* tmx_xml_load(const char* filename)
{
    XMLDocument* doc = new XMLDocument();
    if (doc->LoadFile(filename) != XML_SUCCESS) {
        delete doc;
        return nullptr;
    }
    return doc;
}

/*
 * Free XMLDocument
 */
void tmx_xml_free(void* doc)
{
    if (doc) {
        delete static_cast<XMLDocument*>(doc);
    }
}

/*
 * Get error string (if load failed)
 */
const char* tmx_xml_get_error(void* doc)
{
    if (!doc) return "null document";
    XMLDocument* d = static_cast<XMLDocument*>(doc);
    return d->ErrorStr();
}

/*
 * Get the <map> element
 */
void* tmx_xml_get_map(void* doc)
{
    if (!doc) return nullptr;
    XMLDocument* d = static_cast<XMLDocument*>(doc);
    return d->FirstChildElement("map");
}

/*
 * Get integer attribute from <map>
 */
int tmx_xml_map_get_int(void* map, const char* attr)
{
    if (!map) return 0;
    XMLElement* m = static_cast<XMLElement*>(map);
    return m->IntAttribute(attr, 0);
}

/*
 * Get string attribute from <map>
 */
const char* tmx_xml_map_get_string(void* map, const char* attr)
{
    if (!map) return nullptr;
    XMLElement* m = static_cast<XMLElement*>(map);
    return m->Attribute(attr);
}

/*
 * Tileset iteration
 */
void* tmx_xml_first_tileset(void* map)
{
    if (!map) return nullptr;
    XMLElement* m = static_cast<XMLElement*>(map);
    return m->FirstChildElement("tileset");
}

void* tmx_xml_next_tileset(void* tileset)
{
    if (!tileset) return nullptr;
    XMLElement* ts = static_cast<XMLElement*>(tileset);
    return ts->NextSiblingElement("tileset");
}

/*
 * Load and cache external tileset, returns the <tileset> element from .tsx
 * Uses a simple static cache (good enough for sequential tileset iteration)
 */
static char g_base_path[512] = {0};

void tmx_xml_set_base_path(const char* path)
{
    if (path) {
        strncpy(g_base_path, path, sizeof(g_base_path) - 1);
    } else {
        g_base_path[0] = '\0';
    }
}

static XMLElement* load_external_tileset(const char* tsx_path)
{
    static XMLDocument tsx_doc;
    static char loaded_path[512] = {0};
    
    /* Build full path */
    char full_path[512];
    if (g_base_path[0] && tsx_path[0] != '/') {
        snprintf(full_path, sizeof(full_path), "%s/%s", g_base_path, tsx_path);
    } else {
        strncpy(full_path, tsx_path, sizeof(full_path) - 1);
    }
    
    if (strcmp(loaded_path, full_path) != 0) {
        if (tsx_doc.LoadFile(full_path) != XML_SUCCESS) {
            fprintf(stderr, "tmx_xml: failed to load '%s': %s\n", 
                    full_path, tsx_doc.ErrorStr());
            loaded_path[0] = '\0';
            return nullptr;
        }
        strncpy(loaded_path, full_path, sizeof(loaded_path) - 1);
    }
    return tsx_doc.FirstChildElement("tileset");
}

int tmx_xml_tileset_get_int(void* tileset, const char* attr)
{
    if (!tileset) return 0;
    XMLElement* ts = static_cast<XMLElement*>(tileset);
    
    /* firstgid is always in the TMX, not the external TSX */
    if (strcmp(attr, "firstgid") == 0) {
        return ts->IntAttribute(attr, 0);
    }
    
    /* Check for external tileset for other attributes */
    const char* tsx_source = ts->Attribute("source");
    if (tsx_source) {
        XMLElement* ext_ts = load_external_tileset(tsx_source);
        if (!ext_ts) return 0;
        ts = ext_ts;
    }
    
    return ts->IntAttribute(attr, 0);
}

const char* tmx_xml_tileset_get_string(void* tileset, const char* attr)
{
    if (!tileset) return nullptr;
    XMLElement* ts = static_cast<XMLElement*>(tileset);
    
    /* Check for external tileset */
    const char* tsx_source = ts->Attribute("source");
    if (tsx_source) {
        XMLElement* ext_ts = load_external_tileset(tsx_source);
        if (!ext_ts) return nullptr;
        ts = ext_ts;  /* Use external tileset element from here on */
    }
    
    /* Handle "source" -> get nested image source */
    if (strcmp(attr, "source") == 0) {
        XMLElement* img = ts->FirstChildElement("image");
        return img ? img->Attribute("source") : nullptr;
    }
    
    return ts->Attribute(attr);
}

void* tmx_xml_tileset_get_image(void* tileset)
{
    if (!tileset) return nullptr;
    XMLElement* ts = static_cast<XMLElement*>(tileset);
    return ts->FirstChildElement("image");
}

/*
 * Layer iteration
 */
void* tmx_xml_first_layer(void* map)
{
    if (!map) return nullptr;
    XMLElement* m = static_cast<XMLElement*>(map);
    return m->FirstChildElement("layer");
}

void* tmx_xml_next_layer(void* layer)
{
    if (!layer) return nullptr;
    XMLElement* l = static_cast<XMLElement*>(layer);
    return l->NextSiblingElement("layer");
}

const char* tmx_xml_layer_get_name(void* layer)
{
    if (!layer) return nullptr;
    XMLElement* l = static_cast<XMLElement*>(layer);
    return l->Attribute("name");
}

int tmx_xml_layer_get_int(void* layer, const char* attr)
{
    if (!layer) return 0;
    XMLElement* l = static_cast<XMLElement*>(layer);
    return l->IntAttribute(attr, 0);
}

void* tmx_xml_layer_get_data(void* layer)
{
    if (!layer) return nullptr;
    XMLElement* l = static_cast<XMLElement*>(layer);
    return l->FirstChildElement("data");
}

const char* tmx_xml_data_get_text(void* data)
{
    if (!data) return nullptr;
    XMLElement* d = static_cast<XMLElement*>(data);
    return d->GetText();
}

const char* tmx_xml_data_get_compression(void* data)
{
    if (!data) return nullptr;
    XMLElement* d = static_cast<XMLElement*>(data);
    return d->Attribute("compression");
}

const char* tmx_xml_data_get_encoding(void* data)
{
    if (!data) return nullptr;
    XMLElement* d = static_cast<XMLElement*>(data);
    return d->Attribute("encoding");
}

/*
 * Object group iteration
 */
void* tmx_xml_first_objectgroup(void* map)
{
    if (!map) return nullptr;
    XMLElement* m = static_cast<XMLElement*>(map);
    return m->FirstChildElement("objectgroup");
}

void* tmx_xml_next_objectgroup(void* objgroup)
{
    if (!objgroup) return nullptr;
    XMLElement* og = static_cast<XMLElement*>(objgroup);
    return og->NextSiblingElement("objectgroup");
}

const char* tmx_xml_objectgroup_get_name(void* objgroup)
{
    if (!objgroup) return nullptr;
    XMLElement* og = static_cast<XMLElement*>(objgroup);
    return og->Attribute("name");
}

/*
 * Object iteration
 */
void* tmx_xml_first_object(void* objgroup)
{
    if (!objgroup) return nullptr;
    XMLElement* og = static_cast<XMLElement*>(objgroup);
    return og->FirstChildElement("object");
}

void* tmx_xml_next_object(void* obj)
{
    if (!obj) return nullptr;
    XMLElement* o = static_cast<XMLElement*>(obj);
    return o->NextSiblingElement("object");
}

const char* tmx_xml_object_get_string(void* obj, const char* attr)
{
    if (!obj) return nullptr;
    XMLElement* o = static_cast<XMLElement*>(obj);
    return o->Attribute(attr);
}

float tmx_xml_object_get_float(void* obj, const char* attr, float def)
{
    if (!obj) return def;
    XMLElement* o = static_cast<XMLElement*>(obj);
    return o->FloatAttribute(attr, def);
}

int tmx_xml_object_is_point(void* obj)
{
    if (!obj) return 0;
    XMLElement* o = static_cast<XMLElement*>(obj);
    return o->FirstChildElement("point") != nullptr ? 1 : 0;
}

int tmx_xml_object_is_ellipse(void* obj)
{
    if (!obj) return 0;
    XMLElement* o = static_cast<XMLElement*>(obj);
    return o->FirstChildElement("ellipse") != nullptr ? 1 : 0;
}

/*
 * Property iteration
 */
void* tmx_xml_first_properties(void* element)
{
    if (!element) return nullptr;
    XMLElement* e = static_cast<XMLElement*>(element);
    return e->FirstChildElement("properties");
}

void* tmx_xml_first_property(void* props)
{
    if (!props) return nullptr;
    XMLElement* p = static_cast<XMLElement*>(props);
    return p->FirstChildElement("property");
}

void* tmx_xml_next_property(void* prop)
{
    if (!prop) return nullptr;
    XMLElement* p = static_cast<XMLElement*>(prop);
    return p->NextSiblingElement("property");
}

const char* tmx_xml_property_get_name(void* prop)
{
    if (!prop) return nullptr;
    XMLElement* p = static_cast<XMLElement*>(prop);
    return p->Attribute("name");
}

const char* tmx_xml_property_get_value(void* prop)
{
    if (!prop) return nullptr;
    XMLElement* p = static_cast<XMLElement*>(prop);
    /* Value can be in "value" attribute or as text content */
    const char* val = p->Attribute("value");
    if (val) return val;
    return p->GetText();
}

const char* tmx_xml_property_get_type(void* prop)
{
    if (!prop) return nullptr;
    XMLElement* p = static_cast<XMLElement*>(prop);
    const char* type = p->Attribute("type");
    return type ? type : "string";  /* default to string */
}
/*
 * Get tileset name attribute
 */
const char* tmx_xml_tileset_get_name(void* tileset)
{
    if (!tileset) return nullptr;
    XMLElement* ts = static_cast<XMLElement*>(tileset);
    
    /* Check for external tileset */
    const char* tsx_source = ts->Attribute("source");
    if (tsx_source) {
        XMLElement* ext_ts = load_external_tileset(tsx_source);
        if (ext_ts) {
            return ext_ts->Attribute("name");
        }
        return nullptr;
    }
    
    return ts->Attribute("name");
}

/*
 * Get the <properties> element from a tileset (for custom properties)
 */
void* tmx_xml_tileset_get_properties(void* tileset)
{
    if (!tileset) return nullptr;
    XMLElement* ts = static_cast<XMLElement*>(tileset);
    
    /* Check for external tileset */
    const char* tsx_source = ts->Attribute("source");
    if (tsx_source) {
        XMLElement* ext_ts = load_external_tileset(tsx_source);
        if (ext_ts) {
            return ext_ts->FirstChildElement("properties");
        }
        return nullptr;
    }
    
    return ts->FirstChildElement("properties");
}

/*
 * Helper to get a specific property value from a tileset by property name
 * Returns NULL if property not found
 */
const char* tmx_xml_tileset_get_property(void* tileset, const char* prop_name)
{
    if (!tileset || !prop_name) return nullptr;
    
    void* props = tmx_xml_tileset_get_properties(tileset);
    if (!props) return nullptr;
    
    XMLElement* properties = static_cast<XMLElement*>(props);
    for (XMLElement* prop = properties->FirstChildElement("property"); 
         prop; 
         prop = prop->NextSiblingElement("property")) {
        const char* name = prop->Attribute("name");
        if (name && strcmp(name, prop_name) == 0) {
            return prop->Attribute("value");
        }
    }
    return nullptr;
}

/*
 * Additions to tmx_xml.cpp for tile collision shape support
 * Add these functions to your existing tmx_xml.cpp file
 */

/*
 * Tile iteration within a tileset (for collision shapes)
 * Note: Must handle external tilesets (.tsx files)
 */
void* tmx_xml_tileset_first_tile(void* tileset)
{
    if (!tileset) return nullptr;
    XMLElement* ts = static_cast<XMLElement*>(tileset);
    
    /* Check for external tileset */
    const char* tsx_source = ts->Attribute("source");
    if (tsx_source) {
        XMLElement* ext_ts = load_external_tileset(tsx_source);
        if (!ext_ts) return nullptr;
        ts = ext_ts;
    }
    
    return ts->FirstChildElement("tile");
}

void* tmx_xml_tileset_next_tile(void* tile)
{
    if (!tile) return nullptr;
    XMLElement* t = static_cast<XMLElement*>(tile);
    return t->NextSiblingElement("tile");
}

/*
 * Get tile local ID (0-based index within tileset)
 */
int tmx_xml_tile_get_id(void* tile)
{
    if (!tile) return -1;
    XMLElement* t = static_cast<XMLElement*>(tile);
    return t->IntAttribute("id", -1);
}

/*
 * Get the objectgroup (collision shapes) for a tile
 */
void* tmx_xml_tile_get_objectgroup(void* tile)
{
    if (!tile) return nullptr;
    XMLElement* t = static_cast<XMLElement*>(tile);
    return t->FirstChildElement("objectgroup");
}

/*
 * Check if object has a polygon child element
 */
int tmx_xml_object_has_polygon(void* obj)
{
    if (!obj) return 0;
    XMLElement* o = static_cast<XMLElement*>(obj);
    return o->FirstChildElement("polygon") != nullptr ? 1 : 0;
}

/*
 * Get polygon points string (format: "x1,y1 x2,y2 x3,y3 ...")
 * Points are relative to the object's x,y position
 */
const char* tmx_xml_object_get_polygon_points(void* obj)
{
    if (!obj) return nullptr;
    XMLElement* o = static_cast<XMLElement*>(obj);
    XMLElement* poly = o->FirstChildElement("polygon");
    if (!poly) return nullptr;
    return poly->Attribute("points");
}

/*
 * Check if object has a polyline child element
 */
int tmx_xml_object_has_polyline(void* obj)
{
    if (!obj) return 0;
    XMLElement* o = static_cast<XMLElement*>(obj);
    return o->FirstChildElement("polyline") != nullptr ? 1 : 0;
}

/*
 * Get polyline points string
 */
const char* tmx_xml_object_get_polyline_points(void* obj)
{
    if (!obj) return nullptr;
    XMLElement* o = static_cast<XMLElement*>(obj);
    XMLElement* poly = o->FirstChildElement("polyline");
    if (!poly) return nullptr;
    return poly->Attribute("points");
}

} /* extern "C" */