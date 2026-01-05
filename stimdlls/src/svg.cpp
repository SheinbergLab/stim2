/*
 * svg.c
 *  SVG display module using LunaSVG for parsing and rasterization
 *  Supports full SVG 1.1/1.2 Tiny: gradients, text, transforms, masks, etc.
 *
 *  Features:
 *   - Multi-resolution caching for icons (no re-rasterization on scale)
 *   - Named object support via resolveObjId
 *   - Dynamic stylesheet application
 *   - Color tinting and opacity control
 */

#ifdef __linux__
#define _GNU_SOURCE
#endif

#ifdef WIN32
#include <windows.h>
#endif

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <lunasvg.h>

#include <tcl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stim2.h>
#include <prmutil.h>
#include "objname.h"

/* Cache multiple resolutions for efficient scaling */
#define SVG_CACHE_SIZES 4
static const int CacheSizes[SVG_CACHE_SIZES] = { 64, 128, 256, 512 };

typedef struct _cached_raster {
    int width;
    int height;
    GLuint texture;
    int valid;
} CACHED_RASTER;

typedef struct _svg_obj {
    /* Original SVG dimensions */
    int svg_width;
    int svg_height;
    float aspect_ratio;
    
    /* Display state */
    int visible;
    
    /* OpenGL resources */
    GLuint vertex_buffer;
    GLuint vao;
    
    /* LunaSVG document - kept for re-rasterization and stylesheet changes */
    lunasvg::Document* document;
    
    /* Multi-resolution cache */
    CACHED_RASTER cache[SVG_CACHE_SIZES];
    int current_cache_idx;  /* Which cache level is currently bound */
    
    /* Explicit size override (-1 = auto) */
    int requested_width;
    int requested_height;
    
    /* Rendering parameters */
    float opacity;
    float color[4];         /* Tint color (RGBA) */
    int color_override;     /* 0=preserve, 1=replace, 2=multiply */
    
    /* Background */
    int background_enabled;
    float background_color[4];
    
} SVG_OBJ;

static int SvgID = -1;
static GLuint SvgShaderProgram = 0;
static GLint SvgUniformTexture = -1;
static GLint SvgUniformModelview = -1;
static GLint SvgUniformProjection = -1;
static GLint SvgUniformOpacity = -1;
static GLint SvgUniformColorTint = -1;
static GLint SvgUniformColorOverride = -1;

#ifdef STIM2_USE_GLES
static const char* svg_vertex_shader_source = 
"#version 300 es\n"
"precision mediump float;\n"
"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec2 aTexCoord;\n"
"out vec2 TexCoord;\n"
"uniform mat4 projMat;\n"
"uniform mat4 modelviewMat;\n"
"void main() {\n"
"    gl_Position = projMat * modelviewMat * vec4(aPos, 1.0);\n"
"    TexCoord = aTexCoord;\n"
"}\n";

static const char* svg_fragment_shader_source = 
"#version 300 es\n"
"precision mediump float;\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D ourTexture;\n"
"uniform float opacity;\n"
"uniform vec4 colorTint;\n"
"uniform int colorOverride;\n"
"\n"
"void main() {\n"
"    vec4 color = texture(ourTexture, TexCoord);\n"
"    if (colorOverride == 1) {\n"
"        color.rgb = colorTint.rgb;\n"
"        color.a *= colorTint.a;\n"
"    } else if (colorOverride == 2) {\n"
"        color *= colorTint;\n"
"    }\n"
"    color.a *= opacity;\n"
"    FragColor = color;\n"
"}\n";

#else
static const char* svg_vertex_shader_source = 
"#version 330 core\n"
"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec2 aTexCoord;\n"
"out vec2 TexCoord;\n"
"uniform mat4 projMat;\n"
"uniform mat4 modelviewMat;\n"
"void main() {\n"
"    gl_Position = projMat * modelviewMat * vec4(aPos, 1.0);\n"
"    TexCoord = aTexCoord;\n"
"}\n";

static const char* svg_fragment_shader_source = 
"#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D ourTexture;\n"
"uniform float opacity;\n"
"uniform vec4 colorTint;\n"
"uniform int colorOverride;\n"
"\n"
"void main() {\n"
"    vec4 color = texture(ourTexture, TexCoord);\n"
"    if (colorOverride == 1) {\n"
"        color.rgb = colorTint.rgb;\n"
"        color.a *= colorTint.a;\n"
"    } else if (colorOverride == 2) {\n"
"        color *= colorTint;\n"
"    }\n"
"    color.a *= opacity;\n"
"    FragColor = color;\n"
"}\n";
#endif

/* Generate aspect-ratio corrected quad vertices */
static void generate_svg_vertices(float *vertices, float aspect_ratio) {
    float half_width, half_height;
    
    if (aspect_ratio >= 1.0f) {
        half_width = 0.5f;
        half_height = 0.5f / aspect_ratio;
    } else {
        half_width = 0.5f * aspect_ratio;
        half_height = 0.5f;
    }
    
    float temp_vertices[] = {
        -half_width,  half_height, 0.0f,  0.0f, 0.0f,
        -half_width, -half_height, 0.0f,  0.0f, 1.0f,
         half_width, -half_height, 0.0f,  1.0f, 1.0f,
        -half_width,  half_height, 0.0f,  0.0f, 0.0f,
         half_width, -half_height, 0.0f,  1.0f, 1.0f,
         half_width,  half_height, 0.0f,  1.0f, 0.0f
    };
    
    memcpy(vertices, temp_vertices, sizeof(temp_vertices));
}

static GLuint compile_svg_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "SVG shader compilation error: %s\n", infoLog);
        return 0;
    }
    return shader;
}

static int create_svg_shader_program() {
    GLuint vertex_shader = compile_svg_shader(GL_VERTEX_SHADER, svg_vertex_shader_source);
    GLuint fragment_shader = compile_svg_shader(GL_FRAGMENT_SHADER, svg_fragment_shader_source);
    
    if (!vertex_shader || !fragment_shader) return -1;
    
    SvgShaderProgram = glCreateProgram();
    glAttachShader(SvgShaderProgram, vertex_shader);
    glAttachShader(SvgShaderProgram, fragment_shader);
    glLinkProgram(SvgShaderProgram);
    
    GLint success;
    glGetProgramiv(SvgShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(SvgShaderProgram, 512, NULL, infoLog);
        fprintf(stderr, "SVG shader program linking error: %s\n", infoLog);
        return -1;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    SvgUniformTexture = glGetUniformLocation(SvgShaderProgram, "ourTexture");
    SvgUniformModelview = glGetUniformLocation(SvgShaderProgram, "modelviewMat");
    SvgUniformProjection = glGetUniformLocation(SvgShaderProgram, "projMat");
    SvgUniformOpacity = glGetUniformLocation(SvgShaderProgram, "opacity");
    SvgUniformColorTint = glGetUniformLocation(SvgShaderProgram, "colorTint");
    SvgUniformColorOverride = glGetUniformLocation(SvgShaderProgram, "colorOverride");
    
    return 0;
}

static int init_svg_gl_resources(SVG_OBJ *svg) {
    glGenVertexArrays(1, &svg->vao);
    glBindVertexArray(svg->vao);
    
    glGenBuffers(1, &svg->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, svg->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, 6 * 5 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    
    glBindVertexArray(0);
    
    return 0;
}

/* Rasterize SVG at specific size and upload to texture */
static int rasterize_to_cache(SVG_OBJ *svg, int cache_idx) {
    if (!svg->document || cache_idx < 0 || cache_idx >= SVG_CACHE_SIZES)
        return -1;
    
    int target_size = CacheSizes[cache_idx];
    int width, height;
    
    /* Maintain aspect ratio */
    if (svg->aspect_ratio >= 1.0f) {
        width = target_size;
        height = (int)(target_size / svg->aspect_ratio);
    } else {
        height = target_size;
        width = (int)(target_size * svg->aspect_ratio);
    }
    
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    
    /* Render using LunaSVG */
    lunasvg::Bitmap bitmap = svg->document->renderToBitmap(width, height);
    if (bitmap.isNull()) {
        fprintf(getConsoleFP(), "SVG: Failed to render to bitmap at %dx%d\n", width, height);
        return -1;
    }
    
    /* Create/update texture */
    CACHED_RASTER *cache = &svg->cache[cache_idx];
    
    if (!cache->texture) {
        glGenTextures(1, &cache->texture);
    }
    
    glBindTexture(GL_TEXTURE_2D, cache->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    /* LunaSVG outputs BGRA, need to swizzle or convert */
    /* Actually it outputs ARGB premultiplied - let's convert to RGBA */
    unsigned char* rgba = (unsigned char*)malloc(width * height * 4);
    const unsigned char* src = bitmap.data();
    
    for (int i = 0; i < width * height; i++) {
        /* LunaSVG uses ARGB32 premultiplied format */
        unsigned char b = src[i*4 + 0];
        unsigned char g = src[i*4 + 1];
        unsigned char r = src[i*4 + 2];
        unsigned char a = src[i*4 + 3];
        rgba[i*4 + 0] = r;
        rgba[i*4 + 1] = g;
        rgba[i*4 + 2] = b;
        rgba[i*4 + 3] = a;
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    
    free(rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    cache->width = width;
    cache->height = height;
    cache->valid = 1;
    
    return 0;
}

/* Pre-rasterize all cache levels */
static int rasterize_all_cache_levels(SVG_OBJ *svg) {
    for (int i = 0; i < SVG_CACHE_SIZES; i++) {
        if (rasterize_to_cache(svg, i) < 0) {
            fprintf(getConsoleFP(), "SVG: Warning - failed to cache at size %d\n", CacheSizes[i]);
        }
    }
    svg->current_cache_idx = SVG_CACHE_SIZES / 2;  /* Start with medium resolution */
    return 0;
}

/* Select best cache level based on current transform scale */
static int select_best_cache(SVG_OBJ *svg) {
    /* For now, just use the highest resolution that's valid */
    /* TODO: Could query current modelview scale to pick optimal */
    for (int i = SVG_CACHE_SIZES - 1; i >= 0; i--) {
        if (svg->cache[i].valid) {
            return i;
        }
    }
    return 0;
}

/* Load SVG from file */
static int load_svg_from_file(SVG_OBJ *svg, const char *filename) {
    svg->document = lunasvg::Document::loadFromFile(filename).release();
    if (!svg->document) {
        fprintf(getConsoleFP(), "SVG: Failed to load file: %s\n", filename);
        return -1;
    }
    
    svg->svg_width = (int)svg->document->width();
    svg->svg_height = (int)svg->document->height();
    svg->aspect_ratio = (float)svg->svg_width / (float)svg->svg_height;
    
    /* Update vertex buffer */
    float vertices[30];
    generate_svg_vertices(vertices, svg->aspect_ratio);
    glBindBuffer(GL_ARRAY_BUFFER, svg->vertex_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    /* Pre-rasterize all cache levels */
    return rasterize_all_cache_levels(svg);
}

/* Load SVG from string */
static int load_svg_from_string(SVG_OBJ *svg, const char *svg_data) {
    svg->document = lunasvg::Document::loadFromData(svg_data).release();
    if (!svg->document) {
        fprintf(getConsoleFP(), "SVG: Failed to parse SVG data\n");
        return -1;
    }
    
    svg->svg_width = (int)svg->document->width();
    svg->svg_height = (int)svg->document->height();
    svg->aspect_ratio = (float)svg->svg_width / (float)svg->svg_height;
    
    float vertices[30];
    generate_svg_vertices(vertices, svg->aspect_ratio);
    glBindBuffer(GL_ARRAY_BUFFER, svg->vertex_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    return rasterize_all_cache_levels(svg);
}

/* Drawing function */
void svgShow(GR_OBJ *gobj) {
    SVG_OBJ *svg = (SVG_OBJ *) GR_CLIENTDATA(gobj);
    
    if (!svg->visible) return;
    
    int cache_idx = select_best_cache(svg);
    if (!svg->cache[cache_idx].valid) return;
    
    float modelview[16], projection[16];
    stimGetMatrix(STIM_MODELVIEW_MATRIX, modelview);
    stimGetMatrix(STIM_PROJECTION_MATRIX, projection);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glUseProgram(SvgShaderProgram);
    glUniformMatrix4fv(SvgUniformModelview, 1, GL_FALSE, modelview);
    glUniformMatrix4fv(SvgUniformProjection, 1, GL_FALSE, projection);
    glUniform1f(SvgUniformOpacity, svg->opacity);
    glUniform4f(SvgUniformColorTint, svg->color[0], svg->color[1],
                svg->color[2], svg->color[3]);
    glUniform1i(SvgUniformColorOverride, svg->color_override);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, svg->cache[cache_idx].texture);
    glUniform1i(SvgUniformTexture, 0);
    
    glBindVertexArray(svg->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

void svgDelete(GR_OBJ *gobj) {
    SVG_OBJ *svg = (SVG_OBJ *) GR_CLIENTDATA(gobj);
    
    /* Free LunaSVG document */
    if (svg->document) {
        delete svg->document;
    }
    
    /* Free cached textures */
    for (int i = 0; i < SVG_CACHE_SIZES; i++) {
        if (svg->cache[i].texture) {
            glDeleteTextures(1, &svg->cache[i].texture);
        }
    }
    
    /* Free OpenGL resources */
    if (svg->vertex_buffer) glDeleteBuffers(1, &svg->vertex_buffer);
    if (svg->vao) glDeleteVertexArrays(1, &svg->vao);
    
    free((void *) svg);
}

void svgReset(GR_OBJ *gobj) {
    /* Nothing to reset */
}

int svgCreate(OBJ_LIST *objlist, const char *source, int is_file) {
    const char *name = "SVG";
    GR_OBJ *obj;
    SVG_OBJ *svg;

    obj = gobjCreateObj();
    if (!obj) return -1;

    strcpy(GR_NAME(obj), name);
    GR_OBJTYPE(obj) = SvgID;

    GR_DELETEFUNCP(obj) = svgDelete;
    GR_RESETFUNCP(obj) = svgReset;
    GR_ACTIONFUNCP(obj) = svgShow;

    svg = (SVG_OBJ *) calloc(1, sizeof(SVG_OBJ));
    GR_CLIENTDATA(obj) = svg;

    /* Initialize state */
    svg->visible = 1;
    svg->opacity = 1.0f;
    svg->color[0] = 1.0f;
    svg->color[1] = 1.0f;
    svg->color[2] = 1.0f;
    svg->color[3] = 1.0f;
    svg->color_override = 0;
    svg->requested_width = -1;
    svg->requested_height = -1;
    
    if (init_svg_gl_resources(svg) < 0) {
        fprintf(getConsoleFP(), "SVG: error initializing OpenGL resources\n");
        svgDelete(obj);
        return -1;
    }

    int result;
    if (is_file) {
        result = load_svg_from_file(svg, source);
    } else {
        result = load_svg_from_string(svg, source);
    }
    
    if (result < 0) {
        fprintf(getConsoleFP(), "SVG: error loading SVG\n");
        svgDelete(obj);
        return -1;
    }
    
    return gobjAddObj(objlist, obj);
}

/****************************************************************/
/*                    Tcl Commands                              */
/****************************************************************/

static int svgCmd(ClientData clientData, Tcl_Interp *interp,
                  int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    int id;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " svgfile_or_data", NULL);
        return TCL_ERROR;
    }

    /* Detect if it's SVG data or filename */
    const char *input = argv[1];
    int is_svg_data = (strncmp(input, "<svg", 4) == 0 || strstr(input, "<svg") != NULL);
    
    if ((id = svgCreate(olist, input, !is_svg_data)) < 0) {
        Tcl_SetResult(interp, (char*)"error loading SVG", TCL_STATIC);
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

static int svginfoCmd(ClientData clientData, Tcl_Interp *interp,
                      int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, ((ObjNameInfo*)OL_NAMEINFO(olist)), argv[1], SvgID, "svg")) < 0)
        return TCL_ERROR;

    svg = (SVG_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));

    Tcl_Obj *dictObj = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("width", -1), 
                   Tcl_NewIntObj(svg->svg_width));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("height", -1), 
                   Tcl_NewIntObj(svg->svg_height));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("aspect_ratio", -1), 
                   Tcl_NewDoubleObj(svg->aspect_ratio));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("visible", -1), 
                   Tcl_NewIntObj(svg->visible));

    Tcl_SetObjResult(interp, dictObj);
    return TCL_OK;
}

static int svgvisibleCmd(ClientData clientData, Tcl_Interp *interp,
                         int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id, visible;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [visible]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, ((ObjNameInfo*)OL_NAMEINFO(olist)), argv[1], SvgID, "svg")) < 0)
        return TCL_ERROR;

    svg = (SVG_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(svg->visible));
        return TCL_OK;
    }
    
    if (Tcl_GetInt(interp, argv[2], &visible) != TCL_OK) return TCL_ERROR;
    svg->visible = visible ? 1 : 0;
    
    return TCL_OK;
}

static int svgopacityCmd(ClientData clientData, Tcl_Interp *interp,
                         int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id;
    double opacity;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [opacity]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, ((ObjNameInfo*)OL_NAMEINFO(olist)), argv[1], SvgID, "svg")) < 0)
        return TCL_ERROR;

    svg = (SVG_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(svg->opacity));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &opacity) != TCL_OK) return TCL_ERROR;
    svg->opacity = (float)fmax(0.0, fmin(1.0, opacity));
    
    return TCL_OK;
}

static int svgcolorCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id, override_mode;
    double r, g, b, a;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [mode r g b a]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, ((ObjNameInfo*)OL_NAMEINFO(olist)), argv[1], SvgID, "svg")) < 0)
        return TCL_ERROR;

    svg = (SVG_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(svg->color_override));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->color[0]));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->color[1]));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->color[2]));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->color[3]));
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }
    
    if (argc < 7) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id mode r g b a", NULL);
        return TCL_ERROR;
    }
    
    if (Tcl_GetInt(interp, argv[2], &override_mode) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &r) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &g) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &b) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[6], &a) != TCL_OK) return TCL_ERROR;
    
    svg->color_override = (int)fmax(0, fmin(2, override_mode));
    svg->color[0] = (float)fmax(0.0, fmin(1.0, r));
    svg->color[1] = (float)fmax(0.0, fmin(1.0, g));
    svg->color[2] = (float)fmax(0.0, fmin(1.0, b));
    svg->color[3] = (float)fmax(0.0, fmin(1.0, a));
    
    return TCL_OK;
}

/* Apply CSS stylesheet to SVG (LunaSVG feature!) */
static int svgstylesheetCmd(ClientData clientData, Tcl_Interp *interp,
                            int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id;

    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id css_stylesheet", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, ((ObjNameInfo*)OL_NAMEINFO(olist)), argv[1], SvgID, "svg")) < 0)
        return TCL_ERROR;

    svg = (SVG_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (!svg->document) {
        Tcl_AppendResult(interp, argv[0], ": no SVG document loaded", NULL);
        return TCL_ERROR;
    }
    
    /* Apply stylesheet */
    svg->document->applyStyleSheet(argv[2]);
    
    /* Re-rasterize all cache levels */
    rasterize_all_cache_levels(svg);
    
    return TCL_OK;
}

/* Reload SVG from file (useful during development) */
static int svgreloadCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id;

    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id filename", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, ((ObjNameInfo*)OL_NAMEINFO(olist)), argv[1], SvgID, "svg")) < 0)
        return TCL_ERROR;

    svg = (SVG_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    /* Delete old document */
    if (svg->document) {
        delete svg->document;
        svg->document = NULL;
    }
    
    /* Invalidate cache */
    for (int i = 0; i < SVG_CACHE_SIZES; i++) {
        svg->cache[i].valid = 0;
    }
    
    /* Reload */
    if (load_svg_from_file(svg, argv[2]) < 0) {
        Tcl_AppendResult(interp, argv[0], ": failed to reload SVG", NULL);
        return TCL_ERROR;
    }
    
    return TCL_OK;
}

/****************************************************************/
/*                       Module Init                            */
/****************************************************************/

#ifdef _WIN32
EXPORT(int, Svg_Init) (Tcl_Interp *interp)
#else
extern "C" int Svg_Init(Tcl_Interp *interp)
#endif
{
    OBJ_LIST *OBJList = getOBJList();

    if (
#ifdef USE_TCL_STUBS
        Tcl_InitStubs(interp, "8.5-", 0)
#else
        Tcl_PkgRequire(interp, "Tcl", "8.5-", 0)
#endif
        == NULL) {
        return TCL_ERROR;
    }

    if (SvgID < 0) {
        SvgID = gobjRegisterType();
        
        gladLoadGL();
        
        if (create_svg_shader_program() < 0) {
            Tcl_SetResult(interp, (char*)"error creating SVG shader program", TCL_STATIC);
            return TCL_ERROR;
        }
    }

    Tcl_CreateCommand(interp, "svg", (Tcl_CmdProc *) svgCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "svgInfo", (Tcl_CmdProc *) svginfoCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "svgVisible", (Tcl_CmdProc *) svgvisibleCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "svgOpacity", (Tcl_CmdProc *) svgopacityCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "svgColor", (Tcl_CmdProc *) svgcolorCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "svgStylesheet", (Tcl_CmdProc *) svgstylesheetCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "svgReload", (Tcl_CmdProc *) svgreloadCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

    const char *script = R"(
proc svgAsset {filename} {
    return [svg [assetFind $filename]]
}
)";
    Tcl_Eval(interp, script);

    return TCL_OK;
}

#ifdef WIN32
BOOL APIENTRY
DllEntryPoint(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
    return TRUE;
}
#endif
