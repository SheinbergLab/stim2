/*
 * aseprite_json.cpp
 *
 * Parser for Aseprite JSON sprite sheet exports.
 * Extracts frame tags (animations) with frame indices and hitbox slices.
 *
 * Uses nlohmann/json (header-only).
 * Get it from: https://github.com/nlohmann/json
 */

#include "nlohmann/json.hpp"
#include <fstream>
#include <cstring>
#include <cstdio>

#include "aseprite_json.h"

using json = nlohmann::json;

extern "C" {

/*
 * Load and parse Aseprite JSON file
 * 
 * Parameters:
 *   json_path - path to .json file exported from Aseprite
 *   firstgid  - GID offset to add to frame indices (from TMX tileset)
 *   data      - output structure to fill
 *
 * Returns: 0 on success, -1 on error
 */
int aseprite_load(const char* json_path, int firstgid, AsepriteData* data)
{
    if (!json_path || !data) return -1;
    
    /* Clear output */
    memset(data, 0, sizeof(AsepriteData));
    
    /* Load JSON file */
    std::ifstream f(json_path);
    if (!f.is_open()) {
        fprintf(stderr, "aseprite_json: can't open '%s'\n", json_path);
        return -1;
    }
    
    json j;
    try {
        f >> j;
    } catch (json::parse_error& e) {
        fprintf(stderr, "aseprite_json: parse error in '%s': %s\n", json_path, e.what());
        return -1;
    }
    
    /* Get frame size from first frame or meta */
    if (j.contains("frames")) {
        auto& frames = j["frames"];
        if (frames.is_object() && !frames.empty()) {
            /* Hash format: {"filename": {frame: {...}}, ...} */
            auto first = frames.begin();
            if (first->contains("frame")) {
                data->frame_width = (*first)["frame"]["w"].get<int>();
                data->frame_height = (*first)["frame"]["h"].get<int>();
            }
            data->total_frames = frames.size();
        } else if (frames.is_array() && !frames.empty()) {
            /* Array format: [{frame: {...}}, ...] */
            if (frames[0].contains("frame")) {
                data->frame_width = frames[0]["frame"]["w"].get<int>();
                data->frame_height = frames[0]["frame"]["h"].get<int>();
            }
            data->total_frames = frames.size();
        }
    }
    
    /* Parse frameTags from meta */
    if (!j.contains("meta") || !j["meta"].contains("frameTags")) {
        fprintf(stderr, "aseprite_json: no frameTags in '%s'\n", json_path);
        return -1;
    }
    
    auto& tags = j["meta"]["frameTags"];
    data->animation_count = 0;
    
    for (auto& tag : tags) {
        if (data->animation_count >= ASE_MAX_ANIMATIONS) break;
        
        AsepriteAnimation* anim = &data->animations[data->animation_count];
        
        /* Get name */
        std::string name = tag["name"].get<std::string>();
        strncpy(anim->name, name.c_str(), ASE_MAX_NAME_LEN - 1);
        anim->name[ASE_MAX_NAME_LEN - 1] = '\0';
        
        /* Get frame range */
        int from = tag["from"].get<int>();
        int to = tag["to"].get<int>();
        
        /* Build frame list with GID offset */
        anim->frame_count = 0;
        for (int i = from; i <= to && anim->frame_count < ASE_MAX_FRAMES; i++) {
            anim->frames[anim->frame_count++] = firstgid + i;
        }
        
        /* Compute default FPS from frame durations for THIS animation */
        float total_duration_ms = 0;
        int duration_count = 0;
        
        if (j.contains("frames")) {
            auto& frames = j["frames"];
            if (frames.is_object()) {
                /* Hash format - iterate and match by index */
                int idx = 0;
                for (auto it = frames.begin(); it != frames.end(); ++it, ++idx) {
                    if (idx >= from && idx <= to && it->contains("duration")) {
                        float dur = (*it)["duration"].get<float>();
                        /* Filter out hold frames (> 1 second) */
                        if (dur < 1000) {
                            total_duration_ms += dur;
                            duration_count++;
                        }
                    }
                }
            } else if (frames.is_array()) {
                /* Array format */
                for (int i = from; i <= to && i < (int)frames.size(); i++) {
                    if (frames[i].contains("duration")) {
                        float dur = frames[i]["duration"].get<float>();
                        /* Filter out hold frames (> 1 second) */
                        if (dur < 1000) {
                            total_duration_ms += dur;
                            duration_count++;
                        }
                    }
                }
            }
        }
        
        if (duration_count > 0 && total_duration_ms > 0) {
            float avg_duration_ms = total_duration_ms / duration_count;
            anim->default_fps = 1000.0f / avg_duration_ms;
        } else {
            anim->default_fps = 10.0f;  /* default fallback */
        }
        
        data->animation_count++;
    }
    
    /* Parse slices for hitbox */
    data->has_hitbox = 0;
    if (j["meta"].contains("slices")) {
        for (auto& slice : j["meta"]["slices"]) {
            std::string name = slice["name"].get<std::string>();
            if (name == "hitbox" && slice.contains("keys") && !slice["keys"].empty()) {
                auto& key = slice["keys"][0];  /* Use first keyframe only */
                if (key.contains("bounds")) {
                    auto& bounds = key["bounds"];
                    data->hitbox_x = bounds["x"].get<int>();
                    data->hitbox_y = bounds["y"].get<int>();
                    data->hitbox_w = bounds["w"].get<int>();
                    data->hitbox_h = bounds["h"].get<int>();
                    
                    /* Calculate ratios and offsets */
                    float frame_w = (float)data->frame_width;
                    float frame_h = (float)data->frame_height;
                    
                    data->hitbox_width_ratio = (float)data->hitbox_w / frame_w;
                    data->hitbox_height_ratio = (float)data->hitbox_h / frame_h;
                    
                    /* Offset from frame center to hitbox center (normalized) */
                    float hitbox_center_x = data->hitbox_x + data->hitbox_w * 0.5f;
                    float hitbox_center_y = data->hitbox_y + data->hitbox_h * 0.5f;
                    float frame_center_x = frame_w * 0.5f;
                    float frame_center_y = frame_h * 0.5f;
                    
                    /* Normalize offset (-0.5 to 0.5 range) */
                    data->hitbox_offset_x = (hitbox_center_x - frame_center_x) / frame_w;
                    /* Y is inverted: Aseprite Y=0 is top, game Y=0 is bottom */
                    data->hitbox_offset_y = (frame_center_y - hitbox_center_y) / frame_h;
                    
                    data->has_hitbox = 1;
                    
                    fprintf(stderr, "aseprite: hitbox bounds (%d,%d %dx%d) ratio (%.2f,%.2f) offset (%.3f,%.3f)\n",
                            data->hitbox_x, data->hitbox_y, data->hitbox_w, data->hitbox_h,
                            data->hitbox_width_ratio, data->hitbox_height_ratio,
                            data->hitbox_offset_x, data->hitbox_offset_y);
                }
                break;  /* Found hitbox, stop looking */
            }
        }
    }
    
    return 0;
}

/*
 * Free any resources (currently none, but for API consistency)
 */
void aseprite_free(AsepriteData* data)
{
    if (data) {
        memset(data, 0, sizeof(AsepriteData));
    }
}

/*
 * Debug: print loaded animation data
 */
void aseprite_print(AsepriteData* data)
{
    if (!data) return;
    printf("Aseprite data: %d animations, %d total frames (%dx%d)\n",
           data->animation_count, data->total_frames, 
           data->frame_width, data->frame_height);
    for (int i = 0; i < data->animation_count; i++) {
        AsepriteAnimation* a = &data->animations[i];
        printf("  [%d] %s: %d frames @ %.1f fps, GIDs: ",
               i, a->name, a->frame_count, a->default_fps);
        for (int j = 0; j < a->frame_count && j < 8; j++) {
            printf("%d ", a->frames[j]);
        }
        if (a->frame_count > 8) printf("...");
        printf("\n");
    }
    if (data->has_hitbox) {
        printf("  Hitbox: (%d,%d) %dx%d, ratio (%.2f,%.2f), offset (%.3f,%.3f)\n",
               data->hitbox_x, data->hitbox_y, data->hitbox_w, data->hitbox_h,
               data->hitbox_width_ratio, data->hitbox_height_ratio,
               data->hitbox_offset_x, data->hitbox_offset_y);
    }
}

/*
 * Find animation by name, returns pointer or NULL
 */
AsepriteAnimation* aseprite_find_animation(AsepriteData* data, const char* name)
{
    if (!data || !name) return nullptr;
    for (int i = 0; i < data->animation_count; i++) {
        if (strcmp(data->animations[i].name, name) == 0) {
            return &data->animations[i];
        }
    }
    return nullptr;
}

} /* extern "C" */