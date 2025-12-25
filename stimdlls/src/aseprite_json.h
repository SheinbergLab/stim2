/*
 * aseprite_json.h
 *
 * Parser for Aseprite JSON sprite sheet exports.
 * C-compatible interface.
 */

#ifndef ASEPRITE_JSON_H
#define ASEPRITE_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#define ASE_MAX_ANIMATIONS 16
#define ASE_MAX_FRAMES 32
#define ASE_MAX_NAME_LEN 32

typedef struct {
    char name[ASE_MAX_NAME_LEN];
    int frames[ASE_MAX_FRAMES];
    int frame_count;
    float default_fps;
} AsepriteAnimation;

typedef struct {
    AsepriteAnimation animations[ASE_MAX_ANIMATIONS];
    int animation_count;
    int total_frames;
    int frame_width;
    int frame_height;
    
    /* Hitbox from slice (first keyframe) */
    int has_hitbox;
    int hitbox_x, hitbox_y;         /* position in frame (pixels, from top-left) */
    int hitbox_w, hitbox_h;         /* size (pixels) */
    float hitbox_width_ratio;       /* hitbox_w / frame_w */
    float hitbox_height_ratio;      /* hitbox_h / frame_h */
    float hitbox_offset_x;          /* offset from frame center (normalized, -0.5 to 0.5) */
    float hitbox_offset_y;          /* offset from frame center (normalized, Y inverted for game coords) */
} AsepriteData;

/* Load and parse Aseprite JSON, applying firstgid offset to frame indices */
int aseprite_load(const char* json_path, int firstgid, AsepriteData* data);

/* Free resources (for API consistency) */
void aseprite_free(AsepriteData* data);

/* Debug print */
void aseprite_print(AsepriteData* data);

/* Find animation by name */
AsepriteAnimation* aseprite_find_animation(AsepriteData* data, const char* name);

#ifdef __cplusplus
}
#endif

#endif /* ASEPRITE_JSON_H */