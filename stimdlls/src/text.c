/*
 * text.c
 *  Modern text rendering module using fontstash + stb_truetype
 *  
 *  Features:
 *   - Multiple fonts (load any TTF/OTF)
 *   - Dynamic text updates
 *   - UTF-8 support
 *   - Configurable font paths
 *   - Multiple sizes from same font
 *   - Text measurement
 *   - Justification (left/center/right)
 *
 *  No FreeType dependency - uses header-only stb_truetype
 */

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <tcl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <prmutil.h>
#include <stim2.h>
#include "objname.h"

/* fontstash configuration */
#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

/* We'll implement our own GL backend for modern OpenGL */

/****************************************************************/
/*                    Font System State                         */
/****************************************************************/

#define MAX_FONTS 16
#define ATLAS_SIZE 1024

typedef struct {
    FONScontext* fs;
    GLuint texture;
    int width;
    int height;
    int fonts[MAX_FONTS];
    char* fontNames[MAX_FONTS];
    int numFonts;
    int defaultFont;
    char* fontPath;          /* Base path for fonts */
} FontSystem;

static FontSystem* gFontSystem = NULL;

/****************************************************************/
/*                    Text Object Structure                     */
/****************************************************************/

typedef struct {
    char* string;
    int fontId;
    float fontSize;
    float color[4];
    int justify;             /* 0=left, 1=center, 2=right */
    
    /* Cached geometry */
    GLfloat* verts;
    GLfloat* texcoords;
    int numQuads;
    
    /* Measured bounds */
    float width;
    float height;
    float ascender;
    float descender;
    
    /* OpenGL resources */
    GLuint vao;
    GLuint vbo_pos;
    GLuint vbo_tex;
    int dirty;               /* Needs geometry rebuild */
} TEXT_OBJ;

static int TextID = -1;

/* Shader */
static GLuint TextShaderProgram = 0;
static GLint TextUniformTexture = -1;
static GLint TextUniformModelview = -1;
static GLint TextUniformProjection = -1;
static GLint TextUniformColor = -1;

enum { TEXT_JUSTIFY_LEFT = 0, TEXT_JUSTIFY_CENTER = 1, TEXT_JUSTIFY_RIGHT = 2 };

/****************************************************************/
/*                    Shader Code                               */
/****************************************************************/

#ifdef STIM2_USE_GLES
static const char* text_vertex_shader =
"#version 300 es\n"
"precision mediump float;\n"
"layout(location = 0) in vec2 aPos;\n"
"layout(location = 1) in vec2 aTexCoord;\n"
"out vec2 vTexCoord;\n"
"uniform mat4 projMat;\n"
"uniform mat4 modelviewMat;\n"
"void main() {\n"
"    gl_Position = projMat * modelviewMat * vec4(aPos, 0.0, 1.0);\n"
"    vTexCoord = aTexCoord;\n"
"}\n";

static const char* text_fragment_shader =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vTexCoord;\n"
"out vec4 fragColor;\n"
"uniform sampler2D tex;\n"
"uniform vec4 uColor;\n"
"void main() {\n"
"    float alpha = texture(tex, vTexCoord).r;\n"
"    fragColor = vec4(uColor.rgb, uColor.a * alpha);\n"
"}\n";

#else
static const char* text_vertex_shader =
"#version 330 core\n"
"layout(location = 0) in vec2 aPos;\n"
"layout(location = 1) in vec2 aTexCoord;\n"
"out vec2 vTexCoord;\n"
"uniform mat4 projMat;\n"
"uniform mat4 modelviewMat;\n"
"void main() {\n"
"    gl_Position = projMat * modelviewMat * vec4(aPos, 0.0, 1.0);\n"
"    vTexCoord = aTexCoord;\n"
"}\n";

static const char* text_fragment_shader =
"#version 330 core\n"
"in vec2 vTexCoord;\n"
"out vec4 fragColor;\n"
"uniform sampler2D tex;\n"
"uniform vec4 uColor;\n"
"void main() {\n"
"    float alpha = texture(tex, vTexCoord).r;\n"
"    fragColor = vec4(uColor.rgb, uColor.a * alpha);\n"
"}\n";
#endif

/****************************************************************/
/*                    Fontstash Callbacks                       */
/****************************************************************/

static int fs_create(void* userPtr, int width, int height) {
    FontSystem* sys = (FontSystem*)userPtr;
    sys->width = width;
    sys->height = height;
    
    glGenTextures(1, &sys->texture);
    glBindTexture(GL_TEXTURE_2D, sys->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return 1;
}

static int fs_resize(void* userPtr, int width, int height) {
    return fs_create(userPtr, width, height);
}

static void fs_update(void* userPtr, int* rect, const unsigned char* data) {
    FontSystem* sys = (FontSystem*)userPtr;
    
    int x = rect[0];
    int y = rect[1];
    int w = rect[2] - rect[0];
    int h = rect[3] - rect[1];
    
    glBindTexture(GL_TEXTURE_2D, sys->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, sys->width);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, x);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, y);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RED, GL_UNSIGNED_BYTE, data);
    
    /* Reset pixel store state */
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void fs_draw(void* userPtr, const float* verts, const float* tcoords,
                    const unsigned int* colors, int nverts) {
    /* We don't use fontstash's immediate drawing - we build geometry ourselves */
    (void)userPtr;
    (void)verts;
    (void)tcoords;
    (void)colors;
    (void)nverts;
}

static void fs_delete(void* userPtr) {
    FontSystem* sys = (FontSystem*)userPtr;
    if (sys->texture) {
        glDeleteTextures(1, &sys->texture);
        sys->texture = 0;
    }
}

/****************************************************************/
/*                    Shader Setup                              */
/****************************************************************/

static GLuint compile_text_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        fprintf(stderr, "Text shader compile error: %s\n", log);
        return 0;
    }
    return shader;
}

static int create_text_shader(void) {
    GLuint vs = compile_text_shader(GL_VERTEX_SHADER, text_vertex_shader);
    GLuint fs = compile_text_shader(GL_FRAGMENT_SHADER, text_fragment_shader);
    
    if (!vs || !fs) return -1;
    
    TextShaderProgram = glCreateProgram();
    glAttachShader(TextShaderProgram, vs);
    glAttachShader(TextShaderProgram, fs);
    glLinkProgram(TextShaderProgram);
    
    GLint success;
    glGetProgramiv(TextShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(TextShaderProgram, 512, NULL, log);
        fprintf(stderr, "Text shader link error: %s\n", log);
        return -1;
    }
    
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    TextUniformTexture = glGetUniformLocation(TextShaderProgram, "tex");
    TextUniformModelview = glGetUniformLocation(TextShaderProgram, "modelviewMat");
    TextUniformProjection = glGetUniformLocation(TextShaderProgram, "projMat");
    TextUniformColor = glGetUniformLocation(TextShaderProgram, "uColor");
    
    return 0;
}

/****************************************************************/
/*                    Font System Init                          */
/****************************************************************/

static char* build_font_path(const char* filename) {
    if (!gFontSystem || !gFontSystem->fontPath) return strdup(filename);
    
    /* If absolute path, use as-is */
    if (filename[0] == '/' || filename[0] == '\\') return strdup(filename);
#ifdef WIN32
    if (filename[1] == ':') return strdup(filename);
#endif
    
    /* Build relative path */
    size_t len = strlen(gFontSystem->fontPath) + strlen(filename) + 2;
    char* path = (char*)malloc(len);
    snprintf(path, len, "%s/%s", gFontSystem->fontPath, filename);
    return path;
}

static int init_font_system(const char* fontPath) {
    if (gFontSystem) return 0;  /* Already initialized */
    
    gFontSystem = (FontSystem*)calloc(1, sizeof(FontSystem));
    if (!gFontSystem) return -1;
    
    if (fontPath) {
        gFontSystem->fontPath = strdup(fontPath);
    }
    
    /* Create fontstash context with our callbacks */
    FONSparams params;
    memset(&params, 0, sizeof(params));
    params.width = ATLAS_SIZE;
    params.height = ATLAS_SIZE;
    params.flags = FONS_ZERO_TOPLEFT;
    params.userPtr = gFontSystem;
    params.renderCreate = fs_create;
    params.renderResize = fs_resize;
    params.renderUpdate = fs_update;
    params.renderDraw = fs_draw;
    params.renderDelete = fs_delete;
    
    gFontSystem->fs = fonsCreateInternal(&params);
    if (!gFontSystem->fs) {
        free(gFontSystem->fontPath);
        free(gFontSystem);
        gFontSystem = NULL;
        return -1;
    }
    
    gFontSystem->defaultFont = FONS_INVALID;
    
    return 0;
}

static void shutdown_font_system(void) {
    if (!gFontSystem) return;
    
    if (gFontSystem->fs) {
        fonsDeleteInternal(gFontSystem->fs);
    }
    
    for (int i = 0; i < gFontSystem->numFonts; i++) {
        free(gFontSystem->fontNames[i]);
    }
    
    free(gFontSystem->fontPath);
    free(gFontSystem);
    gFontSystem = NULL;
}

/****************************************************************/
/*                    Font Loading                              */
/****************************************************************/

static int load_font(const char* name, const char* filename) {
    if (!gFontSystem || !gFontSystem->fs) return -1;
    
    /* Check if font with this name already loaded */
    for (int i = 0; i < gFontSystem->numFonts; i++) {
        if (strcmp(gFontSystem->fontNames[i], name) == 0) {
            /* Already loaded - return existing ID */
            return gFontSystem->fonts[i];
        }
    }
    
    if (gFontSystem->numFonts >= MAX_FONTS) return -1;
    
    char* path = build_font_path(filename);
    int fontId = fonsAddFont(gFontSystem->fs, name, path);
    free(path);
    
    if (fontId == FONS_INVALID) {
        fprintf(getConsoleFP(), "Text: Failed to load font: %s\n", filename);
        return -1;
    }
    
    gFontSystem->fonts[gFontSystem->numFonts] = fontId;
    gFontSystem->fontNames[gFontSystem->numFonts] = strdup(name);
    gFontSystem->numFonts++;
    
    /* First font becomes default */
    if (gFontSystem->defaultFont == FONS_INVALID) {
        gFontSystem->defaultFont = fontId;
    }
    
    return fontId;
}

static int get_font_by_name(const char* name) {
    if (!gFontSystem) return FONS_INVALID;
    
    for (int i = 0; i < gFontSystem->numFonts; i++) {
        if (strcmp(gFontSystem->fontNames[i], name) == 0) {
            return gFontSystem->fonts[i];
        }
    }
    return FONS_INVALID;
}

/****************************************************************/
/*                    Text Object                               */
/****************************************************************/

static void text_build_geometry(TEXT_OBJ* t) {
    if (!gFontSystem || !gFontSystem->fs || !t->string) return;
    
    FONScontext* fs = gFontSystem->fs;
    
    /* Set font state - use a reference size for rasterization */
    float rasterSize = 64.0f;  /* Rasterize at 64px for quality */
    fonsSetFont(fs, t->fontId);
    fonsSetSize(fs, rasterSize);
    fonsSetAlign(fs, FONS_ALIGN_LEFT | FONS_ALIGN_BASELINE);
    
    /* Get metrics at raster size */
    float ascender, descender, lineHeight;
    fonsVertMetrics(fs, &ascender, &descender, &lineHeight);
    
    /* Calculate scale: convert pixels to degrees
     * fontSize is now in degrees (e.g., 1.0 = 1 degree tall)
     * We use the em-height (ascender - descender) as the reference
     * Note: descender is negative, so this is ascender + |descender|
     */
    float emHeight = ascender - descender;
    float scale = t->fontSize / emHeight;  /* degrees per pixel */
    
    /* Measure text */
    float bounds[4];
    float advance = fonsTextBounds(fs, 0, 0, t->string, NULL, bounds);
    
    /* Store dimensions in degrees */
    t->width = (bounds[2] - bounds[0]) * scale;
    t->height = (bounds[3] - bounds[1]) * scale;
    t->ascender = ascender * scale;
    t->descender = descender * scale;
    
    /* Count quads needed */
    int len = strlen(t->string);
    int maxQuads = len;  /* One quad per character max */
    
    /* Allocate geometry */
    if (t->verts) free(t->verts);
    if (t->texcoords) free(t->texcoords);
    
    t->verts = (GLfloat*)malloc(maxQuads * 6 * 2 * sizeof(GLfloat));     /* 6 verts * 2 coords (x,y) */
    t->texcoords = (GLfloat*)malloc(maxQuads * 6 * 2 * sizeof(GLfloat)); /* 6 verts * 2 coords (s,t) */
    
    /* Calculate offset based on justification (in pixels, will scale later) 
     * For centering, we center the em-box, not just the glyph bounds.
     * Em-box center is at y = (ascender + descender) / 2 from baseline
     */
    float pixelWidth = bounds[2] - bounds[0];
    float emCenter = (ascender + descender) / 2.0f;  /* Center of em-box relative to baseline */
    
    float xoff = 0, yoff = 0;
    switch (t->justify) {
        case TEXT_JUSTIFY_CENTER:
            xoff = -pixelWidth / 2.0f;
            yoff = emCenter;  /* Shift baseline so em-box center is at origin */
            break;
        case TEXT_JUSTIFY_RIGHT:
            xoff = -pixelWidth;
            yoff = emCenter;
            break;
        case TEXT_JUSTIFY_LEFT:
        default:
            xoff = 0;
            yoff = emCenter;
            break;
    }
    
    /* First call fonsDrawText to ensure glyphs are rasterized to atlas */
    fonsDrawText(fs, 0, 0, t->string, NULL);
    
    /* Now build quads using fontstash iterator */
    FONStextIter iter;
    FONSquad quad;
    
    GLfloat* vptr = t->verts;
    GLfloat* tptr = t->texcoords;
    int numQuads = 0;
    
    /* Start at xoff, with baseline at y=0 (yoff adjusts for centering) */
    fonsTextIterInit(fs, &iter, xoff, 0, t->string, NULL);
    
    while (fonsTextIterNext(fs, &iter, &quad)) {
        /* Scale positions to degrees and flip Y axis (negate Y) 
         * Apply yoff for vertical centering */
        float x0 = quad.x0 * scale;
        float x1 = quad.x1 * scale;
        float y0 = -(quad.y0 + yoff) * scale;  /* Flip Y and apply centering offset */
        float y1 = -(quad.y1 + yoff) * scale;  /* Flip Y and apply centering offset */
        
        /* Triangle 1: v0, v1, v2 */
        *vptr++ = x0; *vptr++ = y0;
        *tptr++ = quad.s0; *tptr++ = quad.t0;
        
        *vptr++ = x1; *vptr++ = y0;
        *tptr++ = quad.s1; *tptr++ = quad.t0;
        
        *vptr++ = x1; *vptr++ = y1;
        *tptr++ = quad.s1; *tptr++ = quad.t1;
        
        /* Triangle 2: v0, v2, v3 */
        *vptr++ = x0; *vptr++ = y0;
        *tptr++ = quad.s0; *tptr++ = quad.t0;
        
        *vptr++ = x1; *vptr++ = y1;
        *tptr++ = quad.s1; *tptr++ = quad.t1;
        
        *vptr++ = x0; *vptr++ = y1;
        *tptr++ = quad.s0; *tptr++ = quad.t1;
        
        numQuads++;
    }
    
    t->numQuads = numQuads;
    
    /* Upload to GPU */
    glBindBuffer(GL_ARRAY_BUFFER, t->vbo_pos);
    glBufferData(GL_ARRAY_BUFFER, numQuads * 6 * 2 * sizeof(GLfloat), t->verts, GL_DYNAMIC_DRAW);
    
    glBindBuffer(GL_ARRAY_BUFFER, t->vbo_tex);
    glBufferData(GL_ARRAY_BUFFER, numQuads * 6 * 2 * sizeof(GLfloat), t->texcoords, GL_DYNAMIC_DRAW);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    t->dirty = 0;
}

static void textDraw(GR_OBJ* g) {
    TEXT_OBJ* t = (TEXT_OBJ*)GR_CLIENTDATA(g);
    
    if (!t->string || !t->numQuads) return;
    if (!gFontSystem || !gFontSystem->texture) return;
    
    if (t->dirty) {
        text_build_geometry(t);
    }
    
    float modelview[16], projection[16];
    stimGetMatrix(STIM_MODELVIEW_MATRIX, modelview);
    stimGetMatrix(STIM_PROJECTION_MATRIX, projection);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glUseProgram(TextShaderProgram);
    glUniformMatrix4fv(TextUniformModelview, 1, GL_FALSE, modelview);
    glUniformMatrix4fv(TextUniformProjection, 1, GL_FALSE, projection);
    glUniform4f(TextUniformColor, t->color[0], t->color[1], t->color[2], t->color[3]);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gFontSystem->texture);
    glUniform1i(TextUniformTexture, 0);
    
    glBindVertexArray(t->vao);
    glDrawArrays(GL_TRIANGLES, 0, t->numQuads * 6);
    
    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

static void textDelete(GR_OBJ* g) {
    TEXT_OBJ* t = (TEXT_OBJ*)GR_CLIENTDATA(g);
    
    if (t->string) free(t->string);
    if (t->verts) free(t->verts);
    if (t->texcoords) free(t->texcoords);
    
    if (t->vbo_pos) glDeleteBuffers(1, &t->vbo_pos);
    if (t->vbo_tex) glDeleteBuffers(1, &t->vbo_tex);
    if (t->vao) glDeleteVertexArrays(1, &t->vao);
    
    free(t);
}

static void textReset(GR_OBJ* g) {
    /* Nothing to reset */
}

static int textCreate(OBJ_LIST* objlist, const char* string, int fontId, float fontSize) {
    GR_OBJ* obj = gobjCreateObj();
    if (!obj) return -1;
    
    strcpy(GR_NAME(obj), "Text");
    GR_OBJTYPE(obj) = TextID;
    GR_ACTIONFUNCP(obj) = textDraw;
    GR_DELETEFUNCP(obj) = textDelete;
    GR_RESETFUNCP(obj) = textReset;
    
    TEXT_OBJ* t = (TEXT_OBJ*)calloc(1, sizeof(TEXT_OBJ));
    GR_CLIENTDATA(obj) = t;
    
    t->string = strdup(string);
    t->fontId = (fontId >= 0) ? fontId : gFontSystem->defaultFont;
    t->fontSize = fontSize;
    t->justify = TEXT_JUSTIFY_CENTER;
    
    /* Default white */
    t->color[0] = 1.0f;
    t->color[1] = 1.0f;
    t->color[2] = 1.0f;
    t->color[3] = 1.0f;
    
    /* Create VAO/VBOs */
    glGenVertexArrays(1, &t->vao);
    glBindVertexArray(t->vao);
    
    glGenBuffers(1, &t->vbo_pos);
    glBindBuffer(GL_ARRAY_BUFFER, t->vbo_pos);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);  /* vec2 position */
    glEnableVertexAttribArray(0);
    
    glGenBuffers(1, &t->vbo_tex);
    glBindBuffer(GL_ARRAY_BUFFER, t->vbo_tex);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);  /* vec2 texcoord */
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);
    
    /* Build initial geometry */
    t->dirty = 1;
    text_build_geometry(t);
    
    return gobjAddObj(objlist, obj);
}

/****************************************************************/
/*                    Tcl Commands                              */
/****************************************************************/

/* textFont name filename ?size? - Load a font */
static int textfontCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " name filename", NULL);
        return TCL_ERROR;
    }
    
    int fontId = load_font(argv[1], argv[2]);
    if (fontId < 0) {
        Tcl_AppendResult(interp, argv[0], ": failed to load font: ", argv[2], NULL);
        return TCL_ERROR;
    }
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(fontId));
    return TCL_OK;
}

/* textPath path - Set font search path */
static int textpathCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    if (argc < 2) {
        /* Return current path */
        if (gFontSystem && gFontSystem->fontPath) {
            Tcl_SetResult(interp, gFontSystem->fontPath, TCL_VOLATILE);
        }
        return TCL_OK;
    }
    
    if (gFontSystem) {
        free(gFontSystem->fontPath);
        gFontSystem->fontPath = strdup(argv[1]);
    }
    
    return TCL_OK;
}

/* text string ?-font fontname? ?-size pts? */
static int textCmd(ClientData clientData, Tcl_Interp *interp,
                   int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST*)clientData;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " string ?-font name? ?-size pts?", NULL);
        return TCL_ERROR;
    }
    
    const char* string = argv[1];
    int fontId = gFontSystem ? gFontSystem->defaultFont : FONS_INVALID;
    float fontSize = 24.0f;
    
    /* Parse options */
    for (int i = 2; i < argc - 1; i += 2) {
        if (strcmp(argv[i], "-font") == 0) {
            fontId = get_font_by_name(argv[i+1]);
            if (fontId == FONS_INVALID) {
                Tcl_AppendResult(interp, argv[0], ": unknown font: ", argv[i+1], NULL);
                return TCL_ERROR;
            }
        } else if (strcmp(argv[i], "-size") == 0) {
            double tempSize;
            if (Tcl_GetDouble(interp, argv[i+1], &tempSize) != TCL_OK) {
                return TCL_ERROR;
            }
            fontSize = (float)tempSize;
        }
    }
    
    if (fontId == FONS_INVALID) {
        Tcl_AppendResult(interp, argv[0], ": no font loaded. Use textFont first.", NULL);
        return TCL_ERROR;
    }
    
    int id = textCreate(olist, string, fontId, fontSize);
    if (id < 0) {
        Tcl_SetResult(interp, (char*)"error creating text", TCL_STATIC);
        return TCL_ERROR;
    }
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

/* textString id newstring - Update text content */
static int textstringCmd(ClientData clientData, Tcl_Interp *interp,
                         int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST*)clientData;
    int id;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id ?string?", NULL);
        return TCL_ERROR;
    }
    
    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], TextID, "text")) < 0)
        return TCL_ERROR;
    
    TEXT_OBJ* t = (TEXT_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetResult(interp, t->string, TCL_VOLATILE);
        return TCL_OK;
    }
    
    free(t->string);
    t->string = strdup(argv[2]);
    t->dirty = 1;
    
    return TCL_OK;
}

/* textColor id r g b ?a? */
static int textcolorCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST*)clientData;
    int id;
    double r, g, b, a = 1.0;
    
    if (argc < 5) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id r g b ?a?", NULL);
        return TCL_ERROR;
    }
    
    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], TextID, "text")) < 0)
        return TCL_ERROR;
    
    TEXT_OBJ* t = (TEXT_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (Tcl_GetDouble(interp, argv[2], &r) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &g) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &b) != TCL_OK) return TCL_ERROR;
    if (argc > 5) {
        if (Tcl_GetDouble(interp, argv[5], &a) != TCL_OK) return TCL_ERROR;
    }
    
    t->color[0] = (float)r;
    t->color[1] = (float)g;
    t->color[2] = (float)b;
    t->color[3] = (float)a;
    
    return TCL_OK;
}

/* textSize id ?pts? - Get or set font size */
static int textsizeCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST*)clientData;
    int id;
    double size;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id ?size?", NULL);
        return TCL_ERROR;
    }
    
    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], TextID, "text")) < 0)
        return TCL_ERROR;
    
    TEXT_OBJ* t = (TEXT_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(t->fontSize));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &size) != TCL_OK) return TCL_ERROR;
    
    t->fontSize = (float)size;
    t->dirty = 1;
    
    return TCL_OK;
}

/* textJustify id ?left|center|right? */
static int textjustifyCmd(ClientData clientData, Tcl_Interp *interp,
                          int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST*)clientData;
    int id;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id ?left|center|right?", NULL);
        return TCL_ERROR;
    }
    
    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], TextID, "text")) < 0)
        return TCL_ERROR;
    
    TEXT_OBJ* t = (TEXT_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        const char* names[] = {"left", "center", "right"};
        Tcl_SetResult(interp, (char*)names[t->justify], TCL_STATIC);
        return TCL_OK;
    }
    
    if (strcmp(argv[2], "left") == 0) {
        t->justify = TEXT_JUSTIFY_LEFT;
    } else if (strcmp(argv[2], "center") == 0) {
        t->justify = TEXT_JUSTIFY_CENTER;
    } else if (strcmp(argv[2], "right") == 0) {
        t->justify = TEXT_JUSTIFY_RIGHT;
    } else {
        Tcl_AppendResult(interp, argv[0], ": invalid justification: ", argv[2], NULL);
        return TCL_ERROR;
    }
    
    t->dirty = 1;
    return TCL_OK;
}

/* textInfo id - Get text metrics */
static int textinfoCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST*)clientData;
    int id;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id", NULL);
        return TCL_ERROR;
    }
    
    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], TextID, "text")) < 0)
        return TCL_ERROR;
    
    TEXT_OBJ* t = (TEXT_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (t->dirty) {
        text_build_geometry(t);
    }
    
    Tcl_Obj* dict = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("width", -1), Tcl_NewDoubleObj(t->width));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("height", -1), Tcl_NewDoubleObj(t->height));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("ascender", -1), Tcl_NewDoubleObj(t->ascender));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("descender", -1), Tcl_NewDoubleObj(t->descender));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("fontSize", -1), Tcl_NewDoubleObj(t->fontSize));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("numChars", -1), Tcl_NewIntObj(strlen(t->string)));
    
    Tcl_SetObjResult(interp, dict);
    return TCL_OK;
}

/* textFonts - List loaded fonts */
static int textfontsCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[]) {
    Tcl_Obj* list = Tcl_NewListObj(0, NULL);
    
    if (gFontSystem) {
        for (int i = 0; i < gFontSystem->numFonts; i++) {
            Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj(gFontSystem->fontNames[i], -1));
        }
    }
    
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

/****************************************************************/
/*                       Module Init                            */
/****************************************************************/

#ifdef _WIN32
EXPORT(int, Text_Init) (Tcl_Interp *interp)
#else
int Text_Init(Tcl_Interp *interp)
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
    
    if (TextID < 0) {
        TextID = gobjRegisterType();
        
        gladLoadGL();
        
        if (create_text_shader() < 0) {
            Tcl_SetResult(interp, (char*)"error creating text shader", TCL_STATIC);
            return TCL_ERROR;
        }
    }
    
    /* Initialize font system with default path */
    const char* fontPath = getenv("STIM2_FONT_PATH");
    static char fontPathBuf[1024];
    
    if (!fontPath) {
#ifdef __APPLE__
        /* Try to get path from application bundle */
        CFBundleRef mainBundle = CFBundleGetMainBundle();
        if (mainBundle) {
            CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
            if (resourcesURL) {
                if (CFURLGetFileSystemRepresentation(resourcesURL, TRUE, 
                        (UInt8*)fontPathBuf, sizeof(fontPathBuf))) {
                    strncat(fontPathBuf, "/fonts", sizeof(fontPathBuf) - strlen(fontPathBuf) - 1);
                    fontPath = fontPathBuf;
                }
                CFRelease(resourcesURL);
            }
        }
        /* Fallback for development */
        if (!fontPath) {
            struct stat st;
            if (stat("/usr/local/stim2/fonts", &st) == 0) {
                fontPath = "/usr/local/stim2/fonts";
            } else {
                fontPath = "./fonts";
            }
        }
#elif defined(WIN32)
        fontPath = "C:/stim2/fonts";
#else
        /* Linux: check install location, fallback to local */
        struct stat st;
        if (stat("/usr/local/stim2/fonts", &st) == 0) {
            fontPath = "/usr/local/stim2/fonts";
        } else {
            fontPath = "./fonts";
        }
#endif
    }
    
    if (init_font_system(fontPath) < 0) {
        Tcl_SetResult(interp, (char*)"error initializing font system", TCL_STATIC);
        return TCL_ERROR;
    }
    
    /* Try to load a default font */
    load_font("default", "NotoSans-Regular.ttf");
    
    /* Register commands */
    Tcl_CreateCommand(interp, "text", (Tcl_CmdProc*)textCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textFont", (Tcl_CmdProc*)textfontCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textPath", (Tcl_CmdProc*)textpathCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textString", (Tcl_CmdProc*)textstringCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textColor", (Tcl_CmdProc*)textcolorCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textSize", (Tcl_CmdProc*)textsizeCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textJustify", (Tcl_CmdProc*)textjustifyCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textInfo", (Tcl_CmdProc*)textinfoCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textFonts", (Tcl_CmdProc*)textfontsCmd,
                      (ClientData)OBJList, NULL);
    
    return TCL_OK;
}

#ifdef WIN32
BOOL APIENTRY DllEntryPoint(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    return TRUE;
}
#endif
