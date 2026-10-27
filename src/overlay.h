/*
 * overlay.h -- annotation layer drawn over stim2's scene
 *
 * Named items -- sets of discs in degrees (a gaze marker, a trail of recent
 * samples, touch points) -- drawn after the current group's scene and its
 * post scripts, before the frame is grabbed, sunk or presented.  The layer
 * is independent of the object list and the group list, so a script that
 * resets its objects every trial (resetObjList, glistInit) does not touch
 * it, and neither does stim::prelude.  It is empty unless something adds to
 * it; an empty layer costs one size check per frame.
 *
 * Coordinates are degrees, mapped like the scene's default projection:
 * x in [-HalfScreenDegreeX, +], y in [-HalfScreenDegreeY, +].
 *
 * Render thread only (Tcl runs there).  Tcl: `overlay` in tclproc.c.
 */
#ifndef OVERLAY_H
#define OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Create or update item `name`.  Each of pts (n x,y pairs), size (disc
   diameter, deg), rgba and show is applied only when given: pts NULL with
   n -1, size < 0, rgba NULL, show -1 leave that property as it is.  New
   items default to no points, 0.5 deg, opaque white, shown. */
void overlaySet(const char *name, const float *pts, int n, float size,
                const float *rgba, int show);
int  overlayDelete(const char *name);          /* 1 if it existed */
void overlayClear(void);
int  overlayCount(void);                       /* items */
/* name i (0-based), or NULL past the end */
const char *overlayName(int i);
/* properties of `name`: 0 if absent */
int  overlayGet(const char *name, int *npts, float *size, float rgba[4],
                int *show);

/* Draw every shown item into the bound framebuffer, w x h pixels, with the
   degree mapping halfx, halfy.  Saves and restores the GL state it
   touches.  Returns the number of discs drawn. */
int  overlayDraw(int w, int h, float halfx, float halfy);

#ifdef __cplusplus
}
#endif

#endif
