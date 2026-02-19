/*
 * world_atlas.c
 *
 * Texture atlas management for 2D world module.
 * Handles loading textures, UV coordinate calculations.
 */

#include "world_internal.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdio.h>
#include <string.h>

/*========================================================================
 * Atlas Lookup
 *========================================================================*/

Atlas* world_find_atlas_for_gid(World *w, int gid)
{
    Atlas *best = NULL;
    for (int i = 0; i < w->atlas_count; i++)
        if (w->atlases[i].firstgid <= gid)
            if (!best || w->atlases[i].firstgid > best->firstgid)
                best = &w->atlases[i];
    return best;
}

void world_get_tile_uvs(Atlas *a, int gid, float *u0, float *v0, float *u1, float *v1)
{
    int local = gid - a->firstgid;
    if (local < 0) local = 0;
    int col = local % a->cols, row = local / a->cols;
    *u0 = col * a->tile_u; 
    *v0 = row * a->tile_v;
    *u1 = *u0 + a->tile_u; 
    *v1 = *v0 + a->tile_v;
}

/*========================================================================
 * Texture Loading
 *========================================================================*/

/* Load a raw texture, returns GLuint texture ID or 0 on failure */
GLuint world_load_texture(const char *path, int *out_width, int *out_height)
{
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) {
        fprintf(stderr, "world: can't load texture %s\n", path);
        return 0;
    }
    
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    stbi_image_free(data);
    
    if (out_width) *out_width = w;
    if (out_height) *out_height = h;
    
    return texture;
}

/*========================================================================
 * Atlas Loading (grid-based tilesets)
 *========================================================================*/

int world_load_atlas(World *w, const char *file, int tw, int th, int firstgid)
{
    if (w->atlas_count >= WORLD_MAX_ATLASES) return -1;
    
    char path[WORLD_MAX_PATH_LEN];
    if (file[0] == '/') {
      strncpy(path, file, WORLD_MAX_PATH_LEN - 1);
      path[WORLD_MAX_PATH_LEN - 1] = '\0';
    } else {
      world_join_path(path, WORLD_MAX_PATH_LEN, w->base_path, file);
    }

    int img_w, img_h;
    GLuint texture = world_load_texture(path, &img_w, &img_h);
    if (!texture) return -1;
    
    Atlas *a = &w->atlases[w->atlas_count];
    strncpy(a->filename, file, WORLD_MAX_PATH_LEN - 1);
    a->width = img_w;
    a->height = img_h;
    a->tile_width = tw;
    a->tile_height = th;
    a->cols = img_w / tw;
    a->rows = img_h / th;
    a->tile_u = (float)tw / img_w;
    a->tile_v = (float)th / img_h;
    a->firstgid = firstgid;
    a->texture = texture;
    
    return w->atlas_count++;
}

/*========================================================================
 * Packed Atlas Loading (sprite sheets with variable-size frames)
 *========================================================================*/

int world_load_packed_atlas(World *w, const char *file)
{
    if (w->atlas_count >= WORLD_MAX_ATLASES) return -1;
    
    char path[WORLD_MAX_PATH_LEN];
    world_join_path(path, WORLD_MAX_PATH_LEN, w->base_path, file);
    
    int img_w, img_h;
    GLuint texture = world_load_texture(path, &img_w, &img_h);
    if (!texture) return -1;
    
    Atlas *a = &w->atlases[w->atlas_count];
    strncpy(a->filename, file, WORLD_MAX_PATH_LEN - 1);
    a->width = img_w;
    a->height = img_h;
    
    /* For packed atlases, tile size is meaningless */
    a->tile_width = 0;
    a->tile_height = 0;
    a->cols = 0;
    a->rows = 0;
    a->tile_u = 0;
    a->tile_v = 0;
    a->firstgid = 0;
    a->texture = texture;
    
    return w->atlas_count++;
}
