/*
 * postpass.h -- full-screen passes over stim2's finished scene
 *
 * A post pass is something a loadable module (a stimdll) draws over the
 * whole framebuffer after the stim's scene is finished: a gaze-contingent
 * window (stimdlls/src/gazewin.cpp), noise, a colour transform, a display
 * encoding.  stim2 itself knows nothing about what a pass draws.  It owns
 * only what a module cannot do from outside:
 *
 *   - where the passes run: after the scene and its post scripts, before
 *     the overlay, the grabs and the sink -- so a grab, a movie and a live
 *     mirror's frames are what the subject saw;
 *   - a static display: when a pass says what it would show has changed,
 *     the main loop repaints -- the passes again over the saved scene, no
 *     step, no frame scripts -- timed late in the refresh like
 *     stimLateDraw.  A repaint is a real frame (SwapCount, stim/frame);
 *   - frameRepaint (a mirror, a replay render) running the passes again;
 *   - the flip time of each frame a pass drew, for latency records;
 *   - a datapoint tap: a forwarded dserv datapoint handed to C as it
 *     arrives, without a Tcl binding;
 *   - `postpass reset`, which every script load runs (stim::prelude), so
 *     a pass one system turned on never carries into the next.
 *
 * Passes form a chain, ordered by priority (lower first).  Each draws over
 * the framebuffer as the one before left it, so a pass needs no knowledge
 * of the others.  By convention: 100 for a pass that selects or degrades
 * the scene (gazewin), higher for one that should apply to its result
 * (noise), 900 and up for a display encoding that must come last.
 *
 * With no pass wanted the cost is one test per frame and per forwarded
 * datapoint.  Render thread only (Tcl and the draw run there); no locks.
 */
#ifndef POSTPASS_H
#define POSTPASS_H

#include <tcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The frame a pass draws into: the bound draw framebuffer drawfb (read
   buffer drawnbuf), w x h pixels, half the screen in degrees, and the real
   clock (stimRealSeconds) at the draw. */
typedef struct stim_postpass_frame {
  int w, h;
  float halfx, halfy;
  unsigned drawfb, drawnbuf;
  double now_s;
} stim_postpass_frame_t;

/* A pass.  The struct must outlive its registration (make it static).
   wanted and stage are required; the rest may be NULL. */
typedef struct stim_postpass {
  const char *name;
  float priority;               /* lower runs first */
  void *data;                   /* handed back to every callback */

  /* 1 while the pass is on and able to draw.  Called on hot paths: a flag
     test. */
  int (*wanted)(void *data);

  /* The scene (and any earlier pass) is finished in f->drawfb: keep what
     is needed to draw again later (normally a copy of the framebuffer),
     and draw the pass over it.  Leave the GL state as found.  Returns 1 if
     it drew. */
  int (*stage)(void *data, const stim_postpass_frame_t *f);

  /* Draw again from what the last stage kept, into f->drawfb, with the
     pass's inputs as they are now.  0 if there is nothing kept for this
     size (stim2 then redraws the scene).  Only the first pass of a chain
     is composited; the later ones are staged again over its result.
     NULL: the scene is always redrawn. */
  int (*composite)(void *data, const stim_postpass_frame_t *f);

  /* Static display: 1 if what the pass would show has changed since it
     last drew (or always, for a pass that animates by itself).  Otherwise
     0, with *wait_s the seconds until it could change by itself, or -1 for
     "only an input can change it".  NULL: never. */
  int (*repaint_due)(void *data, double now_s, double *wait_s);

  /* The repaint's draw-late budget in ms, or -1 to follow stimLateDraw.
     NULL: -1. */
  double (*late_ms)(void *data);

  /* After the swap that showed a frame this pass drew: the flip on the
     real clock, and on the wall clock (-1 unknown). */
  void (*flip)(void *data, double flip_s, long long flip_wall_us);

  /* `postpass reset`: off, back to defaults. */
  void (*reset)(void *data, Tcl_Interp *interp);
} stim_postpass_t;

/* ---- for modules ------------------------------------------------------- */

/* Add a pass to the chain (a pass of the same name is replaced). */
int  stimPostpassRegister(const stim_postpass_t *pass);
void stimPostpassUnregister(const char *name);

/* A forwarded dserv datapoint named `name` goes to fn as it is received:
   line is "name type ts len {value}", now_s the real clock.  The Tcl side
   still sees it (::dsVals, a script's own binding).  The same (name, fn,
   data) is held once. */
typedef void (*stim_dp_tap_fn)(void *data, const char *name, const char *line,
                               double now_s);
void stimDatapointTapAdd(const char *name, stim_dp_tap_fn fn, void *data);
void stimDatapointTapRemove(const char *name, stim_dp_tap_fn fn, void *data);

/* The stim clock in seconds: the real one live, the stepped one in a
   replay or a mirror.  Age an input on this clock and a render that runs
   faster or slower than real time behaves as the rig did. */
double stimClockSeconds(void);

/* The real clock in seconds (glfwGetTime): latency records, repaint
   timing.  A module must not call GLFW itself: it would reach its own
   copy of the library, not stim2's. */
double stimRealSeconds(void);

/* stim2's log (stderr and the console): level is "error", "warn", ... */
void stimLogMessage(const char *level, const char *msg);

/* ---- for stim2.cpp ----------------------------------------------------- */

int  postpassWanted(void);              /* any pass wanted */
int  postpassStage(const stim_postpass_frame_t *f);      /* 1 if any drew */
int  postpassComposite(const stim_postpass_frame_t *f);  /* 0: redraw */
int  postpassRepaintDue(double now_s, double *wait_s);
double postpassLateMs(void);
int  postpassFlipPending(void);
void postpassNoteFlip(double flip_s, long long flip_wall_us);
int  postpassTapCount(void);
void postpassTap(const char *name, const char *line, double now_s);

/* the `postpass` Tcl command */
int  Postpass_TclInit(Tcl_Interp *interp);

#ifdef __cplusplus
}
#endif

#endif /* POSTPASS_H */
