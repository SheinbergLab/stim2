/*
 * shape_raster.h
 *   Fit a polygon and paint it with plutovg. No Tcl and no GL: shape.cpp
 *   uploads the surface, and the wasm build returns the same pixels.
 *
 *   The default raster size is 512, which is SVG_RASTER_SIZE. shape.cpp
 *   passes its own size, so a native shape still follows that macro.
 */

#ifndef SHAPE_RASTER_H
#define SHAPE_RASTER_H

#include <plutovg.h>

#include <vector>

struct ShapeRasterSpec {
    float fill[3];
    int   has_fill;
    float stroke_col[3];
    int   has_stroke_col;
    float stroke_w;
    float pad;
    int   size;
    int   closed;
    float dash_offset;
    std::vector<float> dashes;      /* caller units; empty = solid */
    plutovg_line_join_t join;
    plutovg_line_cap_t  cap;
};

/* Same defaults `shape` uses, minus the GL texture filter. */
ShapeRasterSpec shapeRasterDefaults();

/*
 * Paint xs/ys (n points, caller coordinates) into a new surface.
 * Returns the fitted half-extent. On success *surface is owned by the
 * caller, who reads it and then plutovg_surface_destroy's it. On failure
 * returns -1 and *surface is NULL.
 */
float shapeRasterPaint(const double *xs, const double *ys, int n,
                       const ShapeRasterSpec &spec,
                       plutovg_surface_t **surface);

#endif /* SHAPE_RASTER_H */
