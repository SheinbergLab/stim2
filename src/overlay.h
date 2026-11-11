/*
 * overlay.h -- annotation layer drawn over stim2's scene
 *
 * Named items -- sets of discs or a polyline in degrees (a gaze marker, a
 * trail of recent samples, touch points), each with an optional text
 * label -- drawn after the current group's scene and its
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
/* Item `name`'s style (created if absent): line 1 draws its points as a
   polyline `size` degrees wide instead of discs, each pixel once (it does
   not darken where it crosses itself); fade 1 takes the line's alpha from
   full at the first point to 0 at the last (a trail, newest first).  -1
   leaves either as it is; new items are discs, no fade. */
void overlaySetStyle(const char *name, int line, int fade);
int  overlayGetStyle(const char *name, int *line, int *fade);

/* Item `name`'s text label (created if absent).  Each of text, pos (x, y
   deg), anchor ("c", "n", "s", "e", "w", "ne", "nw", "se", "sw": the part
   of the text block at pos; it also justifies the lines), bg (a box behind
   the text, drawn when its alpha > 0), and font (a file in stim2's font
   directory, or a full path; "" the default, NotoSans-Regular.ttf) is
   applied only when not NULL.  The text is drawn in the item's colour,
   `size` degrees a line, over the item's discs, when it is not empty.
   UTF-8; \n breaks lines.  Characters: Latin-1, Greek, common
   punctuation, arrows and math signs, as far as the font has them (the
   default has no arrows or <= >=: they show as its empty box); others
   show as '?'. */
void overlaySetText(const char *name, const char *text, const float *pos,
                    const char *anchor, const float *bg, const char *font);
int  overlayGetText(const char *name, const char **text, float pos[2],
                    const char **anchor, float bg[4], const char **font);
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
