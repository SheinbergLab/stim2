/* image.c, spine.c, and world_atlas.c each embed stb_image. On the Pi
   those are separate shared libraries. One wasm image keeps image.c's
   copy global and makes the other two private to their own file. */
#ifndef STBIDEF
#define STBIDEF static
#endif
