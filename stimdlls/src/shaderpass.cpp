/*
 * shaderpass.cpp -- a fragment shader over stim2's finished scene
 * (see docs/shaderpass.md)
 *
 * A stimdll.  Each `shaderpass create NAME` is one full-screen fragment
 * shader that reads the finished scene and writes every pixel: noise over
 * the display, a colour transform, a channel swizzle.  Any number can be
 * on at once; each is a post pass in stim2's chain (src/postpass.h,
 * docs/postpass.md), ordered by its priority, so one can run before or
 * after another or the gaze window (gazewin, priority 100).
 *
 * A pass belongs to the script that made it: `postpass reset`, which
 * stim::prelude runs at every script load, deletes them all.
 *
 * State is plain statics: the render thread is the only one that reads or
 * writes it (Tcl commands and the draw run there).
 */
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdarg>

#ifdef _WIN32
#include <windows.h>
#endif

#include <tcl.h>
#include <glad/glad.h>

#include "postpass.h"         /* stim2's seam for full-screen passes */
#include "passutil.h"

#ifndef TCL_SIZE_MAX
typedef int Tcl_Size;
#endif

extern "C" {
void redraw(void);
float *getBackgroundColor(void);
}

namespace {

using namespace passutil;

void log_message(const char *level, const char *fmt, ...)
{
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  stimLogMessage(level, buf);
}

/* the pass that copies the scene through, for `create` with no shader */
const char *FS_COPY =
  "uniform sampler2D scene;\n"
  "out vec4 frag;\n"
  "void main() { frag = texelFetch(scene, ivec2(gl_FragCoord.xy), 0); }\n";

enum { ANIM_AUTO = -1, ANIM_OFF = 0, ANIM_ON = 1 };

struct Pass {
  std::string name;               /* as the command knows it */
  std::string regname;            /* in stim2's chain: "shader/NAME" */
  stim_postpass_t pp;
  bool on = false;
  int animate = ANIM_AUTO;        /* auto: animated if it declares `time` */
  bool dirty = true;              /* something changed since it last drew */
  std::string file;               /* "" = given inline, or the copy */
  Uniforms uniforms;

  GLuint prog = 0, tex = 0, fbo = 0;
  int tex_w = 0, tex_h = 0;
  bool scene_valid = false;
  struct {
    GLint scene = -1, resolution = -1, halfdeg = -1, time = -1, background = -1;
  } L;
  long long stages = 0, composites = 0;

  bool animated() const { return animate == ANIM_AUTO ? L.time >= 0 : animate == ANIM_ON; }
};

std::map<std::string, Pass *> passes;
GLuint vao = 0;                   /* shared: the triangle needs no buffers */

/* Build fs (and vs) into a program for p; on an error p keeps what it had */
bool build(Pass *p, const std::string &vs, const std::string &fs, std::string &err)
{
  GLuint prog = link(vs, fs, err);
  if (!prog) return false;
  if (p->prog) glDeleteProgram(p->prog);
  p->prog = prog;
  p->L.scene = glGetUniformLocation(prog, "scene");
  p->L.resolution = glGetUniformLocation(prog, "resolution");
  p->L.halfdeg = glGetUniformLocation(prog, "halfdeg");
  p->L.time = glGetUniformLocation(prog, "time");
  p->L.background = glGetUniformLocation(prog, "background");
  if (!vao) glGenVertexArrays(1, &vao);
  p->dirty = true;
  return true;
}

bool ensure_target(Pass *p, int w, int h)
{
  if (p->tex && p->tex_w == w && p->tex_h == h) return true;
  if (!p->tex) glGenTextures(1, &p->tex);
  if (!p->fbo) glGenFramebuffers(1, &p->fbo);
  GLint o_tex;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &o_tex);
  glBindTexture(GL_TEXTURE_2D, p->tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, o_tex);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p->fbo);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->tex, 0);
  GLenum st = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
  p->scene_valid = false;
  if (st != GL_FRAMEBUFFER_COMPLETE) {
    log_message("error", "shaderpass %s: scene target incomplete (0x%x)", p->name.c_str(), st);
    p->tex_w = p->tex_h = 0;
    return false;
  }
  p->tex_w = w; p->tex_h = h;
  return true;
}

void draw(Pass *p, const stim_postpass_frame_t *f)
{
  GLSave save;
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, f->drawfb);
  glViewport(0, 0, f->w, f->h);
  glUseProgram(p->prog);
  glBindTexture(GL_TEXTURE_2D, p->tex);
  if (p->L.scene >= 0) glUniform1i(p->L.scene, 0);
  if (p->L.resolution >= 0) glUniform2f(p->L.resolution, (float) f->w, (float) f->h);
  if (p->L.halfdeg >= 0) glUniform2f(p->L.halfdeg, f->halfx, f->halfy);
  /* the stim clock, so a replay render draws the noise the rig's frame
     had; wrapped each hour, to keep a float's precision under a
     millisecond however long stim2 has run */
  if (p->L.time >= 0) glUniform1f(p->L.time, (float) fmod(stimClockSeconds(), 3600.0));
  if (p->L.background >= 0) glUniform4fv(p->L.background, 1, getBackgroundColor());
  apply_uniforms(p->prog, p->uniforms);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  p->dirty = false;
}

/* ---- the pass, as stim2 calls it (postpass.h) --------------------------- */

int pass_wanted(void *d) { Pass *p = (Pass *) d; return p->on && p->prog; }

int pass_stage(void *d, const stim_postpass_frame_t *f)
{
  Pass *p = (Pass *) d;
  if (!p->on || !p->prog || f->w <= 0 || f->h <= 0) return 0;
  if (!ensure_target(p, f->w, f->h)) return 0;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, f->drawfb);
  glReadBuffer(f->drawnbuf);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p->fbo);
  glBlitFramebuffer(0, 0, f->w, f->h, 0, 0, f->w, f->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  p->scene_valid = true;
  draw(p, f);
  glBindFramebuffer(GL_FRAMEBUFFER, f->drawfb);
  glReadBuffer(f->drawnbuf);
  p->stages++;
  return 1;
}

int pass_composite(void *d, const stim_postpass_frame_t *f)
{
  Pass *p = (Pass *) d;
  if (!p->on || !p->prog || !p->scene_valid || f->w != p->tex_w || f->h != p->tex_h) return 0;
  draw(p, f);
  glBindFramebuffer(GL_FRAMEBUFFER, f->drawfb);
  p->composites++;
  return 1;
}

/* a static display: repaint when a uniform changed, and every refresh for
   a pass that animates */
int pass_repaint_due(void *d, double, double *wait_s)
{
  Pass *p = (Pass *) d;
  *wait_s = -1.0;
  return p->on && p->prog && (p->dirty || p->animated());
}

void destroy(Pass *p)
{
  bool was_on = p->on && p->prog;
  stimPostpassUnregister(p->regname.c_str());
  if (p->prog) glDeleteProgram(p->prog);
  if (p->tex) glDeleteTextures(1, &p->tex);
  if (p->fbo) glDeleteFramebuffers(1, &p->fbo);
  passes.erase(p->name);
  delete p;
  if (was_on) redraw();
}

/* `postpass reset`: a pass does not outlive the script that made it */
void pass_reset(void *d, Tcl_Interp *) { destroy((Pass *) d); }

/* ---- the Tcl command ---------------------------------------------------- */

Pass *find(Tcl_Interp *interp, Tcl_Obj *name)
{
  auto it = passes.find(Tcl_GetString(name));
  if (it == passes.end()) {
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("shaderpass: no pass \"%s\"", Tcl_GetString(name)));
    return nullptr;
  }
  return it->second;
}

Tcl_Obj *status_dict(Pass *p)
{
  Tcl_Obj *d = Tcl_NewDictObj();
  auto put = [&](const char *k, Tcl_Obj *v) { Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj(k, -1), v); };
  put("name", Tcl_NewStringObj(p->name.c_str(), -1));
  put("on", Tcl_NewIntObj(p->on ? 1 : 0));
  put("priority", Tcl_NewDoubleObj(p->pp.priority));
  put("file", Tcl_NewStringObj(p->file.c_str(), -1));
  put("animate", p->animate == ANIM_AUTO ? Tcl_NewStringObj("auto", -1) : Tcl_NewIntObj(p->animate));
  put("animated", Tcl_NewIntObj(p->animated() ? 1 : 0));
  Tcl_Obj *u = Tcl_NewDictObj();
  for (auto &kv : p->uniforms) {
    Tcl_Obj *l = Tcl_NewListObj(0, nullptr);
    for (float f : kv.second) Tcl_ListObjAppendElement(nullptr, l, Tcl_NewDoubleObj(f));
    Tcl_DictObjPut(nullptr, u, Tcl_NewStringObj(kv.first.c_str(), -1), l);
  }
  put("uniforms", u);
  put("stages", Tcl_NewWideIntObj(p->stages));
  put("repaints", Tcl_NewWideIntObj(p->composites));
  return d;
}

int parse_animate(Tcl_Interp *interp, Tcl_Obj *o, int *out)
{
  if (std::string(Tcl_GetString(o)) == "auto") { *out = ANIM_AUTO; return TCL_OK; }
  int b;
  if (Tcl_GetBooleanFromObj(nullptr, o, &b) != TCL_OK) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("shaderpass: -animate is auto, 0 or 1", -1));
    return TCL_ERROR;
  }
  *out = b ? ANIM_ON : ANIM_OFF;
  return TCL_OK;
}

const char *USAGE =
  "shaderpass ?names? | create name ?-file path? ?-fragment source? ?-priority p? ?-animate auto|0|1? | "
  "on name | off name | uniform name uniform ?v ...? | animate name ?auto|0|1? | "
  "status name | delete name | reset";

/* create NAME ?-file PATH? ?-fragment SRC? ?-priority P? ?-animate A?
   A pass of that name is replaced (off), unless the new shader fails to
   build: then the error, and the old one stays as it was. */
int create(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  if (objc < 3 || (objc - 3) % 2) {
    Tcl_WrongNumArgs(interp, 2, objv, "name ?-file path? ?-fragment source? ?-priority p? ?-animate auto|0|1?");
    return TCL_ERROR;
  }
  std::string name = Tcl_GetString(objv[2]);
  if (name.empty()) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("shaderpass: a pass needs a name", -1));
    return TCL_ERROR;
  }
  double priority = 200.0;
  int animate = ANIM_AUTO;
  std::string file, err;
  ShaderText t;
  bool have = false;
  for (int i = 3; i < objc; i += 2) {
    std::string opt = Tcl_GetString(objv[i]);
    Tcl_Obj *val = objv[i + 1];
    if (opt == "-file" || opt == "-fragment") {
      if (have) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("shaderpass: one of -file and -fragment", -1));
        return TCL_ERROR;
      }
      have = true;
      if (opt == "-file") {
        file = Tcl_GetString(val);
        if (!load_shader(file, t, err)) {
          Tcl_SetObjResult(interp, Tcl_ObjPrintf("shaderpass: %s", err.c_str()));
          return TCL_ERROR;
        }
      }
      else {
        std::istringstream in(Tcl_GetString(val));
        parse_shader(in, t);
        if (blank(t.section("Fragment"))) {
          Tcl_SetObjResult(interp, Tcl_NewStringObj("shaderpass: -fragment: no fragment shader", -1));
          return TCL_ERROR;
        }
      }
    }
    else if (opt == "-priority") {
      if (Tcl_GetDoubleFromObj(interp, val, &priority) != TCL_OK) return TCL_ERROR;
    }
    else if (opt == "-animate") {
      if (parse_animate(interp, val, &animate) != TCL_OK) return TCL_ERROR;
    }
    else {
      Tcl_SetObjResult(interp, Tcl_ObjPrintf("shaderpass create: unknown option \"%s\" (-file -fragment -priority -animate)", opt.c_str()));
      return TCL_ERROR;
    }
  }

  Pass *p = new Pass;
  std::string vs = blank(t.section("Vertex")) ? VS_BODY : t.section("Vertex");
  std::string fs = have ? t.section("Fragment") : FS_COPY;
  if (!build(p, vs, fs, err)) {
    delete p;
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("shaderpass: shader: %s", err.c_str()));
    return TCL_ERROR;
  }
  auto old = passes.find(name);
  if (old != passes.end()) destroy(old->second);

  p->name = name;
  p->regname = "shader/" + name;
  p->file = file;
  p->animate = animate;
  p->uniforms = t.uniforms;
  memset(&p->pp, 0, sizeof(p->pp));
  p->pp.name = p->regname.c_str();
  p->pp.priority = (float) priority;
  p->pp.data = p;
  p->pp.wanted = pass_wanted;
  p->pp.stage = pass_stage;
  p->pp.composite = pass_composite;
  p->pp.repaint_due = pass_repaint_due;
  p->pp.reset = pass_reset;
  passes[name] = p;
  stimPostpassRegister(&p->pp);
  Tcl_SetObjResult(interp, Tcl_NewStringObj(name.c_str(), -1));
  return TCL_OK;
}

int shaderpassCmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  std::string sub = objc > 1 ? Tcl_GetString(objv[1]) : "names";

  if (sub == "names" && objc <= 2) {
    Tcl_Obj *l = Tcl_NewListObj(0, nullptr);
    for (auto &kv : passes) Tcl_ListObjAppendElement(nullptr, l, Tcl_NewStringObj(kv.first.c_str(), -1));
    Tcl_SetObjResult(interp, l);
    return TCL_OK;
  }
  if (sub == "create") return create(interp, objc, objv);

  if (sub == "reset" && objc == 2) {
    while (!passes.empty()) destroy(passes.begin()->second);
    return TCL_OK;
  }

  if (sub == "on" || sub == "off" || sub == "status" || sub == "delete") {
    if (objc != 3) { Tcl_WrongNumArgs(interp, 2, objv, "name"); return TCL_ERROR; }
    Pass *p = find(interp, objv[2]);
    if (!p) return TCL_ERROR;
    if (sub == "status") { Tcl_SetObjResult(interp, status_dict(p)); return TCL_OK; }
    if (sub == "delete") { destroy(p); return TCL_OK; }
    bool want = sub == "on";
    if (p->on != want) {
      p->on = want;
      p->dirty = true;
      if (!want) p->scene_valid = false;
      redraw();
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(want ? 1 : 0));
    return TCL_OK;
  }

  if (sub == "animate") {
    if (objc != 3 && objc != 4) { Tcl_WrongNumArgs(interp, 2, objv, "name ?auto|0|1?"); return TCL_ERROR; }
    Pass *p = find(interp, objv[2]);
    if (!p) return TCL_ERROR;
    if (objc == 4 && parse_animate(interp, objv[3], &p->animate) != TCL_OK) return TCL_ERROR;
    Tcl_SetObjResult(interp, Tcl_NewIntObj(p->animated() ? 1 : 0));
    return TCL_OK;
  }

  if (sub == "uniform") {
    if (objc < 4) { Tcl_WrongNumArgs(interp, 2, objv, "name uniform ?v1 ?v2 v3 v4??"); return TCL_ERROR; }
    Pass *p = find(interp, objv[2]);
    if (!p) return TCL_ERROR;
    std::string uname = Tcl_GetString(objv[3]);
    if (objc == 4) {
      Tcl_Obj *l = Tcl_NewListObj(0, nullptr);
      auto it = p->uniforms.find(uname);
      if (it != p->uniforms.end())
        for (float f : it->second) Tcl_ListObjAppendElement(nullptr, l, Tcl_NewDoubleObj(f));
      Tcl_SetObjResult(interp, l);
      return TCL_OK;
    }
    /* values as separate words or one list */
    std::vector<float> vals;
    for (int i = 4; i < objc; i++) {
      Tcl_Size lc; Tcl_Obj **le;
      if (Tcl_ListObjGetElements(interp, objv[i], &lc, &le) != TCL_OK) return TCL_ERROR;
      for (int k = 0; k < lc; k++) {
        double d;
        if (Tcl_GetDoubleFromObj(interp, le[k], &d) != TCL_OK) return TCL_ERROR;
        vals.push_back((float) d);
      }
    }
    if (vals.empty() || vals.size() > 4) {
      Tcl_SetObjResult(interp, Tcl_NewStringObj("shaderpass uniform: 1 to 4 values", -1));
      return TCL_ERROR;
    }
    p->uniforms[uname] = vals;
    p->dirty = true;
    return TCL_OK;
  }

  Tcl_SetObjResult(interp, Tcl_ObjPrintf("usage: %s", USAGE));
  return TCL_ERROR;
}

} /* namespace */

#ifdef _WIN32
extern "C" __declspec(dllexport) int Shaderpass_Init(Tcl_Interp *interp)
#else
extern "C" int Shaderpass_Init(Tcl_Interp *interp)
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
  Tcl_CreateObjCommand(interp, "shaderpass", shaderpassCmd, nullptr, nullptr);
  return TCL_OK;
}
