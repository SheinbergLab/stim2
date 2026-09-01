/*
 * shape.cpp
 *   `shape` -- rasterize a polygon given as dynlists straight through
 *   plutovg, LunaSVG's own rasterizer, without building and reparsing an SVG
 *   string on the way.
 *
 *   Lives in the svg module rather than a module of its own because it
 *   produces the same object: see svg_internal.h.
 *
 * AUTHOR
 *   DLS
 */

#ifdef __linux__
#define _GNU_SOURCE
#endif

#ifdef WIN32
#include <windows.h>
#endif

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <vector>

#include <plutovg.h>

#include <tcl.h>
#include <df.h>
#include <tcl_dl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stim2.h>
#include "objname.h"
#include "texfilter.h"
#include "svg_internal.h"

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

    svgUploadArgb32(svg, plutovg_surface_get_data(surface), size, size,
                  plutovg_surface_get_stride(surface));

    plutovg_canvas_destroy(canvas);
    plutovg_surface_destroy(surface);
    return span;
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

    if (svgInitGLResources(svg) < 0) {
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
    svgGenerateVertices(vertices, 1.0f);
    for (int i = 0; i < 6; i++) {
        vertices[i*5 + 0] *= 2.0f * span;
        vertices[i*5 + 1] *= 2.0f * span;
    }
    glBindBuffer(GL_ARRAY_BUFFER, svg->vertex_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return gobjAddObj(objlist, obj);
}


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


void shapeAddCommands(Tcl_Interp *interp, OBJ_LIST *objlist)
{
    Tcl_CreateCommand(interp, "shape", (Tcl_CmdProc *) shapeCmd,
                      (ClientData) objlist, (Tcl_CmdDeleteProc *) NULL);
}
