/* WebGL 2 stand-in for stim2's desktop glad loader.
   The graphics modules call gl* directly. Emscripten's GLES3 headers
   are those functions, so there is no dlopen of libGLESv2. */
#ifndef GLAD_GLAD_H
#define GLAD_GLAD_H

#include <GLES3/gl3.h>

/* Desktop tokens the modules still name. WebGL has no quads and treats
   point size as a shader output, so these only let the calls compile. */
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif

#ifdef __cplusplus
extern "C" {
#endif

int gladLoadGL(void);

#ifdef __cplusplus
}
#endif

#endif
