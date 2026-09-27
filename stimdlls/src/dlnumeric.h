/*
 * dlnumeric.h -- read any numeric dlsh dynlist as floats
 *
 * Modules take vertices, texture coordinates, vectors, matrices and pixel
 * data as dynlists. dlsh lists hold char, short, long (32-bit) or float and,
 * since 2026, int64 and double. Each module used to switch over the subset
 * it knew -- polyverts took long and float, meshObj and the mat4_* family
 * float only -- each with its own error. These helpers read every numeric
 * type the same way and round to float once, which is what the GPU takes, so
 * `set x [dl_dlist ...]` works anywhere a coordinate list is accepted.
 *
 * Header-only (static inline): nothing is needed from libdlsh beyond the
 * DYN_LIST layout, so it behaves the same on Windows, where dlsh's own
 * converters are not exported.
 *
 * The 8-byte element codes are written as DF_LIST_ARRAY+1 / +2 rather than
 * DF_INT64 / DF_DOUBLE so a module still compiles against a df.h from before
 * they existed. They are file opcodes, appended to the enum precisely so no
 * value can ever move (see df.h).
 */

#ifndef STIM2_DLNUMERIC_H
#define STIM2_DLNUMERIC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <df.h>

/* MSVC's C compiler spells it __inline */
#if defined(_MSC_VER) && !defined(__cplusplus) && !defined(inline)
#define inline __inline
#endif

enum { DLN_INT64 = DF_LIST_ARRAY + 1, DLN_DOUBLE = DF_LIST_ARRAY + 2 };

/* char, short, long, float, int64 or double */
static inline int dlnIsNumeric(int datatype)
{
  switch (datatype) {
  case DF_CHAR: case DF_SHORT: case DF_LONG: case DF_FLOAT:
  case DLN_INT64: case DLN_DOUBLE:
    return 1;
  default:
    return 0;
  }
}

/* char, short, long or int64: whole numbers (object ids, counts) */
static inline int dlnIsInteger(int datatype)
{
  return datatype == DF_CHAR || datatype == DF_SHORT ||
    datatype == DF_LONG || datatype == DLN_INT64;
}

/* element i as a double; the caller has checked dlnIsNumeric. char is
   read as plain char, as dlsh's own arithmetic reads it. */
static inline double dlnGet(DYN_LIST *dl, int i)
{
  void *v = DYN_LIST_VALS(dl);
  switch (DYN_LIST_DATATYPE(dl)) {
  case DF_CHAR:    return ((char *) v)[i];
  case DF_SHORT:   return ((short *) v)[i];
  case DF_LONG:    return ((int *) v)[i];    /* DF_LONG is 32-bit */
  case DF_FLOAT:   return ((float *) v)[i];
  case DLN_INT64:  return (double) ((int64_t *) v)[i];
  case DLN_DOUBLE: return ((double *) v)[i];
  default:         return 0.0;
  }
}

/* Fill out[0..n-1] with the list as floats. Returns 0 (and writes nothing)
   if the list is not numeric. */
static inline int dlnToFloats(DYN_LIST *dl, float *out)
{
  int i, n = DYN_LIST_N(dl);
  if (!dlnIsNumeric(DYN_LIST_DATATYPE(dl))) return 0;
  if (DYN_LIST_DATATYPE(dl) == DF_FLOAT) {
    if (n) memcpy(out, DYN_LIST_VALS(dl), n*sizeof(float));
    return 1;
  }
  for (i = 0; i < n; i++) out[i] = (float) dlnGet(dl, i);
  return 1;
}

/* A malloc'd float copy of the list (caller frees), or NULL if the list is
   not numeric or the allocation failed. An empty list gives a valid
   one-element allocation, so NULL always means an error. */
static inline float *dlnFloats(DYN_LIST *dl)
{
  float *out;
  if (!dlnIsNumeric(DYN_LIST_DATATYPE(dl))) return NULL;
  out = (float *) malloc((DYN_LIST_N(dl) ? DYN_LIST_N(dl) : 1)*sizeof(float));
  if (!out) return NULL;
  dlnToFloats(dl, out);
  return out;
}

#endif /* STIM2_DLNUMERIC_H */
