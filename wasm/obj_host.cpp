/*
 * obj_host.cpp
 *   The stim2 object list (grobj, groups, metagroup) running inside the
 *   WebAssembly module. The window and the GPU stay in the browser.
 *   redraw walks the visible group with stim2's own matrix calls and
 *   records each shape quad for the page to blit.
 */

#include "obj_host.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <stim2.h>
#include <animate.h>
#include <objname.h>

extern "C" int Metagroup_Init(Tcl_Interp *interp);
extern "C" int resolveObjId(Tcl_Interp *interp, ObjNameInfo *info,
                            const char *arg, int reqtype, const char *cmdname);

extern "C" OBJ_LIST *OBJList = nullptr;
extern "C" int NextFrameTime = -1;

extern "C" int gladLoadGL(void) { return 1; }
extern "C" int setDynamicUpdate(int status) {
    int old = OBJList ? OL_DYNAMIC(OBJList) : 0;
    if (OBJList) OL_DYNAMIC(OBJList) = status;
    return old;
}
extern "C" void objNameClearRegistry(OBJ_LIST *olist) {
    if (olist && OL_NAMEINFO(olist))
        objNameClear((ObjNameInfo *)OL_NAMEINFO(olist));
}

extern "C" void gobjDelete(GR_OBJ *obj) {
    if (obj && GR_DELETEFUNCP(obj)) GR_DELETEFUNC(obj)(obj);
}

static int find_obj(Tcl_Interp *interp, OBJ_LIST *olist, char *name, int *id) {
    int result = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                              name, -1, nullptr);
    if (result < 0) return TCL_ERROR;
    *id = result;
    return TCL_OK;
}

/* Column-major 4x4, the same order glm uses for the modelview. */
static float mv[16];

static void mat_ident(float *m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

static void mat_mul(float *a, const float *b) {
    float t[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            t[j * 4 + i] = 0.f;
            for (int k = 0; k < 4; k++)
                t[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
        }
    }
    std::memcpy(a, t, sizeof t);
}

static void mat_translate(float *m, float x, float y, float z) {
    float t[16];
    mat_ident(t);
    t[12] = x; t[13] = y; t[14] = z;
    mat_mul(m, t);
}

static void mat_scale(float *m, float x, float y, float z) {
    float s[16];
    mat_ident(s);
    s[0] = x; s[5] = y; s[10] = z;
    mat_mul(m, s);
}

/* glm::rotate, angle in degrees, axis (x,y,z). A zero angle leaves m alone. */
static void mat_rotate(float *m, float deg, float x, float y, float z) {
    if (deg == 0.f) return;
    float len = std::sqrt(x * x + y * y + z * z);
    if (len == 0.f) return;
    x /= len; y /= len; z /= len;
    float a = deg * 0.017453292519943295f;
    float c = std::cos(a), s = std::sin(a), ic = 1.f - c;
    float r[16];
    mat_ident(r);
    r[0] = c + x * x * ic;
    r[1] = y * x * ic + z * s;
    r[2] = z * x * ic - y * s;
    r[4] = x * y * ic - z * s;
    r[5] = c + y * y * ic;
    r[6] = z * y * ic + x * s;
    r[8] = x * z * ic + y * s;
    r[9] = y * z * ic - x * s;
    r[10] = c + z * z * ic;
    mat_mul(m, r);
}

/* setModelViewMatrix for one object, against the list's own scale and
   translation. The list stays at identity unless a command changes it. */
static void set_modelview(GR_OBJ *o) {
    if (GR_USEMATRIX(o)) {
        std::memcpy(mv, GR_MATRIX(o), 16 * sizeof(float));
        mat_scale(mv, GR_SX(o), GR_SY(o), GR_SZ(o));
        return;
    }
    mat_ident(mv);
    mat_rotate(mv, OL_SPIN(OBJList), OL_AX1(OBJList), OL_AX2(OBJList), OL_AX3(OBJList));
    mat_translate(mv,
                  OL_TX(OBJList) + GR_TX(o),
                  OL_TY(OBJList) + GR_TY(o),
                  OL_TZ(OBJList) + GR_TZ(o));
    mat_rotate(mv, GR_SPIN(o), GR_AX1(o), GR_AX2(o), GR_AX3(o));
    mat_scale(mv,
              OL_SX(OBJList) * GR_SX(o),
              OL_SY(OBJList) * GR_SY(o),
              OL_SZ(OBJList) * GR_SZ(o));
}

/* glm::ortho(-halfx, halfx, -halfy, halfy, -1000, 1000), column-major.
   Positive Y is up, matching the degree positions the page blits.
   A vertex at z = 0 does not use the depth term. */
static float proj[16] = {
    1.f, 0.f, 0.f, 0.f,
    0.f, 1.f, 0.f, 0.f,
    0.f, 0.f, 1.f, 0.f,
    0.f, 0.f, 0.f, 1.f
};

extern "C" void stim_screen_params(float halfx, float halfy, int width, int height, float refresh);

extern "C" void stim_set_screen(float halfx, float halfy, int width, int height, float refresh) {
    stim_screen_params(halfx, halfy, width, height, refresh);
    if (!(halfx > 0.f) || !(halfy > 0.f)) return;
    std::memset(proj, 0, sizeof proj);
    proj[0] = 1.f / halfx;
    proj[5] = 1.f / halfy;
    proj[10] = -1.f / 1000.f;
    proj[15] = 1.f;
}

extern "C" int stimPutMatrix(int type, float *vals) {
    if (!vals) return 0;
    if (type == STIM_MODELVIEW_MATRIX) {
        std::memcpy(mv, vals, 16 * sizeof(float));
        return 1;
    }
    if (type == STIM_PROJECTION_MATRIX) {
        std::memcpy(proj, vals, 16 * sizeof(float));
        return 1;
    }
    return 0;
}

extern "C" int stimGetMatrix(int type, float *vals) {
    if (!vals) return 0;
    if (type == STIM_MODELVIEW_MATRIX) {
        std::memcpy(vals, mv, 16 * sizeof(float));
        return 1;
    }
    if (type == STIM_PROJECTION_MATRIX) {
        std::memcpy(vals, proj, 16 * sizeof(float));
        return 1;
    }
    return 0;
}

extern "C" int stimMultMatrix(int type, float t[3], float r[4], float s[3]) {
    if (type != STIM_MODELVIEW_MATRIX) return 0;
    mat_translate(mv, t[0], t[1], t[2]);
    mat_rotate(mv, r[0], r[1], r[2], r[3]);
    mat_scale(mv, s[0], s[1], s[2]);
    return 1;
}

extern "C" int stimMultGrObjMatrix(int type, GR_OBJ *g) {
    if (type != STIM_MODELVIEW_MATRIX || !g) return 0;
    mat_translate(mv, GR_TX(g), GR_TY(g), GR_TZ(g));
    mat_rotate(mv, GR_SPIN(g), GR_AX1(g), GR_AX2(g), GR_AX3(g));
    mat_scale(mv, GR_SX(g), GR_SY(g), GR_SZ(g));
    return 1;
}

struct ShapeLeaf {
    unsigned char *pixels;
    int side;
    int stride;
    float span;
    float opacity;
};

struct StimDraw {
    float cx, cy, hx, hy;
    float opacity;
    int side, stride;
    const unsigned char *pixels;
};

static StimDraw draws[64];
static int ndraws = 0;
static int shape_type = -1;

static void shape_delete(GR_OBJ *o) {
    ShapeLeaf *leaf = (ShapeLeaf *)GR_CLIENTDATA(o);
    if (!leaf) return;
    std::free(leaf->pixels);
    std::free(leaf);
    GR_CLIENTDATA(o) = nullptr;
}

static void shape_show(GR_OBJ *o) {
    ShapeLeaf *leaf = (ShapeLeaf *)GR_CLIENTDATA(o);
    if (!leaf || ndraws >= (int)(sizeof draws / sizeof draws[0])) return;
    float sx = std::sqrt(mv[0] * mv[0] + mv[1] * mv[1] + mv[2] * mv[2]);
    float sy = std::sqrt(mv[4] * mv[4] + mv[5] * mv[5] + mv[6] * mv[6]);
    StimDraw &d = draws[ndraws++];
    d.cx = mv[12];
    d.cy = mv[13];
    d.hx = leaf->span * sx;
    d.hy = leaf->span * sy;
    d.opacity = leaf->opacity;
    d.side = leaf->side;
    d.stride = leaf->stride;
    d.pixels = leaf->pixels;
}

extern "C" void drawObj(GR_OBJ *obj) {
    if (obj && GR_ACTIONFUNCP(obj)) GR_ACTIONFUNC(obj)(obj);
}

static void draw_object(GR_OBJ *o) {
    set_modelview(o);
    drawObj(o);
}

int shapeMakeObject(Tcl_Interp *interp, const unsigned char *bgra,
                               int side, int stride, float span) {
    if (!OBJList) {
        Tcl_SetResult(interp, "shape: object list is not ready", TCL_STATIC);
        return TCL_ERROR;
    }
    if (shape_type < 0) shape_type = gobjRegisterType("shape");
    GR_OBJ *obj = gobjCreateObj();
    if (!obj) {
        Tcl_SetResult(interp, "shape: error creating shape", TCL_STATIC);
        return TCL_ERROR;
    }
    std::snprintf(GR_NAME(obj), 64, "shape");
    GR_OBJTYPE(obj) = (char)shape_type;
    GR_DELETEFUNCP(obj) = shape_delete;
    GR_ACTIONFUNCP(obj) = shape_show;

    ShapeLeaf *leaf = (ShapeLeaf *)std::calloc(1, sizeof(ShapeLeaf));
    size_t nbytes = (size_t)stride * (size_t)side;
    if (!leaf) {
        gobjDestroyObj(obj);
        Tcl_SetResult(interp, "shape: out of memory", TCL_STATIC);
        return TCL_ERROR;
    }
    leaf->pixels = (unsigned char *)std::malloc(nbytes);
    if (!leaf->pixels) {
        std::free(leaf);
        gobjDestroyObj(obj);
        Tcl_SetResult(interp, "shape: out of memory", TCL_STATIC);
        return TCL_ERROR;
    }
    std::memcpy(leaf->pixels, bgra, nbytes);
    leaf->side = side;
    leaf->stride = stride;
    leaf->span = span;
    leaf->opacity = 1.f;
    GR_CLIENTDATA(obj) = leaf;

    int id = gobjAddObj(OBJList, obj);
    if (id < 0) {
        gobjDestroyObj(obj);
        Tcl_SetResult(interp, "shape: error creating shape", TCL_STATIC);
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

static int scale_cmd(void *, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = OBJList;
    int id;
    double x, y, z;
    if (argc < 2) {
        Tcl_SetResult(interp, "usage: scaleObj objid ?x [y [z]]?", TCL_STATIC);
        return TCL_ERROR;
    }
    if (find_obj(interp, olist, argv[1], &id) != TCL_OK) return TCL_ERROR;
    GR_OBJ *o = OL_OBJ(olist, id);
    if (!o) {
        Tcl_SetResult(interp, "scaleObj: invalid object specified", TCL_STATIC);
        return TCL_ERROR;
    }
    if (argc == 2) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.4f %.4f %.4f",
                      o->scale[0], o->scale[1], o->scale[2]);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
        return TCL_OK;
    }
    if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
    if (argc > 3) {
        if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;
    } else y = x;
    if (argc > 4) {
        if (Tcl_GetDouble(interp, argv[4], &z) != TCL_OK) return TCL_ERROR;
    } else z = x;
    gobjScaleObj(o, (float)x, (float)y, (float)z);
    return TCL_OK;
}

static int translate_cmd(void *, Tcl_Interp *interp, int argc, char *argv[]) {
    OBJ_LIST *olist = OBJList;
    int id;
    double x, y, z = 0;
    if (argc < 4) {
        Tcl_SetResult(interp, "usage: translateObj objid x y [z]", TCL_STATIC);
        return TCL_ERROR;
    }
    if (find_obj(interp, olist, argv[1], &id) != TCL_OK) return TCL_ERROR;
    GR_OBJ *o = OL_OBJ(olist, id);
    if (!o) {
        Tcl_SetResult(interp, "translateObj: invalid object specified", TCL_STATIC);
        return TCL_ERROR;
    }
    if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;
    if (argc > 4 && Tcl_GetDouble(interp, argv[4], &z) != TCL_OK) return TCL_ERROR;
    gobjTranslateObj(o, (float)x, (float)y, (float)z);
    return TCL_OK;
}

static int glist_init_cmd(void *, Tcl_Interp *interp, int argc, char *argv[]) {
    int ngroups;
    if (argc < 2) {
        Tcl_SetResult(interp, "usage: glistInit ngroups", TCL_STATIC);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &ngroups) != TCL_OK) return TCL_ERROR;
    glistInit(GList, ngroups);
    return TCL_OK;
}

static int glist_add_cmd(void *, Tcl_Interp *interp, int argc, char *argv[]) {
    int slot, frame = 0, status;
    if (argc < 3) {
        Tcl_SetResult(interp, "usage: glistAddObject object slot [frame]", TCL_STATIC);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &slot) != TCL_OK) return TCL_ERROR;
    if (argc > 3 && Tcl_GetInt(interp, argv[3], &frame) != TCL_OK) return TCL_ERROR;
    status = glistAddObject(GList, argv[1], slot, frame);
    if (status == 0) {
        Tcl_AppendResult(interp, argv[0], ": object \"", argv[1], "\" not found", nullptr);
        return TCL_ERROR;
    }
    if (status < 0) {
        Tcl_SetResult(interp, "glistAddObject: invalid group specified", TCL_STATIC);
        return TCL_ERROR;
    }
    return TCL_OK;
}

static int glist_visible_cmd(void *, Tcl_Interp *interp, int argc, char *argv[]) {
    int status;
    if (argc < 2) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%d", OGL_VISIBLE(GList));
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
        return TCL_OK;
    }
    if (Tcl_GetInt(interp, argv[1], &status) != TCL_OK) return TCL_ERROR;
    glistSetVisible(GList, status);
    return TCL_OK;
}

static int glist_group_cmd(void *, Tcl_Interp *interp, int argc, char *argv[]) {
    int group;
    if (argc < 2) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%d", OGL_CURGROUP(GList));
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
        return TCL_OK;
    }
    if (Tcl_GetInt(interp, argv[1], &group) != TCL_OK) return TCL_ERROR;
    if (!glistSetCurGroup(GList, group)) {
        Tcl_AppendResult(interp, argv[0], ": invalid group specified", nullptr);
        return TCL_ERROR;
    }
    return TCL_OK;
}

static int reset_obj_cmd(void *, Tcl_Interp *, int, char **) {
    objListReset(OBJList);
    return TCL_OK;
}

/* Native svgOpacity lives on the svg object. These shapes are the same
   rasters; the page multiplies the premultiplied color by this value,
   which is what the svg shader's opacity uniform does. */
static int svg_opacity_cmd(void *, Tcl_Interp *interp, int argc, char *argv[]) {
    int id;
    double opacity;
    if (argc < 2) {
        Tcl_SetResult(interp, "usage: svgOpacity id ?opacity?", TCL_STATIC);
        return TCL_ERROR;
    }
    if (find_obj(interp, OBJList, argv[1], &id) != TCL_OK) return TCL_ERROR;
    GR_OBJ *o = OL_OBJ(OBJList, id);
    if (!o || GR_OBJTYPE(o) != shape_type) {
        Tcl_SetResult(interp, "svgOpacity: not a shape", TCL_STATIC);
        return TCL_ERROR;
    }
    ShapeLeaf *leaf = (ShapeLeaf *)GR_CLIENTDATA(o);
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(leaf->opacity));
        return TCL_OK;
    }
    if (Tcl_GetDouble(interp, argv[2], &opacity) != TCL_OK) return TCL_ERROR;
    if (opacity < 0.0) opacity = 0.0;
    if (opacity > 1.0) opacity = 1.0;
    leaf->opacity = (float)opacity;
    return TCL_OK;
}

/* The page blits after the tape command. redraw must exist so the stim
   script's sample_on can finish; it does not draw. */
static int redraw_cmd(void *, Tcl_Interp *, int, char **) {
    return TCL_OK;
}

static void add_cmd(Tcl_Interp *interp, const char *name, Tcl_CmdProc *proc) {
    Tcl_CreateCommand(interp, name, proc, nullptr, nullptr);
}

void stimRegisterOpacity(Tcl_Interp *interp) {
    add_cmd(interp, "svgOpacity", (Tcl_CmdProc *)svg_opacity_cmd);
}

extern "C" void addTclCommands(Tcl_Interp *interp);
extern "C" int Animate_Init(Tcl_Interp *interp);
extern "C" void stim_bind_interp(Tcl_Interp *interp);
extern "C" void stim_register_frame_commands(Tcl_Interp *interp);
extern "C" int startAnimation(void);
extern "C" int stopAnimation(void);
extern "C" void stim_set_eye(int eye);

int stimObjectsInit(Tcl_Interp *interp) {
    OBJList = objListCreate();
    if (!OBJList) return TCL_ERROR;
    OL_NAMEINFO(OBJList) = objNameInitCommands(interp, OBJList);
    if (Metagroup_Init(interp) != TCL_OK) return TCL_ERROR;
    add_cmd(interp, "scaleObj", (Tcl_CmdProc *)scale_cmd);
    add_cmd(interp, "translateObj", (Tcl_CmdProc *)translate_cmd);
    add_cmd(interp, "glistInit", (Tcl_CmdProc *)glist_init_cmd);
    add_cmd(interp, "glistAddObject", (Tcl_CmdProc *)glist_add_cmd);
    add_cmd(interp, "glistSetVisible", (Tcl_CmdProc *)glist_visible_cmd);
    add_cmd(interp, "glistSetCurGroup", (Tcl_CmdProc *)glist_group_cmd);
    add_cmd(interp, "resetObjList", (Tcl_CmdProc *)reset_obj_cmd);
    add_cmd(interp, "svgOpacity", (Tcl_CmdProc *)svg_opacity_cmd);
    add_cmd(interp, "redraw", (Tcl_CmdProc *)redraw_cmd);
    /* The real command table. It replaces the wrappers above for
       scaleObj, the glist commands, setVisible, setBackground, redraw. */
    addTclCommands(interp);
    stim_bind_interp(interp);
    if (Animate_Init(interp) != TCL_OK) return TCL_ERROR;
    Tcl_ResetResult(interp);
    stim_register_frame_commands(interp);
    stimRegisterOpacity(interp);
    return TCL_OK;
}

static int compare_priority(const void *a, const void *b) {
    int ida = *(const int *)a;
    int idb = *(const int *)b;
    GR_OBJ *oa = OL_OBJ(OBJList, ida);
    GR_OBJ *ob = OL_OBJ(OBJList, idb);
    if (!oa && !ob) return 0;
    if (!oa) return 1;
    if (!ob) return -1;
    float diff = GR_PRIORITY(oa) - GR_PRIORITY(ob);
    if (diff != 0.f) return (diff > 0.f) - (diff < 0.f);
    return (a > b) - (a < b);
}

static void draw_eye(OBJ_GROUP *g, int eye, int (*want)(GR_OBJ *)) {
    stim_set_eye(eye);
    for (int i = 0; i < OG_NOBJS(g); i++) {
        GR_OBJ *o = OL_OBJ(OBJList, OG_OBJID(g, i));
        if (o && GR_VISIBLE(o) && (!want || want(o))) draw_object(o);
    }
}

/* videoTimer (and any other object timer) advances from StimTimeF.
   stim2 runs these once the 1 ms timer has fired. This page steps the
   clock inside stim_frame, so the timers run once per drawn frame. */
static void run_timers(OBJ_GROUP *g, int (*want)(GR_OBJ *)) {
    if (!g || !OGL_VISIBLE(GList)) return;
    for (int i = 0; i < OG_NOBJS(g); i++) {
        GR_OBJ *o = OL_OBJ(OBJList, OG_OBJID(g, i));
        if (o && GR_VISIBLE(o) && GR_TIMERFUNCP(o) && (!want || want(o)))
            GR_TIMERFUNC(o)(o);
    }
}

static int want_left(GR_OBJ *o) { return GR_LEFT_EYE(o); }
static int want_right(GR_OBJ *o) { return GR_RIGHT_EYE(o); }

extern "C" int stim_redraw(void) {
    ndraws = 0;
    if (!GList || !OGL_VISIBLE(GList) || OGL_CURGROUP(GList) >= OGL_NGROUPS(GList))
        return 0;
    OBJ_GROUP *g = OGL_GROUP(GList, OGL_CURGROUP(GList));
    if (!g) return 0;
    if (OG_NOBJS(g) > 1)
        std::qsort(OG_OBJIDLIST(g), (size_t)OG_NOBJS(g), sizeof(int), compare_priority);

    if (OGL_NEWLY_VISIBLE(GList) && g &&
        !OL_DYNAMIC(OBJList) && OG_DYNAMIC(g) == DYNAMIC_FRAME_BASED)
        startAnimation();
    else if (g && !OL_DYNAMIC_STORED(OBJList) && OL_DYNAMIC(OBJList) &&
             OG_DYNAMIC(g) != DYNAMIC_FRAME_BASED)
        stopAnimation();
    else if (OL_DYNAMIC(OBJList) && !OGL_VISIBLE(GList))
        stopAnimation();

    if (getStereoMode() == 0) {
        run_timers(g, nullptr);
        draw_eye(g, 0, nullptr);
        return ndraws;
    }
    if (OG_LEFT_EYE(g)) {
        run_timers(g, want_left);
        draw_eye(g, 0, want_left);
    }
    if (getStereoMode() != 2 && OG_RIGHT_EYE(g)) {
        run_timers(g, want_right);
        draw_eye(g, 1, want_right);
    }
    return ndraws;
}

extern "C" int stim_ndraws(void) { return ndraws; }
extern "C" float stim_draw_cx(int i) { return draws[i].cx; }
extern "C" float stim_draw_cy(int i) { return draws[i].cy; }
extern "C" float stim_draw_hx(int i) { return draws[i].hx; }
extern "C" float stim_draw_hy(int i) { return draws[i].hy; }
extern "C" float stim_draw_opacity(int i) { return draws[i].opacity; }
extern "C" int stim_draw_side(int i) { return draws[i].side; }
extern "C" int stim_draw_stride(int i) { return draws[i].stride; }
extern "C" const unsigned char *stim_draw_bytes(int i) { return draws[i].pixels; }
