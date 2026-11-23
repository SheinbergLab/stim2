/*
 * photopatch.cpp -- a square in a corner of the screen that changes between
 * a dark and a light level on chosen frames, for a photodiode taped over it
 * (see docs/photopatch.md)
 *
 * A stimdll.  The diode, read by an extio box and dserv's `photo` processor,
 * says when light actually changed on the panel; the patch makes the frames
 * worth timing change it.  Each frame that changes the patch has a
 * stim/photopatch record, so an edge pairs with the frame that caused it.
 *
 * Which frames change it (the mode):
 *   marked  a frame something is timing: one that releases the reply of a
 *           swap-acked ('!') command -- the way ESS changes the display and
 *           learns which swap did it -- or delivers a swap tag (`swapTag`,
 *           a change the stim made by itself).  Every system then drives the
 *           patch with no change to it, and each edge pairs with a frame
 *           that has a swapcount on both sides.
 *   toggle  every frame: each new frame reaching the panel is an edge, and a
 *           refresh stim2 missed is one that does not come.  A static
 *           display is repainted on every refresh while it is on.
 *   manual  only `photopatch flip`.
 * `photopatch flip` works in every mode: the next frame changes it.
 *
 * It is a post pass (src/postpass.h) of priority 940: after the scene and
 * any pass that changes it, before tlock's row (950).  The default corner,
 * top right, is clear of that row (top left, x 0..508) and is where a
 * panel's scanout reaches first after the top left.
 *
 * Off by default, and `postpass reset` (every script load) turns it off: a
 * flashing corner distracts a subject, so a rig turns it on only while a
 * diode is over it.
 *
 * State is plain statics: the render thread is the only one that reads or
 * writes it (Tcl commands and the draw run there).
 */
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdarg>

#ifdef _WIN32
#include <windows.h>
#endif

#include <tcl.h>
#include <glad/glad.h>

#include "postpass.h"         /* stim2's seam for full-screen passes */

#ifndef TCL_SIZE_MAX
typedef int Tcl_Size;
#endif

extern "C" {
void redraw(void);
}

namespace {

void log_message(const char *level, const char *fmt, ...)
{
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  stimLogMessage(level, buf);
}

/* ---- settings ----------------------------------------------------------- */

enum Mode { MARKED = 0, TOGGLE = 1, MANUAL = 2 };
const char *mode_name(int m) { return m == TOGGLE ? "toggle" : m == MANUAL ? "manual" : "marked"; }

enum Corner { TL = 0, TR = 1, BL = 2, BR = 3 };
const char *corner_name(int c)
{
  static const char *n[] = { "tl", "tr", "bl", "br" };
  return n[c & 3];
}

struct Settings {
  int mode = MARKED;
  int corner = TR;
  int size = 60;                /* pixels, square */
  int inset_x = 0;              /* pixels in from the corner's side edge */
  int inset_y = 0;              /* and from its top or bottom edge */
  float dark[3] = { 0.f, 0.f, 0.f };
  float light[3] = { 1.f, 1.f, 1.f };
} cfg;

/* ---- state -------------------------------------------------------------- */

bool on = false;
int level = 0;                  /* 0 dark, 1 light: what the last frame drew */
bool first = true;              /* the next frame is the first since `on` */
int armed = -2;                 /* flip: -2 none, -1 the other level, 0/1 that */
bool settings_dirty = false;    /* a static display should show a change */

/* the frame just drawn, until its flip is known */
struct Drawn {
  bool pending = false;
  bool record = false;
  int level = 0;
  long long serial = 0;
  int ntags = 0;                /* swap tags it delivers */
  int why = 0;                  /* 1 first, 2 marked, 3 toggle, 4 flip */
  int nacks = 0;                /* '!' replies it releases */
} drawn;

long long n_frames = 0, n_changes = 0, n_marked = 0, n_repeats = 0, n_nofit = 0;
double last_change_flip_s = -1.0;
long long last_change_flip_wall_us = -1;
bool warned_nofit = false;

/* ---- GL ----------------------------------------------------------------- */

bool fits(int w, int h)
{
  return cfg.size > 0 && cfg.inset_x >= 0 && cfg.inset_y >= 0 &&
         cfg.inset_x + cfg.size <= w && cfg.inset_y + cfg.size <= h;
}

/* The square: a scissored clear, so the colour arrives as given (no
   shader, no blending).  An sRGB encode would apply to the clear: off for
   it.  GL's rows count from the bottom. */
void draw(const stim_postpass_frame_t *f, int lvl)
{
  int x = (cfg.corner == TL || cfg.corner == BL) ? cfg.inset_x : f->w - cfg.inset_x - cfg.size;
  int y = (cfg.corner == BL || cfg.corner == BR) ? cfg.inset_y : f->h - cfg.inset_y - cfg.size;
  const float *c = lvl ? cfg.light : cfg.dark;

  GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
  GLint sbox[4];
  glGetIntegerv(GL_SCISSOR_BOX, sbox);
#if defined(GL_FRAMEBUFFER_SRGB) && !defined(STIM2_USE_GLES)
  GLboolean srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
  if (srgb) glDisable(GL_FRAMEBUFFER_SRGB);
#endif
  GLboolean cmask[4];
  glGetBooleanv(GL_COLOR_WRITEMASK, cmask);
  GLfloat cc[4];
  glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);

  glBindFramebuffer(GL_FRAMEBUFFER, f->drawfb);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  if (!scissor) glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, cfg.size, cfg.size);
  glClearColor(c[0], c[1], c[2], 1.f);
  glClear(GL_COLOR_BUFFER_BIT);

  glClearColor(cc[0], cc[1], cc[2], cc[3]);
  glScissor(sbox[0], sbox[1], sbox[2], sbox[3]);
  if (!scissor) glDisable(GL_SCISSOR_TEST);
  glColorMask(cmask[0], cmask[1], cmask[2], cmask[3]);
#if defined(GL_FRAMEBUFFER_SRGB) && !defined(STIM2_USE_GLES)
  if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
#endif
  glReadBuffer(f->drawnbuf);
}

/* ---- the pass, as stim2 calls it (postpass.h) --------------------------- */

int pass_wanted(void *) { return on; }

int pass_stage(void *, const stim_postpass_frame_t *f)
{
  if (!on || f->w <= 0 || f->h <= 0) return 0;
  if (!fits(f->w, f->h)) {
    n_nofit++;
    if (!warned_nofit) {
      warned_nofit = true;
      log_message("error", "photopatch: a %d-pixel square %d,%d in from the %s corner does not fit a %dx%d frame: not drawn",
                  cfg.size, cfg.inset_x, cfg.inset_y, corner_name(cfg.corner), f->w, f->h);
    }
    return 0;
  }

  /* frameRepaint: the last frame again, so the level it showed */
  if (stimPostpassRepeating()) {
    n_repeats++;
    draw(f, level);
    return 1;
  }

  /* marked: however many '!' replies and tags, one change per frame */
  int ntags = stimSwapTagCount(), nacks = stimSwapAckCount();
  bool marked = ntags > 0 || nacks > 0;
  int why = 0, next = level;
  if (armed != -2) { next = armed == -1 ? !level : armed; why = 4; armed = -2; }
  else if (cfg.mode == TOGGLE) { next = !level; why = 3; }
  else if (cfg.mode == MARKED && marked) { next = !level; why = 2; }
  if (first) { why = 1; first = false; }   /* the patch appears: an edge too */

  draw(f, next);
  n_frames++;
  if (marked) n_marked++;
  bool changed = next != level || why == 1;
  level = next;
  settings_dirty = false;

  drawn.pending = true;
  drawn.record = changed;
  drawn.level = next;
  drawn.ntags = ntags;
  drawn.nacks = nacks;
  drawn.why = why;
  drawn.serial = changed ? ++n_changes : n_changes;
  return 1;
}

/* A static display: toggle mode wants every refresh, an armed flip or a
   changed setting the next one.  Marked frames come with frames of their
   own: a '!' command that draws nothing is not given one, so its reply
   keeps stim2's own "no intervening swap" behaviour and no edge claims it. */
int pass_repaint_due(void *, double, double *wait_s)
{
  *wait_s = -1.0;
  if (!on) return 0;
  return cfg.mode == TOGGLE || armed != -2 || first || settings_dirty;
}

/* the flip that showed a changed patch: the frame's record for the data
   file (stim/photopatch: after the swapcount stim2 puts first, the flip on
   the wall clock, the level now showing (0 dark, 1 light), which change
   since `on` it is, the swap tags the frame delivered, why it changed (1
   the patch appeared, 2 a marked frame, 3 toggle mode, 4 `flip`), and the
   '!' replies the frame released) */
void pass_flip(void *, double flip_s, long long flip_wall_us)
{
  if (!drawn.pending) return;
  drawn.pending = false;
  if (!drawn.record) return;
  long long wall = flip_wall_us > 0 ? flip_wall_us : -1;
  last_change_flip_s = flip_s;
  last_change_flip_wall_us = wall;
  long long v[6] = { wall, drawn.level, drawn.serial, drawn.ntags, drawn.why,
                     drawn.nacks };
  stimFrameRecord("stim/photopatch", v, 6);
}

/* off: the next frame is drawn without it, so ask for one */
void turn_off()
{
  armed = -2;
  if (!on) return;
  on = false;
  drawn.pending = false;
  redraw();
}

/* `postpass reset`: off and back to the defaults */
void pass_reset(void *, Tcl_Interp *)
{
  turn_off();
  cfg = Settings();
  level = 0;
  warned_nofit = false;
}

stim_postpass_t pass = {
  "photopatch", 940.0f, nullptr,
  pass_wanted, pass_stage,
  nullptr,                      /* composite: stim2 redraws the scene */
  pass_repaint_due,
  nullptr,                      /* late_ms */
  pass_flip, pass_reset
};

/* ---- the Tcl command ---------------------------------------------------- */

const char *USAGE =
  "photopatch on ?marked|toggle|manual? | off | "
  "set ?-mode m? ?-corner tl|tr|bl|br? ?-size px? ?-inset px|{x y}? ?-dark {r g b}? ?-light {r g b}? | "
  "flip ?0|1? | status | reset";

Tcl_Obj *rgb_obj(const float *c)
{
  Tcl_Obj *l = Tcl_NewListObj(0, nullptr);
  for (int i = 0; i < 3; i++) Tcl_ListObjAppendElement(nullptr, l, Tcl_NewDoubleObj(c[i]));
  return l;
}

/* one number while both insets agree (what an older stim2 said), else {x y} */
Tcl_Obj *inset_obj()
{
  if (cfg.inset_x == cfg.inset_y) return Tcl_NewIntObj(cfg.inset_x);
  Tcl_Obj *l = Tcl_NewListObj(0, nullptr);
  Tcl_ListObjAppendElement(nullptr, l, Tcl_NewIntObj(cfg.inset_x));
  Tcl_ListObjAppendElement(nullptr, l, Tcl_NewIntObj(cfg.inset_y));
  return l;
}

Tcl_Obj *settings_dict()
{
  Tcl_Obj *d = Tcl_NewDictObj();
  auto put = [&](const char *k, Tcl_Obj *v) { Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj(k, -1), v); };
  put("-mode", Tcl_NewStringObj(mode_name(cfg.mode), -1));
  put("-corner", Tcl_NewStringObj(corner_name(cfg.corner), -1));
  put("-size", Tcl_NewIntObj(cfg.size));
  put("-inset", inset_obj());
  put("-dark", rgb_obj(cfg.dark));
  put("-light", rgb_obj(cfg.light));
  return d;
}

int get_mode(Tcl_Interp *interp, Tcl_Obj *o, int *out)
{
  std::string s = Tcl_GetString(o);
  if (s == "marked") *out = MARKED;
  else if (s == "toggle") *out = TOGGLE;
  else if (s == "manual") *out = MANUAL;
  else {
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("photopatch: mode \"%s\" is marked, toggle or manual", s.c_str()));
    return TCL_ERROR;
  }
  return TCL_OK;
}

int get_range(Tcl_Interp *interp, Tcl_Obj *o, const char *what, int lo, int hi, int *out)
{
  int v;
  if (Tcl_GetIntFromObj(interp, o, &v) != TCL_OK) return TCL_ERROR;
  if (v < lo || v > hi) {
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("photopatch: %s is %d to %d", what, lo, hi));
    return TCL_ERROR;
  }
  *out = v;
  return TCL_OK;
}

/* -inset: px from both edges, or {x y}: x from the corner's side edge, y
   from its top or bottom edge */
int get_inset(Tcl_Interp *interp, Tcl_Obj *o, int *x, int *y)
{
  Tcl_Size n;
  Tcl_Obj **el;
  if (Tcl_ListObjGetElements(interp, o, &n, &el) != TCL_OK) return TCL_ERROR;
  if (n != 1 && n != 2) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("photopatch: -inset is px, or {x y}, 0 to 4096", -1));
    return TCL_ERROR;
  }
  int vx, vy;
  if (get_range(interp, el[0], "-inset", 0, 4096, &vx) != TCL_OK) return TCL_ERROR;
  if (get_range(interp, el[n - 1], "-inset", 0, 4096, &vy) != TCL_OK) return TCL_ERROR;
  *x = vx;
  *y = vy;
  return TCL_OK;
}

int get_rgb(Tcl_Interp *interp, Tcl_Obj *o, const char *what, float *out)
{
  Tcl_Size n;
  Tcl_Obj **el;
  if (Tcl_ListObjGetElements(interp, o, &n, &el) != TCL_OK) return TCL_ERROR;
  if (n != 1 && n != 3) {
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("photopatch: %s is {r g b} or one grey, 0 to 1", what));
    return TCL_ERROR;
  }
  float c[3];
  for (int i = 0; i < 3; i++) {
    double v;
    if (Tcl_GetDoubleFromObj(interp, el[n == 3 ? i : 0], &v) != TCL_OK) return TCL_ERROR;
    if (!(v >= 0.0 && v <= 1.0)) {
      Tcl_SetObjResult(interp, Tcl_ObjPrintf("photopatch: %s values are 0 to 1", what));
      return TCL_ERROR;
    }
    c[i] = (float) v;
  }
  memcpy(out, c, sizeof(c));
  return TCL_OK;
}

int set_cmd(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  if ((objc - 2) % 2) {
    Tcl_WrongNumArgs(interp, 2, objv,
                     "?-mode m? ?-corner tl|tr|bl|br? ?-size px? ?-inset px|{x y}? ?-dark {r g b}? ?-light {r g b}?");
    return TCL_ERROR;
  }
  Settings s = cfg;
  for (int i = 2; i < objc; i += 2) {
    std::string opt = Tcl_GetString(objv[i]);
    Tcl_Obj *val = objv[i + 1];
    int rc;
    if (opt == "-mode") rc = get_mode(interp, val, &s.mode);
    else if (opt == "-corner") {
      std::string c = Tcl_GetString(val);
      rc = TCL_OK;
      if (c == "tl") s.corner = TL;
      else if (c == "tr") s.corner = TR;
      else if (c == "bl") s.corner = BL;
      else if (c == "br") s.corner = BR;
      else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("photopatch: corner \"%s\" is tl, tr, bl or br", c.c_str()));
        rc = TCL_ERROR;
      }
    }
    else if (opt == "-size") rc = get_range(interp, val, "-size", 1, 4096, &s.size);
    else if (opt == "-inset") rc = get_inset(interp, val, &s.inset_x, &s.inset_y);
    else if (opt == "-dark") rc = get_rgb(interp, val, "-dark", s.dark);
    else if (opt == "-light") rc = get_rgb(interp, val, "-light", s.light);
    else {
      Tcl_SetObjResult(interp, Tcl_ObjPrintf("photopatch set: unknown option \"%s\" (-mode -corner -size -inset -dark -light)", opt.c_str()));
      rc = TCL_ERROR;
    }
    if (rc != TCL_OK) return TCL_ERROR;
  }
  if (objc > 2) {
    /* a moved or resized square leaves the old one behind until the scene
       is drawn again: draw it again now */
    bool moved = s.corner != cfg.corner || s.size != cfg.size ||
                 s.inset_x != cfg.inset_x || s.inset_y != cfg.inset_y;
    cfg = s;
    warned_nofit = false;
    if (on) {
      if (moved) redraw();
      else settings_dirty = true;
    }
  }
  Tcl_SetObjResult(interp, settings_dict());
  return TCL_OK;
}

Tcl_Obj *status_dict()
{
  Tcl_Obj *d = Tcl_NewDictObj();
  auto put = [&](const char *k, Tcl_Obj *v) { Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj(k, -1), v); };
  put("on", Tcl_NewIntObj(on ? 1 : 0));
  put("mode", Tcl_NewStringObj(mode_name(cfg.mode), -1));
  put("level", Tcl_NewIntObj(level));
  put("armed", Tcl_NewIntObj(armed == -2 ? 0 : 1));
  put("settings", settings_dict());
  put("frames", Tcl_NewWideIntObj(n_frames));
  put("changes", Tcl_NewWideIntObj(n_changes));
  put("marked", Tcl_NewWideIntObj(n_marked));
  put("repeats", Tcl_NewWideIntObj(n_repeats));
  put("not_drawn", Tcl_NewWideIntObj(n_nofit));
  put("last_change_flip_s", Tcl_NewDoubleObj(last_change_flip_s));
  put("last_change_flip_wall_us", Tcl_NewWideIntObj(last_change_flip_wall_us));
  return d;
}

int photopatchCmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  std::string sub = objc > 1 ? Tcl_GetString(objv[1]) : "status";

  if (sub == "status" && objc <= 2) {
    Tcl_SetObjResult(interp, status_dict());
    return TCL_OK;
  }
  /* on ?MODE?: from the next frame, starting dark */
  if (sub == "on" && objc <= 3) {
    if (objc == 3 && get_mode(interp, objv[2], &cfg.mode) != TCL_OK) return TCL_ERROR;
    if (!on) {
      on = true;
      first = true;
      level = 0;
      armed = -2;
      warned_nofit = false;
      redraw();
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(1));
    return TCL_OK;
  }
  if (sub == "off" && objc == 2) {
    turn_off();
    Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
    return TCL_OK;
  }
  if (sub == "set") return set_cmd(interp, objc, objv);
  /* flip ?LEVEL?: the next frame changes it (to LEVEL, if given) */
  if (sub == "flip" && objc <= 3) {
    int lv = -1;
    if (objc == 3 && get_range(interp, objv[2], "the level", 0, 1, &lv) != TCL_OK) return TCL_ERROR;
    if (!on) {
      Tcl_SetObjResult(interp, Tcl_NewStringObj("photopatch flip: the patch is off", -1));
      return TCL_ERROR;
    }
    armed = lv;
    Tcl_SetObjResult(interp, Tcl_NewIntObj(lv < 0 ? !level : lv));
    return TCL_OK;
  }
  if (sub == "reset" && objc == 2) {
    pass_reset(nullptr, interp);
    return TCL_OK;
  }

  Tcl_SetObjResult(interp, Tcl_ObjPrintf("usage: %s", USAGE));
  return TCL_ERROR;
}

} /* namespace */

#ifdef _WIN32
extern "C" __declspec(dllexport) int Photopatch_Init(Tcl_Interp *interp)
#else
extern "C" int Photopatch_Init(Tcl_Interp *interp)
#endif
{
  if (
#ifdef USE_TCL_STUBS
      Tcl_InitStubs(interp, "8.5-", 0)
#else
      Tcl_PkgRequire(interp, "Tcl", "8.5-", 0)
#endif
      == NULL) {
    return TCL_ERROR;
  }
  gladLoadGL();
  stimPostpassRegister(&pass);
  Tcl_CreateObjCommand(interp, "photopatch", photopatchCmd, nullptr, nullptr);
  return TCL_OK;
}
