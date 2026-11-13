/*
 * gazewin.h -- gaze-contingent pass over stim2's finished scene
 *
 * When on, every frame stim2 shows is the scene the stim drew, passed
 * through one full-screen fragment shader that knows where the eye is: a
 * window (only the region around gaze shows), a scotoma (that region is
 * hidden), or anything a custom shader does with the scene and the gaze.
 * It does not depend on the object list, the group list or the stim's
 * script, so it works over any display and survives resetObjList and
 * glistInit.  See docs/gazewin.md.
 *
 *   - The gaze is a dserv datapoint (`gazewin source`, normally
 *     eyetracking/position: two floats, degrees, origin at screen centre).
 *     Live, processDSCommands hands each sample straight to C; a replay
 *     hands the same line over with `gazewin datapoint` (stim::inject does
 *     this).  Or a fixed point (`gazewin point x y`) for tests.
 *   - Each drawn frame: the scene is copied into a texture (one blit) and
 *     the pass draws over the whole framebuffer, before the overlay, the
 *     grabs and the sink -- so a grab, a movie and a live mirror's frames
 *     are what the subject saw.
 *   - A static display has no frames to carry a new sample, so the main
 *     loop repaints: the pass again over the saved scene with the newest
 *     gaze, timed late in the refresh like stimLateDraw.  A repaint is a
 *     real frame (SwapCount, stim/frame), so a replay places the samples
 *     that caused it.
 *
 * Off (the default), the only cost is a flag test per frame and per
 * forwarded datapoint; no GL object exists until `gazewin on`.
 *
 * Render thread only (Tcl and the draw run there); no locks.
 */
#ifndef GAZEWIN_H
#define GAZEWIN_H

#include <tcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The flags the hot paths test.  gazewinWanted: on, with a program.
   gazewinSourceSet: a source datapoint is named. */
int  gazewinWanted(void);
int  gazewinSourceSet(void);

/* stim2.cpp: the stim clock (stimClockNow).  A sample's age, for -hold and
   the shader's age_ms, is on this clock: the real one live, the stepped
   one in a replay or a mirror, so a render that runs faster or slower than
   real time ages samples as the rig did.  The latency record and the
   repaint timing keep glfwGetTime(). */
double gazewinStimClock(void);

/* processDSCommands: if `name` is the source, take the sample from the
   line as received ("name type ts len {value}").  now_s: glfwGetTime(). */
void gazewinMaybeSample(const char *name, const char *line, double now_s);

/* After the scene (and its post scripts) is finished in the bound draw
   framebuffer drawfb (read buffer drawnbuf): copy it aside and draw the
   pass over it.  Returns 1 if the pass was drawn. */
int  gazewinStage(int w, int h, float halfx, float halfy,
                  unsigned drawfb, unsigned drawnbuf, double now_s);

/* The pass again over the scene saved by the last stage, into drawfb.
   0 if there is no saved scene of this size (the caller redraws). */
int  gazewinComposite(int w, int h, float halfx, float halfy,
                      unsigned drawfb, double now_s);

/* Static display: 1 if what the pass would show has changed since it was
   last drawn (a new sample, gaze lost or regained, a setting).  Otherwise
   0, with *wait_s the seconds until it could change by itself (the
   sample going stale), or -1 for "only an input can change it". */
int  gazewinRepaintDue(double now_s, double *wait_s);

/* The repaint's draw-late budget (ms): >= 0 as set by `gazewin set
   -late`, or -1 to follow stimLateDraw. */
double gazewinLateMs(void);

/* After the swap that showed a pass: the flip in the glfwGetTime() domain
   and on the wall clock (-1 unknown), for the latency statistics. */
int  gazewinFlipPending(void);
void gazewinNoteFlip(double flip_s, long long flip_wall_us);

/* the `gazewin` Tcl command */
int  Gazewin_TclInit(Tcl_Interp *interp);

#ifdef __cplusplus
}
#endif

#endif /* GAZEWIN_H */
