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
 *   - Multiline text with \n
 *   - Word wrapping to specified width
 *   - Line spacing control
 *   - Vertical alignment (top/center/bottom)
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

enum { 
    TEXT_JUSTIFY_LEFT = 0, 
    TEXT_JUSTIFY_CENTER = 1, 
    TEXT_JUSTIFY_RIGHT = 2 
};

enum {
    TEXT_VALIGN_TOP = 0,
    TEXT_VALIGN_CENTER = 1,
    TEXT_VALIGN_BOTTOM = 2
};

typedef struct {
    char* string;
    int fontId;
    float fontSize;
    float color[4];
    int justify;             /* 0=left, 1=center, 2=right */
    int valign;              /* 0=top, 1=center, 2=bottom */
    
    /* Multiline support */
    float wrapWidth;         /* 0 = no wrap, >0 = wrap to this width in degrees */
    float lineSpacing;       /* Line height multiplier (default 1.3) */
    
    /* Cached geometry */
    GLfloat* verts;
    GLfloat* texcoords;
    int numQuads;
    
    /* Measured bounds (total for all lines) */
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
/*                    Line Processing                           */
/****************************************************************/

/* Structure for a processed line */
typedef struct {
    char* text;
    float width;      /* Width in degrees */
} TextLine;

/* Measure text width in degrees at given font/size */
static float measure_text_width(const char* str, int fontId, float fontSize) {
    if (!gFontSystem || !gFontSystem->fs || !str || !*str) return 0.0f;
    
    FONScontext* fs = gFontSystem->fs;
    float rasterSize = 64.0f;
    
    fonsSetFont(fs, fontId);
    fonsSetSize(fs, rasterSize);
    
    float ascender, descender, lineHeight;
    fonsVertMetrics(fs, &ascender, &descender, &lineHeight);
    float emHeight = ascender - descender;
    float scale = fontSize / emHeight;
    
    float bounds[4];
    fonsTextBounds(fs, 0, 0, str, NULL, bounds);
    
    return (bounds[2] - bounds[0]) * scale;
}

/* Word wrap a single line to fit within maxWidth (in degrees)
 * Returns array of TextLine structs, terminated by NULL text
 * Caller must free the array and each text string
 */
static TextLine* wrap_line(const char* line, int fontId, float fontSize, float maxWidth) {
    /* Count words to estimate max lines needed */
    int wordCount = 1;
    for (const char* p = line; *p; p++) {
        if (*p == ' ') wordCount++;
    }
    
    /* Allocate for worst case (each word on its own line) + terminator */
    TextLine* lines = (TextLine*)calloc(wordCount + 1, sizeof(TextLine));
    int lineCount = 0;
    
    if (maxWidth <= 0 || !*line) {
        /* No wrapping - just copy the line */
        lines[0].text = strdup(line);
        lines[0].width = measure_text_width(line, fontId, fontSize);
        return lines;
    }
    
    /* Build lines word by word */
    char* lineBuf = (char*)malloc(strlen(line) + 1);
    lineBuf[0] = '\0';
    float lineWidth = 0;
    
    const char* wordStart = line;
    while (*wordStart) {
        /* Skip leading spaces */
        while (*wordStart == ' ') wordStart++;
        if (!*wordStart) break;
        
        /* Find word end */
        const char* wordEnd = wordStart;
        while (*wordEnd && *wordEnd != ' ') wordEnd++;
        
        /* Extract word */
        int wordLen = wordEnd - wordStart;
        char* word = (char*)malloc(wordLen + 1);
        strncpy(word, wordStart, wordLen);
        word[wordLen] = '\0';
        
        /* Measure word (with leading space if not first word on line) */
        char* testStr;
        if (lineBuf[0]) {
            testStr = (char*)malloc(strlen(lineBuf) + wordLen + 2);
            sprintf(testStr, "%s %s", lineBuf, word);
        } else {
            testStr = strdup(word);
        }
        
        float testWidth = measure_text_width(testStr, fontId, fontSize);
        
        if (testWidth > maxWidth && lineBuf[0]) {
            /* Word doesn't fit - finish current line, start new one */
            lines[lineCount].text = strdup(lineBuf);
            lines[lineCount].width = lineWidth;
            lineCount++;
            
            strcpy(lineBuf, word);
            lineWidth = measure_text_width(word, fontId, fontSize);
        } else {
            /* Word fits - add to current line */
            strcpy(lineBuf, testStr);
            lineWidth = testWidth;
        }
        
        free(word);
        free(testStr);
        wordStart = wordEnd;
    }
    
    /* Don't forget the last line */
    if (lineBuf[0]) {
        lines[lineCount].text = strdup(lineBuf);
        lines[lineCount].width = lineWidth;
        lineCount++;
    }
    
    free(lineBuf);
    return lines;
}

/* Split string on newlines and apply word wrapping
 * Returns array of TextLine structs, terminated by NULL text
 */
static TextLine* process_lines(const char* str, int fontId, float fontSize, float wrapWidth) {
    /* Count newlines to estimate line count */
    int newlineCount = 1;
    for (const char* p = str; *p; p++) {
        if (*p == '\n') newlineCount++;
    }
    
    /* Allocate generous initial array */
    int maxLines = newlineCount * 10;  /* Allow for word wrapping */
    TextLine* allLines = (TextLine*)calloc(maxLines + 1, sizeof(TextLine));
    int totalLines = 0;
    
    /* Make a working copy we can modify */
    char* work = strdup(str);
    char* line = work;
    char* next;
    
    while (line) {
        /* Find next newline */
        next = strchr(line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }
        
        /* Wrap this line */
        TextLine* wrapped = wrap_line(line, fontId, fontSize, wrapWidth);
        
        /* Copy wrapped lines to output */
        for (int i = 0; wrapped[i].text; i++) {
            if (totalLines < maxLines) {
                allLines[totalLines] = wrapped[i];
                totalLines++;
            } else {
                free(wrapped[i].text);
            }
        }
        free(wrapped);
        
        line = next;
    }
    
    free(work);
    return allLines;
}

/* Free lines array */
static void free_lines(TextLine* lines) {
    if (!lines) return;
    for (int i = 0; lines[i].text; i++) {
        free(lines[i].text);
    }
    free(lines);
}

/****************************************************************/
/*                    Geometry Building                         */
/****************************************************************/

static void text_build_geometry(TEXT_OBJ* t) {
    if (!gFontSystem || !gFontSystem->fs || !t->string) return;
    
    FONScontext* fs = gFontSystem->fs;
    
    /* Set font state - use a reference size for rasterization */
    float rasterSize = 64.0f;
    fonsSetFont(fs, t->fontId);
    fonsSetSize(fs, rasterSize);
    fonsSetAlign(fs, FONS_ALIGN_LEFT | FONS_ALIGN_BASELINE);
    
    /* Get metrics at raster size */
    float ascender, descender, lineHeight;
    fonsVertMetrics(fs, &ascender, &descender, &lineHeight);
    
    /* Calculate scale */
    float emHeight = ascender - descender;
    float scale = t->fontSize / emHeight;
    
    /* Process lines (split on \n and word wrap) */
    TextLine* lines = process_lines(t->string, t->fontId, t->fontSize, t->wrapWidth);
    
    /* Count lines and find max width */
    int numLines = 0;
    float maxWidth = 0;
    for (int i = 0; lines[i].text; i++) {
        numLines++;
        if (lines[i].width > maxWidth) {
            maxWidth = lines[i].width;
        }
    }
    
    /* Calculate total dimensions */
    float lineHeightDeg = t->fontSize * t->lineSpacing;
    float totalHeight = lineHeightDeg * numLines;
    
    t->width = maxWidth;
    t->height = totalHeight;
    t->ascender = ascender * scale;
    t->descender = descender * scale;
    
    /* Count total characters for geometry allocation */
    int totalChars = 0;
    for (int i = 0; lines[i].text; i++) {
        totalChars += strlen(lines[i].text);
    }
    
    /* Allocate geometry */
    if (t->verts) free(t->verts);
    if (t->texcoords) free(t->texcoords);
    
    int maxQuads = totalChars;
    t->verts = (GLfloat*)malloc(maxQuads * 6 * 2 * sizeof(GLfloat));
    t->texcoords = (GLfloat*)malloc(maxQuads * 6 * 2 * sizeof(GLfloat));
    
    GLfloat* vptr = t->verts;
    GLfloat* tptr = t->texcoords;
    int numQuads = 0;
    
    /* Calculate starting Y based on vertical alignment */
    float startY;
    float emCenter = (ascender + descender) / 2.0f * scale;
    
    switch (t->valign) {
        case TEXT_VALIGN_TOP:
            /* First line baseline at top, accounting for ascender */
            startY = -t->ascender + emCenter;
            break;
        case TEXT_VALIGN_CENTER:
            /* Center the whole block vertically */
            startY = (totalHeight / 2.0f) - lineHeightDeg / 2.0f + emCenter;
            break;
        case TEXT_VALIGN_BOTTOM:
            /* Last line at bottom */
            startY = totalHeight - lineHeightDeg + emCenter;
            break;
        default:
            startY = (totalHeight / 2.0f) - lineHeightDeg / 2.0f + emCenter;
    }
    
    /* Render each line */
    float currentY = startY;
    
    for (int lineIdx = 0; lines[lineIdx].text; lineIdx++) {
        const char* lineText = lines[lineIdx].text;
        float lineWidth = lines[lineIdx].width;
        
        if (!lineText[0]) {
            /* Empty line - just advance Y */
            currentY -= lineHeightDeg;
            continue;
        }
        
        /* Calculate X offset based on justification */
        float xoff = 0;
        switch (t->justify) {
            case TEXT_JUSTIFY_CENTER:
                xoff = -lineWidth / (2.0f * scale);  /* Convert back to pixels for fontstash */
                break;
            case TEXT_JUSTIFY_RIGHT:
                xoff = -lineWidth / scale;
                break;
            case TEXT_JUSTIFY_LEFT:
            default:
                xoff = 0;
                break;
        }
        
        /* Ensure glyphs are rasterized */
        fonsDrawText(fs, 0, 0, lineText, NULL);
        
        /* Build quads for this line */
        FONStextIter iter;
        FONSquad quad;
        
        fonsTextIterInit(fs, &iter, xoff, 0, lineText, NULL);
        
        while (fonsTextIterNext(fs, &iter, &quad)) {
            /* Scale positions to degrees and apply Y offset */
            float x0 = quad.x0 * scale;
            float x1 = quad.x1 * scale;
            float y0 = currentY - quad.y0 * scale;
            float y1 = currentY - quad.y1 * scale;
            
            /* Triangle 1 */
            *vptr++ = x0; *vptr++ = y0;
            *tptr++ = quad.s0; *tptr++ = quad.t0;
            
            *vptr++ = x1; *vptr++ = y0;
            *tptr++ = quad.s1; *tptr++ = quad.t0;
            
            *vptr++ = x1; *vptr++ = y1;
            *tptr++ = quad.s1; *tptr++ = quad.t1;
            
            /* Triangle 2 */
            *vptr++ = x0; *vptr++ = y0;
            *tptr++ = quad.s0; *tptr++ = quad.t0;
            
            *vptr++ = x1; *vptr++ = y1;
            *tptr++ = quad.s1; *tptr++ = quad.t1;
            
            *vptr++ = x0; *vptr++ = y1;
            *tptr++ = quad.s0; *tptr++ = quad.t1;
            
            numQuads++;
        }
        
        currentY -= lineHeightDeg;
    }
    
    free_lines(lines);
    
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
    t->valign = TEXT_VALIGN_CENTER;
    t->wrapWidth = 0;          /* No wrapping by default */
    t->lineSpacing = 1.3f;     /* Default line spacing */
    
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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    
    glGenBuffers(1, &t->vbo_tex);
    glBindBuffer(GL_ARRAY_BUFFER, t->vbo_tex);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);
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

/* textFont name filename - Load a font */
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

/* text string ?-font fontname? ?-size pts? ?-wrap width? ?-spacing mult? */
static int textCmd(ClientData clientData, Tcl_Interp *interp,
                   int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST*)clientData;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], 
            " string ?-font name? ?-size pts? ?-wrap width? ?-spacing mult?", NULL);
        return TCL_ERROR;
    }
    
    const char* string = argv[1];
    int fontId = gFontSystem ? gFontSystem->defaultFont : FONS_INVALID;
    float fontSize = 0.5f;
    float wrapWidth = 0;
    float lineSpacing = 1.3f;
    
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
        } else if (strcmp(argv[i], "-wrap") == 0) {
            double tempWrap;
            if (Tcl_GetDouble(interp, argv[i+1], &tempWrap) != TCL_OK) {
                return TCL_ERROR;
            }
            wrapWidth = (float)tempWrap;
        } else if (strcmp(argv[i], "-spacing") == 0) {
            double tempSpacing;
            if (Tcl_GetDouble(interp, argv[i+1], &tempSpacing) != TCL_OK) {
                return TCL_ERROR;
            }
            lineSpacing = (float)tempSpacing;
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
    
    /* Apply optional settings */
    TEXT_OBJ* t = (TEXT_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    t->wrapWidth = wrapWidth;
    t->lineSpacing = lineSpacing;
    if (wrapWidth > 0 || lineSpacing != 1.3f) {
        t->dirty = 1;
        text_build_geometry(t);
    }
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

/* textString id ?newstring? - Get or update text content */
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

/* textValign id ?top|center|bottom? - Get or set vertical alignment */
static int textvalignCmd(ClientData clientData, Tcl_Interp *interp,
                         int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST*)clientData;
    int id;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id ?top|center|bottom?", NULL);
        return TCL_ERROR;
    }
    
    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], TextID, "text")) < 0)
        return TCL_ERROR;
    
    TEXT_OBJ* t = (TEXT_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        const char* names[] = {"top", "center", "bottom"};
        Tcl_SetResult(interp, (char*)names[t->valign], TCL_STATIC);
        return TCL_OK;
    }
    
    if (strcmp(argv[2], "top") == 0) {
        t->valign = TEXT_VALIGN_TOP;
    } else if (strcmp(argv[2], "center") == 0) {
        t->valign = TEXT_VALIGN_CENTER;
    } else if (strcmp(argv[2], "bottom") == 0) {
        t->valign = TEXT_VALIGN_BOTTOM;
    } else {
        Tcl_AppendResult(interp, argv[0], ": invalid vertical alignment: ", argv[2], NULL);
        return TCL_ERROR;
    }
    
    t->dirty = 1;
    return TCL_OK;
}

/* textWrap id ?width? - Get or set wrap width (0 = no wrap) */
static int textwrapCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST*)clientData;
    int id;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id ?width?", NULL);
        return TCL_ERROR;
    }
    
    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], TextID, "text")) < 0)
        return TCL_ERROR;
    
    TEXT_OBJ* t = (TEXT_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(t->wrapWidth));
        return TCL_OK;
    }
    
    double width;
    if (Tcl_GetDouble(interp, argv[2], &width) != TCL_OK) return TCL_ERROR;
    
    t->wrapWidth = (float)width;
    t->dirty = 1;
    
    return TCL_OK;
}

/* textSpacing id ?multiplier? - Get or set line spacing */
static int textspacingCmd(ClientData clientData, Tcl_Interp *interp,
                          int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST*)clientData;
    int id;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id ?multiplier?", NULL);
        return TCL_ERROR;
    }
    
    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], TextID, "text")) < 0)
        return TCL_ERROR;
    
    TEXT_OBJ* t = (TEXT_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(t->lineSpacing));
        return TCL_OK;
    }
    
    double spacing;
    if (Tcl_GetDouble(interp, argv[2], &spacing) != TCL_OK) return TCL_ERROR;
    
    t->lineSpacing = (float)spacing;
    t->dirty = 1;
    
    return TCL_OK;
}

/* Calculate bounding box based on justify and valign settings
 * Returns bounds in object-local coordinates (before any transforms)
 */
static void text_calc_bounds(TEXT_OBJ* t, float* x0, float* y0, float* x1, float* y1) {
    float w = t->width;
    float h = t->height;
    
    /* X bounds based on horizontal justification */
    switch (t->justify) {
        case TEXT_JUSTIFY_LEFT:
            *x0 = 0;
            *x1 = w;
            break;
        case TEXT_JUSTIFY_CENTER:
            *x0 = -w / 2.0f;
            *x1 = w / 2.0f;
            break;
        case TEXT_JUSTIFY_RIGHT:
            *x0 = -w;
            *x1 = 0;
            break;
        default:
            *x0 = -w / 2.0f;
            *x1 = w / 2.0f;
    }
    
    /* Y bounds based on vertical alignment
     * Note: Y increases upward in stim2 coordinates
     */
    switch (t->valign) {
        case TEXT_VALIGN_TOP:
            *y0 = -h;
            *y1 = 0;
            break;
        case TEXT_VALIGN_CENTER:
            *y0 = -h / 2.0f;
            *y1 = h / 2.0f;
            break;
        case TEXT_VALIGN_BOTTOM:
            *y0 = 0;
            *y1 = h;
            break;
        default:
            *y0 = -h / 2.0f;
            *y1 = h / 2.0f;
    }
}

/* textBounds id - Get bounding box as list {x0 y0 x1 y1} */
static int textboundsCmd(ClientData clientData, Tcl_Interp *interp,
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
    
    float x0, y0, x1, y1;
    text_calc_bounds(t, &x0, &y0, &x1, &y1);
    
    Tcl_Obj* list = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, list, Tcl_NewDoubleObj(x0));
    Tcl_ListObjAppendElement(interp, list, Tcl_NewDoubleObj(y0));
    Tcl_ListObjAppendElement(interp, list, Tcl_NewDoubleObj(x1));
    Tcl_ListObjAppendElement(interp, list, Tcl_NewDoubleObj(y1));
    
    Tcl_SetObjResult(interp, list);
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
    
    /* Count lines */
    int numLines = 1;
    for (const char* p = t->string; *p; p++) {
        if (*p == '\n') numLines++;
    }
    
    /* Calculate bounds */
    float x0, y0, x1, y1;
    text_calc_bounds(t, &x0, &y0, &x1, &y1);
    
    Tcl_Obj* dict = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("width", -1), Tcl_NewDoubleObj(t->width));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("height", -1), Tcl_NewDoubleObj(t->height));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("ascender", -1), Tcl_NewDoubleObj(t->ascender));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("descender", -1), Tcl_NewDoubleObj(t->descender));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("fontSize", -1), Tcl_NewDoubleObj(t->fontSize));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("numChars", -1), Tcl_NewIntObj(strlen(t->string)));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("numLines", -1), Tcl_NewIntObj(numLines));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("wrapWidth", -1), Tcl_NewDoubleObj(t->wrapWidth));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("lineSpacing", -1), Tcl_NewDoubleObj(t->lineSpacing));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("x0", -1), Tcl_NewDoubleObj(x0));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("y0", -1), Tcl_NewDoubleObj(y0));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("x1", -1), Tcl_NewDoubleObj(x1));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("y1", -1), Tcl_NewDoubleObj(y1));
    
    Tcl_SetObjResult(interp, dict);
    return TCL_OK;
}

/* Parse edge name to enum value */
enum {
    EDGE_TOP = 0,
    EDGE_BOTTOM,
    EDGE_LEFT,
    EDGE_RIGHT,
    EDGE_CENTERX,
    EDGE_CENTERY,
    EDGE_INVALID
};

static int parse_edge(const char* name) {
    if (strcmp(name, "top") == 0) return EDGE_TOP;
    if (strcmp(name, "bottom") == 0) return EDGE_BOTTOM;
    if (strcmp(name, "left") == 0) return EDGE_LEFT;
    if (strcmp(name, "right") == 0) return EDGE_RIGHT;
    if (strcmp(name, "centerx") == 0) return EDGE_CENTERX;
    if (strcmp(name, "centery") == 0) return EDGE_CENTERY;
    /* Shorthand aliases */
    if (strcmp(name, "t") == 0) return EDGE_TOP;
    if (strcmp(name, "b") == 0) return EDGE_BOTTOM;
    if (strcmp(name, "l") == 0) return EDGE_LEFT;
    if (strcmp(name, "r") == 0) return EDGE_RIGHT;
    if (strcmp(name, "cx") == 0) return EDGE_CENTERX;
    if (strcmp(name, "cy") == 0) return EDGE_CENTERY;
    return EDGE_INVALID;
}

/* Get edge coordinate from bounds */
static float get_edge_coord(int edge, float x0, float y0, float x1, float y1) {
    switch (edge) {
        case EDGE_TOP:     return y1;
        case EDGE_BOTTOM:  return y0;
        case EDGE_LEFT:    return x0;
        case EDGE_RIGHT:   return x1;
        case EDGE_CENTERX: return (x0 + x1) / 2.0f;
        case EDGE_CENTERY: return (y0 + y1) / 2.0f;
        default: return 0;
    }
}

/* Check if edge is vertical (top/bottom/centery) or horizontal (left/right/centerx) */
static int is_vertical_edge(int edge) {
    return (edge == EDGE_TOP || edge == EDGE_BOTTOM || edge == EDGE_CENTERY);
}

/* textAlign targetId targetEdge refId refEdge ?gap?
 * Aligns target's edge to reference's edge with optional gap.
 * 
 * Examples:
 *   textAlign $body top $title bottom 0.3   ;# body's top at title's bottom - 0.3
 *   textAlign $label left $body right 0.4   ;# label's left at body's right + 0.4
 *
 * Gap is subtracted for top/right alignment (moving away from reference)
 * Gap is added for bottom/left alignment (moving away from reference)
 */
static int textalignCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST*)clientData;
    int targetId, refId;
    double gap = 0;
    
    if (argc < 5) {
        Tcl_AppendResult(interp, "usage: ", argv[0], 
            " targetId targetEdge refId refEdge ?gap?\n"
            "  edges: top/t, bottom/b, left/l, right/r, centerx/cx, centery/cy", NULL);
        return TCL_ERROR;
    }
    
    /* Parse target */
    if ((targetId = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], TextID, "text")) < 0)
        return TCL_ERROR;
    
    int targetEdge = parse_edge(argv[2]);
    if (targetEdge == EDGE_INVALID) {
        Tcl_AppendResult(interp, argv[0], ": invalid target edge: ", argv[2], NULL);
        return TCL_ERROR;
    }
    
    /* Parse reference */
    if ((refId = resolveObjId(interp, OL_NAMEINFO(olist), argv[3], TextID, "text")) < 0)
        return TCL_ERROR;
    
    int refEdge = parse_edge(argv[4]);
    if (refEdge == EDGE_INVALID) {
        Tcl_AppendResult(interp, argv[0], ": invalid reference edge: ", argv[4], NULL);
        return TCL_ERROR;
    }
    
    /* Check edge compatibility */
    if (is_vertical_edge(targetEdge) != is_vertical_edge(refEdge)) {
        Tcl_AppendResult(interp, argv[0], 
            ": cannot align vertical edge to horizontal edge", NULL);
        return TCL_ERROR;
    }
    
    /* Parse optional gap */
    if (argc > 5) {
        if (Tcl_GetDouble(interp, argv[5], &gap) != TCL_OK)
            return TCL_ERROR;
    }
    
    /* Get text objects */
    GR_OBJ* targetObj = OL_OBJ(olist, targetId);
    GR_OBJ* refObj = OL_OBJ(olist, refId);
    TEXT_OBJ* target = (TEXT_OBJ*)GR_CLIENTDATA(targetObj);
    TEXT_OBJ* ref = (TEXT_OBJ*)GR_CLIENTDATA(refObj);
    
    /* Ensure geometry is up to date */
    if (target->dirty) text_build_geometry(target);
    if (ref->dirty) text_build_geometry(ref);
    
    /* Get bounds for both objects */
    float tx0, ty0, tx1, ty1;
    float rx0, ry0, rx1, ry1;
    text_calc_bounds(target, &tx0, &ty0, &tx1, &ty1);
    text_calc_bounds(ref, &rx0, &ry0, &rx1, &ry1);
    
    /* Get current translation from position (set by gobjTranslateObj/translateObj) */
    float refTransX = GR_TX(refObj);
    float refTransY = GR_TY(refObj);
    float targetTransX = GR_TX(targetObj);
    float targetTransY = GR_TY(targetObj);
    
    /* Calculate edge positions in world coordinates */
    float targetEdgeCoord = get_edge_coord(targetEdge, tx0, ty0, tx1, ty1) + 
                           (is_vertical_edge(targetEdge) ? targetTransY : targetTransX);
    float refEdgeCoord = get_edge_coord(refEdge, rx0, ry0, rx1, ry1) +
                        (is_vertical_edge(refEdge) ? refTransY : refTransX);
    
    /* Calculate required translation delta
     * Basic formula: move target edge to reference edge position
     * Gap: positive gap always means "add space between" the edges
     */
    float delta = refEdgeCoord - targetEdgeCoord;
    
    if (is_vertical_edge(targetEdge)) {
        /* Vertical alignment
         * If target's top aligns to ref's bottom, target is BELOW ref
         * So gap should push target further down (negative Y)
         * If target's bottom aligns to ref's top, target is ABOVE ref
         * So gap should push target further up (positive Y)
         */
        if (targetEdge == EDGE_TOP && refEdge == EDGE_BOTTOM) {
            /* Target below reference - gap pushes down */
            delta -= gap;
        } else if (targetEdge == EDGE_BOTTOM && refEdge == EDGE_TOP) {
            /* Target above reference - gap pushes up */
            delta += gap;
        }
        /* For same-edge or center alignment, no gap adjustment */
        
        /* Set new Y position directly */
        GR_TY(targetObj) = targetTransY + delta;
    } else {
        /* Horizontal alignment
         * If target's left aligns to ref's right, target is RIGHT of ref
         * So gap should push target further right (positive X)
         * If target's right aligns to ref's left, target is LEFT of ref  
         * So gap should push target further left (negative X)
         */
        if (targetEdge == EDGE_LEFT && refEdge == EDGE_RIGHT) {
            /* Target to right of reference - gap pushes right */
            delta += gap;
        } else if (targetEdge == EDGE_RIGHT && refEdge == EDGE_LEFT) {
            /* Target to left of reference - gap pushes left */
            delta -= gap;
        }
        /* For same-edge or center alignment, no gap adjustment */
        
        /* Set new X position directly */
        GR_TX(targetObj) = targetTransX + delta;
    }
    
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
    Tcl_CreateCommand(interp, "textValign", (Tcl_CmdProc*)textvalignCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textWrap", (Tcl_CmdProc*)textwrapCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textSpacing", (Tcl_CmdProc*)textspacingCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textInfo", (Tcl_CmdProc*)textinfoCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textBounds", (Tcl_CmdProc*)textboundsCmd,
                      (ClientData)OBJList, NULL);
    Tcl_CreateCommand(interp, "textAlign", (Tcl_CmdProc*)textalignCmd,
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
