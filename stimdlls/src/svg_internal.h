/*
 * svg_internal.h
 *   Shared between svg.cpp and shape.cpp, which are two front-ends onto the
 *   same object.
 *
 * The module rasterizes vector geometry into a textured quad.  svg.cpp gets
 * that geometry by parsing an SVG document with LunaSVG; shape.cpp builds a
 * path directly from dynlists through plutovg, which is LunaSVG's own
 * rasterizer.  Either way the result is one SVG_OBJ, so svgVisible,
 * svgOpacity, svgColor, svgFilter and svgInfo apply to both and there is a
 * single show/delete path and a single shader.  That shared object is why
 * these live in one module rather than two.
 *
 * Private to the module -- nothing outside stimdlls/src includes this.
 */

#ifndef SVG_INTERNAL_H
#define SVG_INTERNAL_H

#include <glad/glad.h>
#include <tcl.h>
#include <stim2.h>

namespace lunasvg { class Document; }

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
 * it.
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

    /* LunaSVG document - kept for re-rasterization and stylesheet changes.
       NULL for a shape, which has a path rather than a document. */
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

extern int SvgID;               /* gobj type, registered in Svg_Init */

/* object lifecycle, owned by svg.cpp */
void svgShow(GR_OBJ *gobj);
void svgDelete(GR_OBJ *gobj);
void svgReset(GR_OBJ *gobj);

/* shared plumbing */
void svgGenerateVertices(float *vertices, float aspect_ratio);
int  svgInitGLResources(SVG_OBJ *svg);
void svgApplyFilter(SVG_OBJ *svg);

/*
 * Upload one premultiplied-ARGB32 buffer as the object's level-0 texture and
 * apply its filter.  Both producers hand us the same format: LunaSVG's Bitmap
 * and plutovg's surface are both ARGB32 premultiplied, which on a
 * little-endian machine is B,G,R,A in memory -- and premultiplied is exactly
 * what the shader and the mip chain want (see the fragment shader).
 */
void svgUploadArgb32(SVG_OBJ *svg, const unsigned char *src,
                     int width, int height, int stride);

/* registered from Svg_Init; implemented in shape.cpp */
void shapeAddCommands(Tcl_Interp *interp, OBJ_LIST *objlist);

#endif /* SVG_INTERNAL_H */
