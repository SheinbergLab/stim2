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

int tmx_xml_tileset_get_int(void* tileset, const char* attr)
{
    if (!tileset) return 0;
    XMLElement* ts = static_cast<XMLElement*>(tileset);
    return ts->IntAttribute(attr, 0);
}

const char* tmx_xml_tileset_get_string(void* tileset, const char* attr)
{
    if (!tileset) return nullptr;
    XMLElement* ts = static_cast<XMLElement*>(tileset);
    
    /* Handle nested image element for "source" */
    if (strcmp(attr, "source") == 0) {
        XMLElement* img = ts->FirstChildElement("image");
        if (img) return img->Attribute("source");
        return nullptr;
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

} /* extern "C" */