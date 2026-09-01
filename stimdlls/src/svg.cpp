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
#include <vector>
#include <math.h>
#include <string.h>

#include <lunasvg.h>
#include <plutovg.h>

#include <tcl.h>
#include <df.h>
#include <tcl_dl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stim2.h>
#include <prmutil.h>
#include "objname.h"
#include "texfilter.h"

/*
 * Rasterization size.
 *
 * This used to be a four-level cache, { 64, 128, 256, 512 }, all four levels
 * rasterized on every load and select_best_cache() carrying a TODO about
 * picking among them from the modelview scale.  It never did: it returned the
 * highest valid level unconditionally, so 512 was the only level ever bound
 * and the other three were rasterized, uploaded and kept resident for nothing.
 *
 * Mipmapping is the answer that cache was reaching for, and it is a better
 * one -- the level is chosen per-fragment from the actual screen-space
 * derivatives instead of once per object, and trilinear blends between levels
 * instead of popping.  So there is one raster now, at what was the top level,
 * and the chain below it comes from glGenerateMipmap when a caller asks for
 * it.  Nothing on screen changes: 512 is what was being drawn before.
 */
#define SVG_RASTER_SIZE 512

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

    /* Rasterized texture */
    GLuint texture;
    int tex_width;
    int tex_height;
    int tex_valid;

    /* GL *minification* filter; mag filter and mip chain derive from it.
       GL_LINEAR (no mip chain) is the historical behavior and the default. */
    int filter;

    /* Set for objects built by `shape` (plutovg path) rather than `svg`
       (parsed document).  A shape has no lunasvg::Document, so the commands
       that re-render from one refuse rather than crash. */
    int is_shape;
    float shape_span;       /* half-extent in the caller's coordinates */

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
"    // The texture holds PREMULTIPLIED alpha: LunaSVG renders ARGB32\n"
"    // premultiplied and rasterize() uploads it as-is.  We blend with\n"
"    // GL_ONE/GL_ONE_MINUS_SRC_ALPHA to match, so every operation below has\n"
"    // to keep rgb == alpha * straight_rgb.  Staying premultiplied is also\n"
"    // what makes the mip chain correct -- box-filtering premultiplied\n"
"    // texels is the right operation, filtering straight alpha bleeds.\n"
"    vec4 color = texture(ourTexture, TexCoord);\n"
"    if (colorOverride == 1) {\n"
"        // replace rgb, scale alpha: rebuild premultiplied from the tint\n"
"        float a = color.a * colorTint.a;\n"
"        color = vec4(colorTint.rgb * a, a);\n"
"    } else if (colorOverride == 2) {\n"
"        // multiply: rgb by tint.rgb, and rgb and alpha both by tint.a\n"
"        color *= colorTint;\n"
"        color.rgb *= colorTint.a;\n"
"    }\n"
"    color *= opacity;   // premultiplied: scales rgb and alpha together\n"
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
"    // The texture holds PREMULTIPLIED alpha: LunaSVG renders ARGB32\n"
"    // premultiplied and rasterize() uploads it as-is.  We blend with\n"
"    // GL_ONE/GL_ONE_MINUS_SRC_ALPHA to match, so every operation below has\n"
"    // to keep rgb == alpha * straight_rgb.  Staying premultiplied is also\n"
"    // what makes the mip chain correct -- box-filtering premultiplied\n"
"    // texels is the right operation, filtering straight alpha bleeds.\n"
"    vec4 color = texture(ourTexture, TexCoord);\n"
"    if (colorOverride == 1) {\n"
"        // replace rgb, scale alpha: rebuild premultiplied from the tint\n"
"        float a = color.a * colorTint.a;\n"
"        color = vec4(colorTint.rgb * a, a);\n"
"    } else if (colorOverride == 2) {\n"
"        // multiply: rgb by tint.rgb, and rgb and alpha both by tint.a\n"
"        color *= colorTint;\n"
"        color.rgb *= colorTint.a;\n"
"    }\n"
"    color *= opacity;   // premultiplied: scales rgb and alpha together\n"
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

/*
 * Apply this object's filter to the currently bound GL_TEXTURE_2D, building
 * the mip chain if the filter samples one.  Separate from rasterize() so
 * svgFilter can change the filter without re-running LunaSVG.
 *
 * Caller must have GL_TEXTURE_2D bound to svg->texture with level 0 uploaded.
 */
static void apply_filter(SVG_OBJ *svg) {
    int minfilter = svg->filter;

    /* MAG never takes a *_MIPMAP_* enum - that is GL_INVALID_ENUM */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minfilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    texMagFilterFor(minfilter));

    if (texNeedsMipmap(minfilter)) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                        texMipLevelCount(svg->tex_width, svg->tex_height) - 1);
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        /* only level 0 exists; say so rather than leaving the default 1000 */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    }
}

/*
 * Upload one premultiplied-ARGB32 buffer as the object's level-0 texture and
 * apply its filter.  Both producers hand us the same format: LunaSVG's Bitmap
 * and plutovg's surface are both ARGB32 premultiplied, which on a
 * little-endian machine is B,G,R,A in memory -- and premultiplied is exactly
 * what the shader and the mip chain want (see the fragment shader).
 */
static void upload_argb32(SVG_OBJ *svg, const unsigned char* src,
                          int width, int height, int stride) {
    if (!svg->texture) glGenTextures(1, &svg->texture);

    glBindTexture(GL_TEXTURE_2D, svg->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    unsigned char* rgba = (unsigned char*)malloc((size_t)width * height * 4);
    for (int y = 0; y < height; y++) {
        const unsigned char* srcRow = src + (size_t)y * stride;
        unsigned char* dstRow = rgba + (size_t)y * width * 4;
        for (int x = 0; x < width; x++) {
            dstRow[x*4 + 0] = srcRow[x*4 + 2];  // R
            dstRow[x*4 + 1] = srcRow[x*4 + 1];  // G
            dstRow[x*4 + 2] = srcRow[x*4 + 0];  // B
            dstRow[x*4 + 3] = srcRow[x*4 + 3];  // A
        }
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);

    svg->tex_width = width;
    svg->tex_height = height;
    svg->tex_valid = 1;

    apply_filter(svg);          /* needs tex_width/height, so set them first */

    glBindTexture(GL_TEXTURE_2D, 0);
}

/* Rasterize the SVG and upload it */
static int rasterize(SVG_OBJ *svg) {
    if (!svg->document) return -1;

    int width, height;

    /* Maintain aspect ratio */
    if (svg->aspect_ratio >= 1.0f) {
        width = SVG_RASTER_SIZE;
        height = (int)(SVG_RASTER_SIZE / svg->aspect_ratio);
    } else {
        height = SVG_RASTER_SIZE;
        width = (int)(SVG_RASTER_SIZE * svg->aspect_ratio);
    }

    if (width < 1) width = 1;
    if (height < 1) height = 1;

    /* Render using LunaSVG */
    lunasvg::Bitmap bitmap = svg->document->renderToBitmap(width, height);
    if (bitmap.isNull()) {
        fprintf(getConsoleFP(), "SVG: Failed to render to bitmap at %dx%d\n", width, height);
        return -1;
    }

    upload_argb32(svg, bitmap.data(), width, height, (int)bitmap.stride());

    return 0;
}

/****************************************************************/
/*        Direct path rasterization (no SVG document)           */
/****************************************************************/

/*
 * `shape` rasterizes a polygon given as two dynlists straight through
 * plutovg -- the same rasterizer LunaSVG uses, reached without building and
 * reparsing an SVG string.  plutovg is already linked here, so this costs no
 * new dependency.
 *
 * The quad is sized in the CALLER'S coordinates rather than normalized to a
 * unit square: the raster covers exactly [-span,+span] and the vertices span
 * the same, so one unit of outline coordinate is one unit of scaleObj.  A
 * caller scales by the size it wants and is done -- no 2*span factor to
 * remember, which is the part of the SVG route that was easy to get wrong.
 */

static double dl_elt(DYN_LIST *dl, int i) {
    switch (DYN_LIST_DATATYPE(dl)) {
    case DF_FLOAT: return ((float *)DYN_LIST_VALS(dl))[i];
    case DF_LONG:  return ((int *)DYN_LIST_VALS(dl))[i];
    case DF_SHORT: return ((short *)DYN_LIST_VALS(dl))[i];
    case DF_CHAR:  return ((char *)DYN_LIST_VALS(dl))[i];
    default:       return 0.0;
    }
}

static int dl_numeric(DYN_LIST *dl) {
    switch (DYN_LIST_DATATYPE(dl)) {
    case DF_FLOAT: case DF_LONG: case DF_SHORT: case DF_CHAR: return 1;
    default: return 0;
    }
}

/*
 * Paint follows SVG's model rather than inventing one: a shape is FILLED
 * unless -fill is "none", and STROKED whenever -stroke is greater than zero,
 * so asking for both gets both (fill first, stroke over it).  -strokecolor
 * defaults to the fill colour, which keeps the common "just draw me an
 * outline in this colour" case to -fill none -stroke w -strokecolor c.
 *
 * Widths -- stroke, dashes, pad -- are all in the CALLER'S coordinates, not
 * pixels, so they stay meaningful however the object is later scaled.
 */
struct ShapeSpec {
    float fill[3]        = { 1.f, 1.f, 1.f };
    int   has_fill       = 1;
    float stroke_col[3]  = { 1.f, 1.f, 1.f };
    int   has_stroke_col = 0;       /* if unset, the stroke uses fill[] */
    float stroke_w       = 0.f;     /* 0 = no stroke */
    float pad            = 0.f;
    int   size           = SVG_RASTER_SIZE;
    int   filter         = GL_LINEAR;
    int   closed         = 1;
    float dash_offset    = 0.f;
    std::vector<float> dashes;      /* caller units; empty = solid */
    plutovg_line_join_t join = PLUTOVG_LINE_JOIN_ROUND;
    plutovg_line_cap_t  cap  = PLUTOVG_LINE_CAP_BUTT;
};

/* Returns the fitted half-extent, or -1 on failure. */
static float shape_rasterize(SVG_OBJ *svg, DYN_LIST *xs, DYN_LIST *ys,
                             const ShapeSpec& spec) {
    int n = DYN_LIST_N(xs);

    /* Fit: the box has to hold the outline plus half the stroke, since a
       stroke straddles the path -- exactly the span rule the SVG route used. */
    double m = 0.0;
    for (int i = 0; i < n; i++) {
        double ax = fabs(dl_elt(xs, i)), ay = fabs(dl_elt(ys, i));
        if (ax > m) m = ax;
        if (ay > m) m = ay;
    }
    float span = (float)(m + spec.stroke_w/2.0 + spec.pad);
    if (span <= 0.f) return -1.f;

    int size = spec.size;
    plutovg_surface_t* surface = plutovg_surface_create(size, size);
    if (!surface) return -1.f;
    plutovg_canvas_t* canvas = plutovg_canvas_create(surface);

    /* outline units -> raster pixels, y flipped (texture rows run downward) */
    double k = size / (2.0 * span);
    for (int i = 0; i < n; i++) {
        float px = (float)((dl_elt(xs, i) + span) * k);
        float py = (float)((span - dl_elt(ys, i)) * k);
        if (i == 0) plutovg_canvas_move_to(canvas, px, py);
        else        plutovg_canvas_line_to(canvas, px, py);
    }
    if (spec.closed) plutovg_canvas_close_path(canvas);

    /* fill first, stroke over it -- SVG's order */
    if (spec.has_fill) {
        plutovg_canvas_set_rgba(canvas, spec.fill[0], spec.fill[1],
                                spec.fill[2], 1.f);
        /* preserve: a stroke may still need the path */
        plutovg_canvas_fill_preserve(canvas);
    }
    if (spec.stroke_w > 0.f) {
        const float *sc = spec.has_stroke_col ? spec.stroke_col : spec.fill;
        plutovg_canvas_set_rgba(canvas, sc[0], sc[1], sc[2], 1.f);
        plutovg_canvas_set_line_width(canvas, (float)(spec.stroke_w * k));
        plutovg_canvas_set_line_join(canvas, spec.join);
        plutovg_canvas_set_line_cap(canvas, spec.cap);
        if (!spec.dashes.empty()) {
            std::vector<float> px_dashes;
            px_dashes.reserve(spec.dashes.size());
            for (float d : spec.dashes) px_dashes.push_back((float)(d * k));
            plutovg_canvas_set_dash(canvas, (float)(spec.dash_offset * k),
                                    px_dashes.data(), (int)px_dashes.size());
        }
        plutovg_canvas_stroke(canvas);
    }

    upload_argb32(svg, plutovg_surface_get_data(surface), size, size,
                  plutovg_surface_get_stride(surface));

    plutovg_canvas_destroy(canvas);
    plutovg_surface_destroy(surface);
    return span;
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
    
    return rasterize(svg);
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

    return rasterize(svg);
}

/* Drawing function */
void svgShow(GR_OBJ *gobj) {
    SVG_OBJ *svg = (SVG_OBJ *) GR_CLIENTDATA(gobj);
    
    if (!svg->visible) return;

    if (!svg->tex_valid) return;

    float modelview[16], projection[16];
    stimGetMatrix(STIM_MODELVIEW_MATRIX, modelview);
    stimGetMatrix(STIM_PROJECTION_MATRIX, projection);
    
    glEnable(GL_BLEND);
    /* premultiplied source -- see the fragment shader.  This used to be
       GL_SRC_ALPHA, which applied alpha a second time and squared it: a
       50%-opacity fill composited at 25%, and every antialiased edge came
       out darker than it was drawn. */
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    
    glUseProgram(SvgShaderProgram);
    glUniformMatrix4fv(SvgUniformModelview, 1, GL_FALSE, modelview);
    glUniformMatrix4fv(SvgUniformProjection, 1, GL_FALSE, projection);
    glUniform1f(SvgUniformOpacity, svg->opacity);
    glUniform4f(SvgUniformColorTint, svg->color[0], svg->color[1],
                svg->color[2], svg->color[3]);
    glUniform1i(SvgUniformColorOverride, svg->color_override);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, svg->texture);
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
    
    if (svg->texture) glDeleteTextures(1, &svg->texture);

    /* Free OpenGL resources */
    if (svg->vertex_buffer) glDeleteBuffers(1, &svg->vertex_buffer);
    if (svg->vao) glDeleteVertexArrays(1, &svg->vao);
    
    free((void *) svg);
}

void svgReset(GR_OBJ *gobj) {
    /* Nothing to reset */
}

int svgCreate(OBJ_LIST *objlist, const char *source, int is_file, int filter) {
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
    svg->filter = filter;

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

int shapeCreate(OBJ_LIST *objlist, DYN_LIST *xs, DYN_LIST *ys,
                const ShapeSpec& spec) {
    GR_OBJ *obj = gobjCreateObj();
    if (!obj) return -1;

    strcpy(GR_NAME(obj), "SVG");
    GR_OBJTYPE(obj) = SvgID;
    GR_DELETEFUNCP(obj) = svgDelete;
    GR_RESETFUNCP(obj) = svgReset;
    GR_ACTIONFUNCP(obj) = svgShow;

    SVG_OBJ *svg = (SVG_OBJ *) calloc(1, sizeof(SVG_OBJ));
    GR_CLIENTDATA(obj) = svg;

    svg->visible = 1;
    svg->opacity = 1.0f;
    svg->color[0] = svg->color[1] = svg->color[2] = svg->color[3] = 1.0f;
    svg->color_override = 0;
    svg->requested_width = -1;
    svg->requested_height = -1;
    svg->filter = spec.filter;
    svg->is_shape = 1;
    svg->aspect_ratio = 1.0f;

    if (init_svg_gl_resources(svg) < 0) {
        fprintf(getConsoleFP(), "shape: error initializing OpenGL resources\n");
        svgDelete(obj);
        return -1;
    }

    float span = shape_rasterize(svg, xs, ys, spec);
    if (span < 0.f) {
        fprintf(getConsoleFP(), "shape: error rasterizing path\n");
        svgDelete(obj);
        return -1;
    }
    svg->shape_span = span;
    svg->svg_width = svg->svg_height = spec.size;

    /* Quad in the CALLER'S units: the unit quad is +/-0.5, so widening it to
       +/-span makes one outline unit equal one unit of scaleObj. */
    float vertices[30];
    generate_svg_vertices(vertices, 1.0f);
    for (int i = 0; i < 6; i++) {
        vertices[i*5 + 0] *= 2.0f * span;
        vertices[i*5 + 1] *= 2.0f * span;
    }
    glBindBuffer(GL_ARRAY_BUFFER, svg->vertex_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return gobjAddObj(objlist, obj);
}

/****************************************************************/
/*                    Tcl Commands                              */
/****************************************************************/

/*
 * shape xs ys ?options?
 *
 *   -fill {r g b}|none    fill colour, or none for an unfilled shape
 *   -stroke w             stroke width; 0 or absent means no stroke
 *   -strokecolor {r g b}  stroke colour (defaults to the fill colour)
 *   -linejoin round|miter|bevel
 *   -linecap butt|round|square
 *   -dash {on off ...}    dash pattern; empty or absent means solid
 *   -dashoffset o         where in the pattern the first dash starts
 *   -close 0|1            close the path (default 1); 0 leaves it open,
 *                         which is what a contour or trajectory wants
 *   -size n               raster size in texels (default 512)
 *   -filter name          texture filter; "mipmap" for minified shapes
 *   -pad p                extra room in the fitted box
 *
 * Fill and stroke are both drawn when both are asked for, fill first -- SVG's
 * model.  Widths (-stroke, -dash, -dashoffset, -pad) are in the CALLER'S
 * coordinates, not pixels, so they stay meaningful however the object is
 * later scaled.
 */
static int shapeCmd(ClientData clientData, Tcl_Interp *interp,
                    int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    DYN_LIST *xs, *ys;
    ShapeSpec spec;
    int id;

    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
                         " xs ys ?-fill {r g b}|none? ?-stroke w?"
                         " ?-strokecolor {r g b}? ?-linejoin j? ?-linecap c?"
                         " ?-dash {on off ...}? ?-dashoffset o? ?-close 0|1?"
                         " ?-size n? ?-filter f? ?-pad p?", NULL);
        return TCL_ERROR;
    }

    if (tclFindDynList(interp, argv[1], &xs) != TCL_OK) return TCL_ERROR;
    if (tclFindDynList(interp, argv[2], &ys) != TCL_OK) return TCL_ERROR;

    if (!dl_numeric(xs) || !dl_numeric(ys)) {
        Tcl_AppendResult(interp, argv[0], ": xs and ys must be numeric lists",
                         NULL);
        return TCL_ERROR;
    }
    if (DYN_LIST_N(xs) != DYN_LIST_N(ys)) {
        Tcl_AppendResult(interp, argv[0], ": xs and ys differ in length", NULL);
        return TCL_ERROR;
    }
    if (DYN_LIST_N(xs) < 3) {
        Tcl_AppendResult(interp, argv[0], ": need at least 3 points", NULL);
        return TCL_ERROR;
    }

    for (int i = 3; i < argc; i += 2) {
        if (i + 1 >= argc) {
            Tcl_AppendResult(interp, argv[0], ": option \"", argv[i],
                             "\" needs a value", NULL);
            return TCL_ERROR;
        }
        const char *opt = argv[i], *val = argv[i+1];
        double d;
        if (!strcmp(opt, "-fill") || !strcmp(opt, "-strokecolor")) {
            int is_fill = !strcmp(opt, "-fill");
            if (is_fill && !strcmp(val, "none")) {
                spec.has_fill = 0;
                continue;
            }
            Tcl_Size nc; const char **cv;
            if (Tcl_SplitList(interp, val, &nc, &cv) != TCL_OK) return TCL_ERROR;
            if (nc != 3) {
                Tcl_Free((char *) cv);
                Tcl_AppendResult(interp, argv[0], ": ", opt, " needs {r g b}",
                                 is_fill ? " or none" : "", NULL);
                return TCL_ERROR;
            }
            for (int c = 0; c < 3; c++) {
                if (Tcl_GetDouble(interp, cv[c], &d) != TCL_OK) {
                    Tcl_Free((char *) cv); return TCL_ERROR;
                }
                if (is_fill) spec.fill[c] = (float) d;
                else         spec.stroke_col[c] = (float) d;
            }
            Tcl_Free((char *) cv);
            if (is_fill) spec.has_fill = 1; else spec.has_stroke_col = 1;
        } else if (!strcmp(opt, "-close")) {
            if (Tcl_GetBoolean(interp, val, &spec.closed) != TCL_OK)
                return TCL_ERROR;
        } else if (!strcmp(opt, "-dashoffset")) {
            if (Tcl_GetDouble(interp, val, &d) != TCL_OK) return TCL_ERROR;
            spec.dash_offset = (float) d;
        } else if (!strcmp(opt, "-dash")) {
            Tcl_Size nd; const char **dv;
            if (Tcl_SplitList(interp, val, &nd, &dv) != TCL_OK) return TCL_ERROR;
            spec.dashes.clear();
            for (Tcl_Size c = 0; c < nd; c++) {
                if (Tcl_GetDouble(interp, dv[c], &d) != TCL_OK) {
                    Tcl_Free((char *) dv); return TCL_ERROR;
                }
                if (d < 0) {
                    Tcl_Free((char *) dv);
                    Tcl_AppendResult(interp, argv[0],
                                     ": -dash lengths must not be negative",
                                     NULL);
                    return TCL_ERROR;
                }
                spec.dashes.push_back((float) d);
            }
            Tcl_Free((char *) dv);
        } else if (!strcmp(opt, "-linecap")) {
            if (!strcmp(val, "butt"))        spec.cap = PLUTOVG_LINE_CAP_BUTT;
            else if (!strcmp(val, "round"))  spec.cap = PLUTOVG_LINE_CAP_ROUND;
            else if (!strcmp(val, "square")) spec.cap = PLUTOVG_LINE_CAP_SQUARE;
            else {
                Tcl_AppendResult(interp, argv[0], ": -linecap must be butt,"
                                 " round or square", NULL);
                return TCL_ERROR;
            }
        } else if (!strcmp(opt, "-stroke")) {
            if (Tcl_GetDouble(interp, val, &d) != TCL_OK) return TCL_ERROR;
            spec.stroke_w = (float) d;
        } else if (!strcmp(opt, "-pad")) {
            if (Tcl_GetDouble(interp, val, &d) != TCL_OK) return TCL_ERROR;
            spec.pad = (float) d;
        } else if (!strcmp(opt, "-size")) {
            if (Tcl_GetInt(interp, val, &spec.size) != TCL_OK) return TCL_ERROR;
            if (spec.size < 4 || spec.size > 4096) {
                Tcl_AppendResult(interp, argv[0], ": -size out of range 4..4096",
                                 NULL);
                return TCL_ERROR;
            }
        } else if (!strcmp(opt, "-filter")) {
            if ((spec.filter = texParseFilterName(val)) < 0) {
                Tcl_AppendResult(interp, argv[0], ": unknown filter type: \"",
                                 val, "\"", NULL);
                return TCL_ERROR;
            }
        } else if (!strcmp(opt, "-linejoin")) {
            if (!strcmp(val, "round"))       spec.join = PLUTOVG_LINE_JOIN_ROUND;
            else if (!strcmp(val, "miter"))  spec.join = PLUTOVG_LINE_JOIN_MITER;
            else if (!strcmp(val, "bevel"))  spec.join = PLUTOVG_LINE_JOIN_BEVEL;
            else {
                Tcl_AppendResult(interp, argv[0], ": -linejoin must be round,"
                                 " miter or bevel", NULL);
                return TCL_ERROR;
            }
        } else {
            Tcl_AppendResult(interp, argv[0], ": unknown option \"", opt, "\"",
                             NULL);
            return TCL_ERROR;
        }
    }

    if (!spec.has_fill && spec.stroke_w <= 0.f) {
        Tcl_AppendResult(interp, argv[0], ": -fill none with no -stroke would"
                         " draw nothing", NULL);
        return TCL_ERROR;
    }

    if ((id = shapeCreate(olist, xs, ys, spec)) < 0) {
        Tcl_AppendResult(interp, argv[0], ": error creating shape", NULL);
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

static int svgCmd(ClientData clientData, Tcl_Interp *interp,
                  int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    int id;
    int filter = GL_LINEAR;     /* historical behavior; mipmapping is opt-in */

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
                         " svgfile_or_data [filter]", NULL);
        return TCL_ERROR;
    }

    if (argc > 2) {
        if ((filter = texParseFilterName(argv[2])) < 0) {
            Tcl_AppendResult(interp, argv[0], ": unknown filter type: \"",
                             argv[2], "\"", NULL);
            return TCL_ERROR;
        }
    }

    /* Detect if it's SVG data or filename */
    const char *input = argv[1];
    int is_svg_data = (strncmp(input, "<svg", 4) == 0 || strstr(input, "<svg") != NULL);

    if ((id = svgCreate(olist, input, !is_svg_data, filter)) < 0) {
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
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("filter", -1),
                   Tcl_NewStringObj(texFilterName(svg->filter), -1));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("raster_width", -1),
                   Tcl_NewIntObj(svg->tex_width));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("raster_height", -1),
                   Tcl_NewIntObj(svg->tex_height));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("kind", -1),
                   Tcl_NewStringObj(svg->is_shape ? "shape" : "svg", -1));
    if (svg->is_shape)
        Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("span", -1),
                       Tcl_NewDoubleObj(svg->shape_span));

    Tcl_SetObjResult(interp, dictObj);
    return TCL_OK;
}

/*
 * svgFilter id [filter]
 *
 * Query or set the texture filter.  "linear" (the default) and "nearest" are
 * unchanged.  "mipmap" builds a mip chain and samples it trilinearly, which
 * is what a heavily MINIFIED svg wants: a 512-wide raster drawn onto a quad
 * ~60 screen pixels across is an 8x reduction, and plain GL_LINEAR takes a
 * 2x2 tap out of that ~8x8 texel footprint and throws the rest away.  Thin
 * strokes are where it shows first.
 */
static int svgfilterCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id, filter;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [filter]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, ((ObjNameInfo*)OL_NAMEINFO(olist)), argv[1], SvgID, "svg")) < 0)
        return TCL_ERROR;

    svg = (SVG_OBJ*)GR_CLIENTDATA(OL_OBJ(olist, id));

    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(texFilterName(svg->filter), -1));
        return TCL_OK;
    }

    if ((filter = texParseFilterName(argv[2])) < 0) {
        Tcl_AppendResult(interp, argv[0], ": unknown filter type: \"",
                         argv[2], "\"", NULL);
        return TCL_ERROR;
    }

    svg->filter = filter;

    /* level 0 is already uploaded, so just re-apply - no re-rasterization */
    if (svg->tex_valid) {
        glBindTexture(GL_TEXTURE_2D, svg->texture);
        apply_filter(svg);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

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
    
    if (svg->is_shape || !svg->document) {
        Tcl_AppendResult(interp, argv[0],
                         ": no SVG document loaded (shape objects have no"
                         " document to restyle)", NULL);
        return TCL_ERROR;
    }
    
    /* Apply stylesheet */
    svg->document->applyStyleSheet(argv[2]);

    rasterize(svg);

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

    if (svg->is_shape) {
        Tcl_AppendResult(interp, argv[0],
                         ": object was built by `shape`, not from a file", NULL);
        return TCL_ERROR;
    }

    /* Delete old document */
    if (svg->document) {
        delete svg->document;
        svg->document = NULL;
    }
    
    svg->tex_valid = 0;

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
        SvgID = gobjRegisterType("svg");
        
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
    Tcl_CreateCommand(interp, "svgFilter", (Tcl_CmdProc *) svgfilterCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "shape", (Tcl_CmdProc *) shapeCmd,
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
proc svgAsset {filename args} {
    return [svg [assetFind $filename] {*}$args]
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
