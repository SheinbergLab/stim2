/*
 * world_render.c
 *
 * Rendering system for 2D world module.
 * Handles shaders, VBOs, and draw calls for tiles and sprites.
 */

#include "world_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/*========================================================================
 * Shader Sources
 *========================================================================*/

#ifdef STIM2_USE_GLES
static const char *world_vs =
    "#version 300 es\nprecision mediump float;\n"
    "layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV;\n"
    "out vec2 vUV; uniform mat4 projMat, modelviewMat;\n"
    "void main() { gl_Position = projMat * modelviewMat * vec4(aPos,0,1); vUV = aUV; }\n";
static const char *world_fs =
    "#version 300 es\nprecision mediump float;\n"
    "in vec2 vUV; out vec4 fragColor; uniform sampler2D atlas;\n"
    "void main() { vec4 c = texture(atlas, vUV); if(c.a<0.1) discard; fragColor = c; }\n";
#else
static const char *world_vs =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV;\n"
    "out vec2 vUV; uniform mat4 projMat, modelviewMat;\n"
    "void main() { gl_Position = projMat * modelviewMat * vec4(aPos,0,1); vUV = aUV; }\n";
static const char *world_fs =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 fragColor; uniform sampler2D atlas;\n"
    "void main() { vec4 c = texture(atlas, vUV); if(c.a<0.1) discard; fragColor = c; }\n";
#endif

/*========================================================================
 * Shader Compilation
 *========================================================================*/

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, NULL, log);
        fprintf(stderr, "world shader: %s\n", log);
        return 0;
    }
    return s;
}

/*========================================================================
 * GL Initialization
 *========================================================================*/

int world_init_gl(World *w)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, world_vs);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, world_fs);
    if (!vs || !fs) return -1;
    
    w->shader_program = glCreateProgram();
    glAttachShader(w->shader_program, vs);
    glAttachShader(w->shader_program, fs);
    glLinkProgram(w->shader_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    GLint ok;
    glGetProgramiv(w->shader_program, GL_LINK_STATUS, &ok);
    if (!ok) return -1;
    
    w->u_texture = glGetUniformLocation(w->shader_program, "atlas");
    w->u_modelview = glGetUniformLocation(w->shader_program, "modelviewMat");
    w->u_projection = glGetUniformLocation(w->shader_program, "projMat");
    
    /* Tile VBO */
    glGenVertexArrays(1, &w->vao);
    glGenBuffers(1, &w->vbo);
    glBindVertexArray(w->vao);
    glBindBuffer(GL_ARRAY_BUFFER, w->vbo);
    glBufferData(GL_ARRAY_BUFFER, WORLD_MAX_TILES * 6 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    
    /* Sprite VBO */
    glGenVertexArrays(1, &w->sprite_vao);
    glGenBuffers(1, &w->sprite_vbo);
    glBindVertexArray(w->sprite_vao);
    glBindBuffer(GL_ARRAY_BUFFER, w->sprite_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glBindVertexArray(0);
    
    return 0;
}

/*========================================================================
 * VBO Rebuilding
 *========================================================================*/

void world_rebuild_vbo(World *w)
{
    if (w->tile_count == 0) return;
    
    float *v = malloc(w->tile_count * 6 * 4 * sizeof(float));
    int vi = 0;
    
    for (int i = 0; i < w->tile_count; i++) {
        TileInstance *t = &w->tiles[i];
        float x0 = t->x - t->w * 0.5f, y0 = t->y - t->h * 0.5f;
        float x1 = t->x + t->w * 0.5f, y1 = t->y + t->h * 0.5f;
        v[vi++] = x0; v[vi++] = y0; v[vi++] = t->u0; v[vi++] = t->v1;
        v[vi++] = x1; v[vi++] = y0; v[vi++] = t->u1; v[vi++] = t->v1;
        v[vi++] = x1; v[vi++] = y1; v[vi++] = t->u1; v[vi++] = t->v0;
        v[vi++] = x0; v[vi++] = y0; v[vi++] = t->u0; v[vi++] = t->v1;
        v[vi++] = x1; v[vi++] = y1; v[vi++] = t->u1; v[vi++] = t->v0;
        v[vi++] = x0; v[vi++] = y1; v[vi++] = t->u0; v[vi++] = t->v0;
    }
    
    glBindBuffer(GL_ARRAY_BUFFER, w->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vi * sizeof(float), v);
    free(v);
    w->tiles_dirty = 0;
}

/*========================================================================
 * Sprite Vertex Building
 *========================================================================*/

void world_build_sprite_verts(World *w, Sprite *sp, float *v)
{
    float width, height, u0, v0, u1, v1;
    
    /* Check if using sprite sheet */
    if (sp->uses_sprite_sheet) {
        SpriteSheet *ss = &w->sprite_sheets[sp->sprite_sheet_id];
        
        /* Use canonical size for consistent rendering across frames */
        width = ss->canonical_w / w->pixels_per_meter;
        height = ss->canonical_h / w->pixels_per_meter;
        
        /* Get UVs for current frame */
        u0 = ss->frames[sp->current_frame].u0;
        v0 = ss->frames[sp->current_frame].v0;
        u1 = ss->frames[sp->current_frame].u1;
        v1 = ss->frames[sp->current_frame].v1;
    } else {
        /* Grid-based tileset */
        width = sp->w;
        height = sp->h;
        u0 = sp->u0;
        v0 = sp->v0;
        u1 = sp->u1;
        v1 = sp->v1;
    }
    
    float hw = width * 0.5f, hh = height * 0.5f;
    float c = cosf(sp->angle), s = sinf(sp->angle);
    float corners[4][2] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
    float r[4][2];
    
    for (int i = 0; i < 4; i++) {
        r[i][0] = sp->x + corners[i][0] * c - corners[i][1] * s;
        r[i][1] = sp->y + corners[i][0] * s + corners[i][1] * c;
    }
    
    int vi = 0;
    v[vi++] = r[0][0]; v[vi++] = r[0][1]; v[vi++] = u0; v[vi++] = v1;
    v[vi++] = r[1][0]; v[vi++] = r[1][1]; v[vi++] = u1; v[vi++] = v1;
    v[vi++] = r[2][0]; v[vi++] = r[2][1]; v[vi++] = u1; v[vi++] = v0;
    v[vi++] = r[0][0]; v[vi++] = r[0][1]; v[vi++] = u0; v[vi++] = v1;
    v[vi++] = r[2][0]; v[vi++] = r[2][1]; v[vi++] = u1; v[vi++] = v0;
    v[vi++] = r[3][0]; v[vi++] = r[3][1]; v[vi++] = u0; v[vi++] = v0;
}

/*========================================================================
 * Draw
 *========================================================================*/

void world_render(World *w)
{
    if (w->tile_count == 0 && w->sprite_count == 0) return;
    if (w->tiles_dirty) world_rebuild_vbo(w);
    
    float mv[16], pr[16];
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(w->shader_program);
    stimGetMatrix(STIM_MODELVIEW_MATRIX, mv);
    stimGetMatrix(STIM_PROJECTION_MATRIX, pr);
    
    /* Apply camera offset to modelview matrix */
    mv[12] -= w->camera.x;
    mv[13] -= w->camera.y;
    
    glUniformMatrix4fv(w->u_modelview, 1, GL_FALSE, mv);
    glUniformMatrix4fv(w->u_projection, 1, GL_FALSE, pr);
    
    /* Draw tiles */
    if (w->tile_count > 0 && w->atlas_count > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, w->atlases[0].texture);
        glUniform1i(w->u_texture, 0);
        glBindVertexArray(w->vao);
        glDrawArrays(GL_TRIANGLES, 0, w->tile_count * 6);
    }
    
    /* Draw sprites */
    if (w->sprite_count > 0) {
        float sv[24];
        glBindVertexArray(w->sprite_vao);
        for (int i = 0; i < w->sprite_count; i++) {
            Sprite *sp = &w->sprites[i];
            if (sp->atlas_id >= 0 && sp->atlas_id < w->atlas_count)
                glBindTexture(GL_TEXTURE_2D, w->atlases[sp->atlas_id].texture);
            world_build_sprite_verts(w, sp, sv);
            glBindBuffer(GL_ARRAY_BUFFER, w->sprite_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(sv), sv);
            if (sp->visible) glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }
    
    glBindVertexArray(0);
    glUseProgram(0);
}
