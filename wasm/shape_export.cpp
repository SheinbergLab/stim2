/*
 * shape_export.cpp
 *   A small Tcl interpreter whose `shape` command paints one polygon with
 *   the same plutovg routine the native `shape` command uses.
 *
 *   The first two arguments are dlsh dynlist names, looked up by
 *   tclFindDynList, the same call native shape.cpp makes. A name such
 *   as %list4%:0:0 is what `$polys:$i:0` becomes after Tcl substitutes
 *   the list's name. The -fill option is the one the stim script writes.
 *
 *   Pixels are plutovg's premultiplied ARGB32: B,G,R,A in memory, the
 *   same bytes svgUploadArgb32 receives.
 */

#include "shape_raster.h"
#include "dlnumeric.h"
#include "obj_host.h"

#include <cstring>
#include <string>
#include <tcl.h>

#include <emscripten/html5.h>

extern "C" {
int Dl_Init(Tcl_Interp *interp);
int tclFindDynList(Tcl_Interp *interp, char *name, DYN_LIST **dl);
}

#include <pthread.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

/* tclUnixThrd.c calls this. Emscripten's non-threaded libc does not
   define it, and this build never creates a second thread. */
extern "C" int pthread_kill(pthread_t, int) { return 0; }

static unsigned char *pixels = nullptr;
static size_t pixels_cap = 0;
static int side = 0;
static int stride = 0;
static float span_out = -1.f;
static char errbuf[512];
static Tcl_Interp *interp = nullptr;

static void set_err(const char *msg) {
    std::snprintf(errbuf, sizeof errbuf, "%s", msg ? msg : "tcl error");
}

static int store_surface(plutovg_surface_t *surface, int size, float span) {
    side = size;
    stride = plutovg_surface_get_stride(surface);
    size_t nbytes = (size_t)stride * (size_t)side;
    if (pixels_cap < nbytes) {
        free(pixels);
        pixels = (unsigned char *)malloc(nbytes);
        pixels_cap = pixels ? nbytes : 0;
    }
    if (!pixels) {
        plutovg_surface_destroy(surface);
        set_err("shape: out of memory");
        return TCL_ERROR;
    }
    memcpy(pixels, plutovg_surface_get_data(surface), nbytes);
    plutovg_surface_destroy(surface);
    span_out = span;
    return TCL_OK;
}

static int read_dynlist(Tcl_Interp *ip, Tcl_Obj *nameObj, std::vector<double> &out) {
    DYN_LIST *dl = nullptr;
    if (tclFindDynList(ip, Tcl_GetString(nameObj), &dl) != TCL_OK) return TCL_ERROR;
    if (!dl || !dlnIsNumeric(DYN_LIST_DATATYPE(dl))) {
        set_err("shape: xs and ys must be numeric lists");
        Tcl_SetObjResult(ip, Tcl_NewStringObj(errbuf, -1));
        return TCL_ERROR;
    }
    int n = DYN_LIST_N(dl);
    out.resize((size_t)n);
    for (int i = 0; i < n; i++) out[(size_t)i] = dlnGet(dl, i);
    return TCL_OK;
}

static int shapeCmd(void *, Tcl_Interp *ip, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc < 3) {
        set_err("usage: shape xs ys ?-fill {r g b}|none?");
        Tcl_SetObjResult(ip, Tcl_NewStringObj(errbuf, -1));
        return TCL_ERROR;
    }

    std::vector<double> xs, ys;
    if (read_dynlist(ip, objv[1], xs) != TCL_OK) return TCL_ERROR;
    if (read_dynlist(ip, objv[2], ys) != TCL_OK) return TCL_ERROR;
    if (xs.size() != ys.size() || xs.size() < 3) {
        set_err("shape: xs and ys need the same length, at least 3");
        Tcl_SetObjResult(ip, Tcl_NewStringObj(errbuf, -1));
        return TCL_ERROR;
    }

    ShapeRasterSpec spec = shapeRasterDefaults();
    for (Tcl_Size i = 3; i < objc; i += 2) {
        if (i + 1 >= objc) {
            set_err("shape: option needs a value");
            Tcl_SetObjResult(ip, Tcl_NewStringObj(errbuf, -1));
            return TCL_ERROR;
        }
        const char *opt = Tcl_GetString(objv[i]);
        if (std::strcmp(opt, "-filter") == 0) continue;
        if (std::strcmp(opt, "-fill") != 0) {
            std::snprintf(errbuf, sizeof errbuf, "shape: unknown option \"%s\"", opt);
            Tcl_SetObjResult(ip, Tcl_NewStringObj(errbuf, -1));
            return TCL_ERROR;
        }
        const char *val = Tcl_GetString(objv[i + 1]);
        if (std::strcmp(val, "none") == 0) {
            spec.has_fill = 0;
            continue;
        }
        Tcl_Size nc = 0;
        Tcl_Obj **cv = nullptr;
        if (Tcl_ListObjGetElements(ip, objv[i + 1], &nc, &cv) != TCL_OK)
            return TCL_ERROR;
        if (nc != 3) {
            set_err("shape: -fill needs {r g b} or none");
            Tcl_SetObjResult(ip, Tcl_NewStringObj(errbuf, -1));
            return TCL_ERROR;
        }
        for (int c = 0; c < 3; c++) {
            double d = 0;
            if (Tcl_GetDoubleFromObj(ip, cv[c], &d) != TCL_OK) return TCL_ERROR;
            spec.fill[c] = (float)d;
        }
        spec.has_fill = 1;
    }

    if (!spec.has_fill && spec.stroke_w <= 0.f) {
        set_err("shape: -fill none with no -stroke would draw nothing");
        Tcl_SetObjResult(ip, Tcl_NewStringObj(errbuf, -1));
        return TCL_ERROR;
    }

    plutovg_surface_t *surface = nullptr;
    float span = shapeRasterPaint(xs.data(), ys.data(), (int)xs.size(), spec, &surface);
    if (span < 0.f || !surface) {
        set_err("shape: error rasterizing path");
        Tcl_SetObjResult(ip, Tcl_NewStringObj(errbuf, -1));
        return TCL_ERROR;
    }
    if (store_surface(surface, spec.size, span) != TCL_OK) return TCL_ERROR;
    return shapeMakeObject(ip, pixels, side, stride, span_out);
}

static void ensure_interp() {
    if (interp) return;
    interp = Tcl_CreateInterp();
    if (!interp) return;
    if (Dl_Init(interp) != TCL_OK) {
        const char *msg = Tcl_GetStringResult(interp);
        set_err(msg && msg[0] ? msg : "Dl_Init failed");
        Tcl_DeleteInterp(interp);
        interp = nullptr;
        return;
    }
    if (stimObjectsInit(interp) != TCL_OK) {
        const char *msg = Tcl_GetStringResult(interp);
        set_err(msg && msg[0] ? msg : "object list init failed");
        Tcl_DeleteInterp(interp);
        interp = nullptr;
        return;
    }
    Tcl_CreateObjCommand2(interp, "shape", shapeCmd, nullptr, nullptr);
}

static void register_shape(void) {
    if (interp) Tcl_CreateObjCommand2(interp, "shape", shapeCmd, nullptr, nullptr);
}

extern "C" int Polygon_Init(Tcl_Interp *);
extern "C" int Image_Init(Tcl_Interp *);
extern "C" int Text_Init(Tcl_Interp *);
extern "C" int Shader_Init(Tcl_Interp *);
extern "C" int Mesh_Init(Tcl_Interp *);
extern "C" int Motionpatch_Init(Tcl_Interp *);
extern "C" int Svg_Init(Tcl_Interp *);
extern "C" int Box_Init(Tcl_Interp *);
extern "C" int Spine_Init(Tcl_Interp *);
extern "C" int World_Init(Tcl_Interp *);
extern "C" int Video_Init(Tcl_Interp *);
extern "C" void stim_timer_start(void);

extern "C" {

/* The ess log stores stimdg as a raw dg blob. dg_fromString is dlsh's
   loader; the name is forced so fractal_stim.tcl's stimdg: lookups hit it. */
int stim_load_dg(const unsigned char *data, int len) {
    if (!data || len < 5) {
        set_err("stim_load_dg: empty dg");
        return -1;
    }
    ensure_interp();
    if (!interp) return -1;
    Tcl_Obj *objv[3];
    objv[0] = Tcl_NewStringObj("dg_fromString", -1);
    objv[1] = Tcl_NewByteArrayObj(data, len);
    objv[2] = Tcl_NewStringObj("stimdg", -1);
    for (int i = 0; i < 3; i++) Tcl_IncrRefCount(objv[i]);
    int rc = Tcl_EvalObjv(interp, 3, objv, 0);
    for (int i = 0; i < 3; i++) Tcl_DecrRefCount(objv[i]);
    if (rc != TCL_OK) {
        const char *msg = Tcl_GetStringResult(interp);
        set_err(msg && msg[0] ? msg : "dg_fromString failed");
        return -1;
    }
    errbuf[0] = 0;
    return 0;
}

int stim_eval(const char *script) {
    if (!script) {
        set_err("stim_eval: null script");
        return -1;
    }
    ensure_interp();
    if (!interp) {
        set_err("stim_eval: Tcl_CreateInterp failed");
        return -1;
    }
    int rc = Tcl_Eval(interp, script);
    if (rc != TCL_OK) {
        const char *msg = Tcl_GetStringResult(interp);
        if (msg && msg[0]) set_err(msg);
        else if (!errbuf[0]) set_err("tcl error");
        return -1;
    }
    errbuf[0] = 0;
    return 0;
}

const char *stim_error(void) { return errbuf; }
const char *stim_result(void) {
    return interp ? Tcl_GetStringResult(interp) : "";
}

/* Compile the graphics-module shaders on the page's WebGL context and
   register their commands. shape and svgOpacity are put back afterwards:
   the page still blits the software shape, and svgOpacity has to accept
   those objects. */
int stim_gl_init(void) {
    ensure_interp();
    if (!interp) return -1;
    /* The page already created the WebGL2 context. This adopts it so
       glCreateShader hits that context instead of an empty one. */
    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);
    attr.majorVersion = 2;
    attr.alpha = EM_FALSE;
    attr.depth = EM_FALSE;
    attr.stencil = EM_FALSE;
    attr.antialias = EM_FALSE;
    attr.premultipliedAlpha = EM_FALSE;
    attr.preserveDrawingBuffer = EM_TRUE;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE handle =
        emscripten_webgl_create_context("#view", &attr);
    if (handle <= 0) {
        set_err("no WebGL2 context on the page canvas");
        return -1;
    }
    if (emscripten_webgl_make_context_current(handle) != EMSCRIPTEN_RESULT_SUCCESS) {
        set_err("could not use the page WebGL context");
        return -1;
    }
    struct Mod {
        const char *name;
        int (*init)(Tcl_Interp *);
    };
    const Mod mods[] = {
        {"polygon", Polygon_Init},
        {"image", Image_Init},
        {"text", Text_Init},
        {"shader", Shader_Init},
        {"mesh", Mesh_Init},
        {"motionpatch", Motionpatch_Init},
        {"svg", Svg_Init},
        {"box2d", Box_Init},
        {"spine", Spine_Init},
        {"world", World_Init},
        {"video", Video_Init},
    };
    std::string failed;
    for (const Mod &mod : mods) {
        Tcl_ResetResult(interp);
        int bad = mod.init(interp) != TCL_OK;
        const char *msg = Tcl_GetStringResult(interp);
        /* Polygon_Init registers its commands even when the shader
           compile fails, and leaves the reason in the result. */
        if (!bad && msg && std::strstr(msg, "error")) bad = 1;
        /* The Pi provides these by loading a .so. This image has no
           dlopen, so the package has to be provided after init. */
        if (!bad && (std::strcmp(mod.name, "box2d") == 0 ||
                     std::strcmp(mod.name, "spine") == 0 ||
                     std::strcmp(mod.name, "world") == 0)) {
            if (Tcl_PkgProvide(interp, mod.name, "1.0") != TCL_OK) bad = 1;
        }
        if (bad) {
            if (!failed.empty()) failed += "; ";
            failed += mod.name;
            if (msg && msg[0]) {
                failed += ": ";
                failed += msg;
            }
        }
    }
    register_shape();
    stimRegisterOpacity(interp);
    stim_timer_start();
    if (!failed.empty()) {
        set_err(failed.c_str());
        return -1;
    }
    errbuf[0] = 0;
    return 0;
}
const unsigned char *shape_bytes(void) { return pixels; }
int shape_side(void) { return side; }
int shape_stride(void) { return stride; }
float shape_span(void) { return span_out; }

}
