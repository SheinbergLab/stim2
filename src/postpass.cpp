/*
 * postpass.cpp -- the chain of full-screen passes over stim2's finished
 * scene, and the datapoint taps (see postpass.h and docs/postpass.md)
 *
 * State is plain statics: the render thread is the only one that reads or
 * writes it.
 */
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

#include <GLFW/glfw3.h>

#include "postpass.h"

void log_message(const char *level, const char *fmt, ...);   /* stim2.cpp, C++ */

namespace {

std::vector<const stim_postpass_t *> chain;   /* by priority, stable */

struct Tap {
  std::string name;
  stim_dp_tap_fn fn;
  void *data;
};
std::vector<Tap> taps;

/* a pass drew since the last flip was noted */
bool flip_pending = false;
std::vector<const stim_postpass_t *> drew;    /* the passes that did */

inline bool want(const stim_postpass_t *p) { return p->wanted(p->data) != 0; }

void note_drew(const stim_postpass_t *p)
{
  if (std::find(drew.begin(), drew.end(), p) == drew.end()) drew.push_back(p);
  flip_pending = true;
}

void forget(const stim_postpass_t *p)
{
  drew.erase(std::remove(drew.begin(), drew.end(), p), drew.end());
}

} /* namespace */

extern "C" {

/* ---- for modules ------------------------------------------------------- */

int stimPostpassRegister(const stim_postpass_t *pass)
{
  if (!pass || !pass->name || !pass->wanted || !pass->stage) return 0;
  stimPostpassUnregister(pass->name);
  auto at = std::upper_bound(chain.begin(), chain.end(), pass,
                             [](const stim_postpass_t *a, const stim_postpass_t *b) {
                               return a->priority < b->priority;
                             });
  chain.insert(at, pass);
  return 1;
}

void stimPostpassUnregister(const char *name)
{
  if (!name) return;
  for (auto it = chain.begin(); it != chain.end(); ) {
    if (strcmp((*it)->name, name) == 0) { forget(*it); it = chain.erase(it); }
    else ++it;
  }
}

void stimDatapointTapAdd(const char *name, stim_dp_tap_fn fn, void *data)
{
  if (!name || !*name || !fn) return;
  for (auto &t : taps)
    if (t.fn == fn && t.data == data && t.name == name) return;
  taps.push_back({ name, fn, data });
}

void stimDatapointTapRemove(const char *name, stim_dp_tap_fn fn, void *data)
{
  if (!name) return;
  taps.erase(std::remove_if(taps.begin(), taps.end(),
                            [&](const Tap &t) {
                              return t.fn == fn && t.data == data && t.name == name;
                            }),
             taps.end());
}

double stimRealSeconds(void) { return glfwGetTime(); }

void stimLogMessage(const char *level, const char *msg)
{
  log_message(level ? level : "info", "%s", msg ? msg : "");
}

/* ---- for stim2.cpp ----------------------------------------------------- */

int postpassWanted(void)
{
  for (auto p : chain) if (want(p)) return 1;
  return 0;
}

int postpassStage(const stim_postpass_frame_t *f)
{
  int any = 0;
  for (auto p : chain) {
    if (!want(p)) continue;
    if (p->stage(p->data, f)) { note_drew(p); any = 1; }
  }
  return any;
}

/* The first wanted pass draws again from the scene it kept; what the
   later ones kept is the earlier passes' old output, so they are staged
   again over the new one. */
int postpassComposite(const stim_postpass_frame_t *f)
{
  bool first = true;
  for (auto p : chain) {
    if (!want(p)) continue;
    if (first) {
      if (!p->composite || !p->composite(p->data, f)) return 0;
      first = false;
      note_drew(p);
    }
    else if (p->stage(p->data, f)) note_drew(p);
  }
  return first ? 0 : 1;
}

int postpassRepaintDue(double now_s, double *wait_s)
{
  *wait_s = -1.0;
  int due = 0;
  for (auto p : chain) {
    if (!want(p) || !p->repaint_due) continue;
    double w = -1.0;
    if (p->repaint_due(p->data, now_s, &w)) due = 1;
    else if (w >= 0.0 && (*wait_s < 0.0 || w < *wait_s)) *wait_s = w;
  }
  return due;
}

/* the largest budget any wanted pass asks for; -1 if none names one */
double postpassLateMs(void)
{
  double ms = -1.0;
  for (auto p : chain) {
    if (!want(p) || !p->late_ms) continue;
    double m = p->late_ms(p->data);
    if (m > ms) ms = m;
  }
  return ms;
}

int postpassFlipPending(void) { return flip_pending ? 1 : 0; }

void postpassNoteFlip(double flip_s, long long flip_wall_us)
{
  if (!flip_pending) return;
  flip_pending = false;
  /* a flip callback may unregister a pass: walk a copy */
  std::vector<const stim_postpass_t *> d;
  d.swap(drew);
  for (auto p : d)
    if (p->flip && std::find(chain.begin(), chain.end(), p) != chain.end())
      p->flip(p->data, flip_s, flip_wall_us);
}

int postpassTapCount(void) { return (int) taps.size(); }

void postpassTap(const char *name, const char *line, double now_s)
{
  for (size_t i = 0; i < taps.size(); i++)
    if (taps[i].name == name) taps[i].fn(taps[i].data, name, line, now_s);
}

} /* extern "C" */

/* ======================================================================== */
/* the Tcl command                                                          */

namespace {

/*   postpass ?list?   one dict per pass, in chain order: name priority on
     postpass reset    every pass off and back to its defaults            */
int postpassCmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  std::string sub = objc > 1 ? Tcl_GetString(objv[1]) : "list";

  if (sub == "list" && objc <= 2) {
    Tcl_Obj *l = Tcl_NewListObj(0, nullptr);
    for (auto p : chain) {
      Tcl_Obj *d = Tcl_NewDictObj();
      Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj("name", -1), Tcl_NewStringObj(p->name, -1));
      Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj("priority", -1), Tcl_NewDoubleObj(p->priority));
      Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj("on", -1), Tcl_NewIntObj(want(p) ? 1 : 0));
      Tcl_ListObjAppendElement(nullptr, l, d);
    }
    Tcl_SetObjResult(interp, l);
    return TCL_OK;
  }
  if (sub == "reset" && objc == 2) {
    /* a reset may run Tcl; the chain itself does not change under it */
    std::vector<const stim_postpass_t *> c = chain;
    for (auto p : c) if (p->reset) p->reset(p->data, interp);
    Tcl_ResetResult(interp);
    return TCL_OK;
  }
  Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: postpass ?list|reset?", -1));
  return TCL_ERROR;
}

} /* namespace */

extern "C" int Postpass_TclInit(Tcl_Interp *interp)
{
  Tcl_CreateObjCommand(interp, "postpass", postpassCmd, nullptr, nullptr);
  return TCL_OK;
}
