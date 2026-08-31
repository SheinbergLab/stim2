/*
 * texfilter.h
 *   Texture filter naming, shared by the modules that expose a filter
 *   choice to Tcl (shaderimage.c, svg.cpp).  Header-only so it needs no
 *   build-system entry and works from both C and C++.
 *
 * WHY THIS EXISTS
 *   A filter is carried around as its GL *minification* filter, and the
 *   magnification filter and mip-chain requirement are DERIVED from it
 *   (texMagFilterFor / texNeedsMipmap) rather than stored beside it.
 *   GL_TEXTURE_MAG_FILTER accepts only GL_NEAREST or GL_LINEAR -- handing it
 *   a *_MIPMAP_* enum is GL_INVALID_ENUM -- so a single "filter" field
 *   assigned to both parameters cannot be extended with mipmap enums.
 *   Deriving makes that mistake unrepresentable at every call site.
 *
 *   "nearest" and "linear" are the historical names and behave exactly as
 *   they always have: MIN == MAG and no mip chain.  Mipmapping is opt-in by
 *   name, and only ever affects minification.
 *
 * AUTHOR
 *   DLS
 */

#ifndef STIM_TEXFILTER_H
#define STIM_TEXFILTER_H

#include <glad/glad.h>

/*
 * MSVC only guarantees C-mode `inline` under /std:c11 or later, and stimdlls
 * sets no CMAKE_C_STANDARD, so .c files compile at the compiler default.
 * __inline is MSVC's own C spelling and has always been accepted there.
 * (Plain `static` would work everywhere too, but warns -Wunused-function in
 * every translation unit that doesn't call all of these.)
 */
#if defined(_MSC_VER) && !defined(__cplusplus)
#define TEXFILTER_INLINE static __inline
#else
#define TEXFILTER_INLINE static inline
#endif

/* case-insensitive compare, no strcasecmp/_stricmp portability dance */
TEXFILTER_INLINE int texFilterNameEq(const char *a, const char *b)
{
  for (; *a && *b; a++, b++) {
    int ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
    if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
    if (ca != cb) return 0;
  }
  return *a == *b;
}

/*
 * Map a filter name onto a GL minification filter.
 * Returns -1 for an unrecognized name, so callers keep their own error
 * behavior (some sites report it, some have always ignored it).
 */
TEXFILTER_INLINE int texParseFilterName(const char *name)
{
  if (texFilterNameEq(name, "nearest"))        return GL_NEAREST;
  if (texFilterNameEq(name, "linear"))         return GL_LINEAR;
  /* trilinear: box-filtered mip chain, smoothly blended between levels */
  if (texFilterNameEq(name, "mipmap") ||
      texFilterNameEq(name, "linear_mipmap"))  return GL_LINEAR_MIPMAP_LINEAR;
  /* mipmapped minification but blocky magnification, for pixel art */
  if (texFilterNameEq(name, "nearest_mipmap")) return GL_NEAREST_MIPMAP_LINEAR;
  return -1;
}

/* Name for a minification filter, for round-tripping back to Tcl. */
TEXFILTER_INLINE const char *texFilterName(int minfilter)
{
  switch (minfilter) {
  case GL_NEAREST:                return "nearest";
  case GL_LINEAR:                 return "linear";
  case GL_LINEAR_MIPMAP_LINEAR:   return "mipmap";
  case GL_NEAREST_MIPMAP_LINEAR:  return "nearest_mipmap";
  default:                        return "unknown";
  }
}

/* The magnification filter that goes with a given minification filter. */
TEXFILTER_INLINE int texMagFilterFor(int minfilter)
{
  switch (minfilter) {
  case GL_NEAREST:
  case GL_NEAREST_MIPMAP_NEAREST:
  case GL_NEAREST_MIPMAP_LINEAR:
    return GL_NEAREST;
  default:
    return GL_LINEAR;
  }
}

TEXFILTER_INLINE int texNeedsMipmap(int minfilter)
{
  switch (minfilter) {
  case GL_NEAREST_MIPMAP_NEAREST:
  case GL_NEAREST_MIPMAP_LINEAR:
  case GL_LINEAR_MIPMAP_NEAREST:
  case GL_LINEAR_MIPMAP_LINEAR:
    return 1;
  default:
    return 0;
  }
}

/*
 * Number of levels in a full mip chain for a w x h image (levels 0..n-1).
 * Used to pin GL_TEXTURE_MAX_LEVEL to what glGenerateMipmap produced: a
 * texture whose min filter samples mip levels is INCOMPLETE, and samples as
 * undefined (typically black), unless the chain reaches MAX_LEVEL.
 */
TEXFILTER_INLINE int texMipLevelCount(int w, int h)
{
  int levels = 1;
  int m = (w > h) ? w : h;
  while (m > 1) { m >>= 1; levels++; }
  return levels;
}

#endif /* STIM_TEXFILTER_H */
