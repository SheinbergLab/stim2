/*
 * NAME
 *   rawapi.h
 *
 * DESCRIPTION
 *  Functions for handling raw image file header
 *  
 * DETAILS 
 *  The raw image header is a 20 byte header with the following layout
 *
 *  BYTES 0-3:   MAGIC NUMBER [3 26 19 97]
 *  BYTES 4-7:   VERSION (float 1.0)
 *  BYTES 8-11:  WIDTH
 *  BYTES 12-15: HEIGHT
 *  BYTES 16-19: DEPTH
 *
 * AUTHOR
 *    DLS / APR-98
 */

#ifndef _RAWAPI_H_
#define _RAWAPI_H_

#ifdef __cplusplus
extern "C" {
#endif

#define RAW_VERSION (1.0)

#define RAW_FLAGS_FLIP 0x01

/* read header info and return 1 if header exists */
int raw_readHeader(char *filename, int *width, int *height, int *depth);

/* write out twenty byte header with supplied info to open fp */
int raw_writeHeader(int width, int height, int depth, FILE *fp);

/* get image dimensions from a raw file either from header or by guessing */
int raw_getImageDims(char *filename, int *width, int *height, int *d, 
		     int *hbytes);

/* does the raw file have a header? */
int raw_hasHeader(char *oldname);

/* make new file with a raw header using supplied w/h or guess if w = h = 0 */
int raw_addHeader(char *oldname, char *newname, int w, int h, int d);

/* output rawfile to postscript */
int raw_toPS(char *filename, FILE *fp, int flags);
int raw_bufToPS(unsigned char *buf, int w, int h, int d, FILE *fp, int flags);

#ifdef __cplusplus
}
#endif

#endif
