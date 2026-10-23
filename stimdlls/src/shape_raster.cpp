/*
 * shape_raster.cpp
 *   The plutovg fit-and-paint used by `shape`, with no Tcl and no GL.
 */

#include "shape_raster.h"

#include <cmath>
#include <vector>

ShapeRasterSpec shapeRasterDefaults() {
    ShapeRasterSpec spec;
    spec.fill[0] = spec.fill[1] = spec.fill[2] = 1.f;
    spec.has_fill = 1;
    spec.stroke_col[0] = spec.stroke_col[1] = spec.stroke_col[2] = 1.f;
    spec.has_stroke_col = 0;
    spec.stroke_w = 0.f;
    spec.pad = 0.f;
    spec.size = 512;
    spec.closed = 1;
    spec.dash_offset = 0.f;
    spec.join = PLUTOVG_LINE_JOIN_ROUND;
    spec.cap = PLUTOVG_LINE_CAP_BUTT;
    return spec;
}

float shapeRasterPaint(const double *xs, const double *ys, int n,
                       const ShapeRasterSpec &spec,
                       plutovg_surface_t **surface) {
    if (surface) *surface = nullptr;
    if (!xs || !ys || n < 1 || spec.size < 1) return -1.f;

    /* Fit: the box has to hold the outline plus half the stroke, since a
       stroke straddles the path -- exactly the span rule the SVG route used. */
    double m = 0.0;
    for (int i = 0; i < n; i++) {
        double ax = fabs(xs[i]), ay = fabs(ys[i]);
        if (ax > m) m = ax;
        if (ay > m) m = ay;
    }
    float span = (float)(m + spec.stroke_w / 2.0 + spec.pad);
    if (span <= 0.f) return -1.f;

    int size = spec.size;
    plutovg_surface_t *surf = plutovg_surface_create(size, size);
    if (!surf) return -1.f;
    plutovg_canvas_t *canvas = plutovg_canvas_create(surf);

    /* outline units -> raster pixels, y flipped (texture rows run downward) */
    double k = size / (2.0 * span);
    for (int i = 0; i < n; i++) {
        float px = (float)((xs[i] + span) * k);
        float py = (float)((span - ys[i]) * k);
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

    plutovg_canvas_destroy(canvas);
    if (surface) *surface = surf;
    else plutovg_surface_destroy(surf);
    return span;
}
