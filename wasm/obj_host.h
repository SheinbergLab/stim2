#ifndef STIM_OBJ_HOST_H
#define STIM_OBJ_HOST_H

#include <tcl.h>

/* Create the object list and register scaleObj, translateObj, the
   display-group commands, and metagroup. shape stays in shape_export. */
int stimObjectsInit(Tcl_Interp *interp);

/* svgOpacity for the software shape objects. Svg_Init registers its own
   command on the GL svg objects; this puts ours back afterwards. */
void stimRegisterOpacity(Tcl_Interp *interp);

/* Turn the raster just painted into a graphics object and set the
   interpreter result to its id, the way native shape does. */
int shapeMakeObject(Tcl_Interp *interp, const unsigned char *bgra,
                    int side, int stride, float span);

#endif
