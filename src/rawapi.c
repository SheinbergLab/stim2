/*
 * NAME
 *   rawapi.c
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


#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>             /* low-level file stuff                 */
#include <sys/stat.h>
#include <math.h>

#include "rawapi.h"

static float raw_version = RAW_VERSION;
static char magic_numbers[] = { 3, 26, 19, 97 };

enum { IMG_MASK = 0x01 };

static long fliplong(long oldl)
{
  long newl;
  char *old, *new;
  old = (char *) &oldl;
  new = (char *) &newl;
  new[0] = old[3];  new[1] = old[2];  new[2] = old[1];  new[3] = old[0];
  return(newl);
}

static float flipfloat(float oldl)
{
  float newl;
  char *old, *new;
  old = (char *) &oldl;
  new = (char *) &newl;
  new[0] = old[3];  new[1] = old[2];  new[2] = old[1];  new[3] = old[0];
  return(newl);
}

int raw_hasHeader(char *filename)
{
  char header[20];
  float version;
  int status;
  FILE *fp = fopen(filename, "rb");
  status = fread(header, 20, 1, fp);
  fclose(fp);
  if (status == 1) {
    /* Check for magic number: 03 26 19 97 */
    if (header[0] == 3 && header[1] == 26 && 
	header[2] == 19 && header[3] == 97) {
      
      version = *((float *) &header[4]);
      if (version == raw_version) return 1;
      else if (flipfloat(version) == raw_version) return 1;
    }
  }
  return 0;
}

int raw_readHeader(char *filename, int *width, int *height, int *depth)
{
  char header[20];
  float version;
  int status;
  FILE *fp = fopen(filename, "rb");
  status = fread(header, 20, 1, fp);
  fclose(fp);
  if (status == 1) {
    /* Check for magic number: 03 26 19 97 */
    if (header[0] == 3 && header[1] == 26 && 
	header[2] == 19 && header[3] == 97) {
      
      version = *((float *) &header[4]);
      if (version == raw_version) {
	*width =  *((int *) &header[8]);
	*height = *((int *) &header[12]);
	*depth =  *((int *) &header[16]);
	return 1;
      }
      /* Other byte format */
      else if (flipfloat(version) == raw_version) {
	*width =  fliplong(*((int *) &header[8]));
	*height = fliplong(*((int *) &header[12]));
	*depth =  fliplong(*((int *) &header[16]));
	return 1;
      }
    }
  }
  return 0;
}

int raw_writeHeader(int width, int height, int depth, FILE *fp)
{
  if (fwrite(magic_numbers, 4, 1, fp) != 1) return 0;
  if (fwrite(&raw_version, 4, 1, fp) != 1) return 0;
  if (fwrite(&width, 4, 1, fp) != 1) return 0;
  if (fwrite(&height, 4, 1, fp) != 1) return 0;
  if (fwrite(&depth, 4, 1, fp) != 1) return 0;
  return 1;
}

int raw_getImageDims(char *filename, int *width, 
		     int *height, int *d, int *hbytes)
{
  struct stat statbuf;
  int status;
  int size, n;

  if (hbytes) *hbytes = 0;
  status = stat(filename, &statbuf);
  if (status < 0) return(0);
  size = statbuf.st_size;

  n = (*width) * (*height) * (*d);
  if (n == size) {
    *hbytes = 0;
    return 1;
  }
  else if (n+20 == size) {
    *hbytes = 20;
    return 1;
  }

  /*
   * If any of the dims are to be overridden, just see if the header
   * exists, so it can be skipped
   */
  if (*width || *height || *d) {
    if (raw_hasHeader(filename)) {
      *hbytes = 20;
      size -= 20;			/* make size = only data bytes */
    }
  }

  else if (raw_readHeader(filename, width, height, d)) {
    if (hbytes) *hbytes = 20;
    return 1;
  }

  if (*width && *height) {
    int wh = (*width)*(*height);
    if ((wh * 4) == size) *d = 4;
    else if ((wh * 3) == size) *d = 3;
    else if (wh == size) *d = 1;
    else return 0;
    
    return 1;
  }
  else if (*width && *d) {
    int wd = (*width)*(*d);
    if (size % wd == 0) {
      *height = size / wd;
      return 1;
    } 
    else return 0;
  }
  else if (*width) {
    if (!(size % *width)) {
      int r = size/(*width);	/* remainder bytes  */      
      if (!(r%4)) {		/* assume it's RGBA */
	*height = r/4;
	*d = 4;
      }
      else if (!(r%3)) {	/* assume it's RGB  */
	*height = r/3;
	*d = 3;
      }
      else {			/* assume it's gray */
	*height = r;
	*d = 1;
      }
      return 1;
    }
    else return 0;
  }

  else if (*height) {
    if (!(size % *height)) {
      int r = size/(*height);	/* remainder bytes  */
      
      if (!(r%4)) {		/* assume it's RGBA */
	*width = r/4;
	*d = 4;
      }
      else if (!(r%3)) {	/* assume it's RGB  */
	*width = r/3;
	*d = 3;
      }
      else {			/* assume it's gray */
	*width = r;
	*d = 1;
      }
      return 1;
    }
    else return 0;
  }

  switch (size) {
    case 256*256:
      *width = 256;
      *height = 256;
      *d = 1;
      break;
    case 256*256*3:
      *width = 256;
      *height = 256;
      *d = 3;
      break;
    case 256*256*4:
      *width = 256;
      *height = 256;
      *d = 4;
      break;
    case 64*64:
      *width = 64;
      *height = 64;
      *d = 1;
      break;
    case 64*64*3:
      *width = 64;
      *height = 64;
      *d = 3;
      break;
    case 64*64*4:
      *width = 64;
      *height = 64;
      *d = 4;
      break;
    case 128*128*3:
      *width = 128;
      *height = 128;
      *d = 3;
      break;
    case 512*512*3:
      *width = 512;
      *height = 512;
      *d = 3;
      break;
    case 512*512*4:
      *width = 512;
      *height = 512;
      *d = 4;
      break;
    case 1024*1024*3:
      *width = 1024;
      *height = 1024;
      *d = 3;
    case 1024*1024*4:
      *width = 1024;
      *height = 1024;
      *d = 4;
      break;
    default:
      return(0);
      break;
  }
  return(1);
}

int raw_addHeader(char *oldname, char *newname, int w, int h, int d)
{
  FILE *infp, *outfp;
  char buf[4096];
  int hbytes = 0, n;

  if (!raw_getImageDims(oldname, &w, &h, &d, &hbytes)) return 0;

  infp = fopen(oldname, "rb");
  if (hbytes) fseek(infp, hbytes, SEEK_SET);

  outfp = fopen(newname, "wb");
  raw_writeHeader(w, h, d, outfp);
  
  while ((n = fread(buf, 1, sizeof(buf), infp)) != 0) {
    if (fwrite(buf, n, 1, outfp) != 1) {
      fclose(infp);
      fclose(outfp);
      return 0;
    }
  }
  fclose(infp);
  fclose(outfp);
  return 1;
}

int raw_bufToPS(unsigned char *buf, int w, int h, int d, 
		FILE *outfp, int flags) 
{
  unsigned char *row, *r;
  int i, j, k;

  fputs("%!PS-Adobe-3.0\n", outfp);
  fputs("%%Creator: (dlsh/dlwish)\n", outfp);
  fprintf(outfp, "%%%%Title: (raw_image)\n");
  fprintf(outfp, "%%%%BoundingBox: %d %d %d %d\n", 0, 0, w, h);
  fputs("%%EndComments\n", outfp);

  fprintf(outfp, "0 %d translate\n", h);

  if (d == 1) fputs("/DeviceGray setcolorspace\n", outfp);
  else fputs("/DeviceRGB setcolorspace\n", outfp);

  fputs("\n<<\n", outfp);
  fputs("\t/ImageType 1\n", outfp);
  fprintf(outfp, "\t/Width %d\n\t/Height %d\n", w, h);
  fputs("\t/BitsPerComponent 8\n", outfp);
  fputs("\t/ImageMatrix [1 0 0 -1 0 0]\n", outfp);
  fputs("\t/DataSource currentfile /ASCIIHexDecode filter\n", outfp);
  fputs("\t/MultipleDataSources false\n", outfp);

  if (d == 1) fputs("\t/Decode [ 0 1 ]\n", outfp);
  else fputs("\t/Decode [ 0 1 0 1 0 1 ]\n", outfp);
  
  fputs(">>\n", outfp);

  fputs("image\n", outfp);

  switch (d) {
  case 1:
    for (i = 0, k = 0; i < h; i++) {
      if (flags & RAW_FLAGS_FLIP) row = &buf[w*d*(h-i-1)];
      else row = &buf[w*d*i];
      for (j = 0, r = row; j < w; j++, k++) {
	if (k%36 == 0) fprintf(outfp, "\n  ");
	fprintf(outfp, "%02x", *r++);
      }
    }
    break;
  case 3:
  case 4:
    for (i = 0, k = 0; i < h; i++) {
      if (flags & RAW_FLAGS_FLIP) row = &buf[w*d*(h-i-1)];
      else row = &buf[w*d*i];
      for (j = 0, r = row; j < w; j++, k++, r += d) {
	if (k%12 == 0) fprintf(outfp, "\n  ");
	fprintf(outfp, "%02x%02x%02x", r[0], r[1], r[2]);
      }
    }
    break;
  }
  fputs("\n>\n", outfp);
  return 1;
}

int raw_toPS(char *filename, FILE *outfp, int options)
{
  unsigned char *row, *r;
  int nbytes;
  FILE *infp;
  int i, j, k;
  int w, h, d;
  w = h = d = 0;
  if (!raw_getImageDims(filename, &w, &h, &d, &nbytes)) return 0;

  infp = fopen(filename, "rb");

  fputs("%!PS-Adobe-3.0\n", outfp);
  fputs("%%Creator: (dlsh/dlwish)\n", outfp);
  fprintf(outfp, "%%%%Title: (%s)\n", filename);
  fprintf(outfp, "%%%%BoundingBox: %d %d %d %d\n", 0, 0, w, h);
  fputs("%%EndComments\n", outfp);

  fprintf(outfp, "0 %d translate\n", h);

  /* Allocate space for a single row */
  row = (unsigned char *) calloc(w*d, sizeof(char));
  if (!row) {
    fclose(infp);
    return 0;
  }

  if (nbytes) fseek(infp, nbytes, SEEK_SET);

  if (d == 1) fputs("/DeviceGray setcolorspace\n", outfp);
  else fputs("/DeviceRGB setcolorspace\n", outfp);

  fputs("\n<<\n", outfp);
  fputs("\t/ImageType 1\n", outfp);
  fprintf(outfp, "\t/Width %d\n\t/Height %d\n", w, h);
  fputs("\t/BitsPerComponent 8\n", outfp);
  fputs("\t/ImageMatrix [1 0 0 -1 0 0]\n", outfp);
  fputs("\t/DataSource currentfile /ASCIIHexDecode filter\n", outfp);
  fputs("\t/MultipleDataSources false\n", outfp);

  if (d == 1) fputs("\t/Decode [ 0 1 ]\n", outfp);
  else fputs("\t/Decode [ 0 1 0 1 0 1 ]\n", outfp);
  
  fputs(">>\n", outfp);

  fputs("image\n", outfp);

  switch (d) {
  case 1:
    for (i = 0, k = 0; (unsigned) i < h; i++) {
      if (fread(row, d, w, infp) != w) {
	fclose(infp);
	free(row);
	return 0;
      }
      for (j = 0, r = row; (unsigned int) j < w; j++, k++) {
	if (k%36 == 0) fprintf(outfp, "\n  ");
	fprintf(outfp, "%02x", *r++);
      }
    }
    break;
  case 3:
  case 4:
    for (i = 0, k = 0; (unsigned) i < h; i++) {
      if (fread(row, d, w, infp) != w) {
	fclose(infp);
	free(row);
	return 0;
      }
      for (j = 0, r = row; (unsigned int) j < w; j++, k++, r += d) {
	if (k%12 == 0) fprintf(outfp, "\n  ");
	fprintf(outfp, "%02x%02x%02x", r[0], r[1], r[2]);
      }
    }
    break;
  }
  fputs("\n>\n", outfp);
  fclose(infp);
  free(row);
  return 1;
}

#ifdef STAND_ALONE_CHECK
int main(int argc, char *argv[])
{
  int i;
  int w, h, d, hbytes;
  for (i = 1; i < argc; i++) {
    w = h = d = 0;
    if (raw_getImageDims(argv[i], &w, &h, &d, &hbytes)) {
      printf("File: %s - %dx%dx%d (%d byte header)\n", 
	     argv[i], w, h, d, hbytes);
    }
    else {
      printf("File: %s - unknown size\n", argv[i]);
    }
  }
  return 1;
}
#endif

#ifdef STAND_ALONE
int main(int argc, char *argv[])
{
  if (argc < 3) {
    fprintf(stderr, "usage: rawaddheader infile outfile\n");
    exit(0);
  }
  raw_addHeader(argv[1], argv[2], 0, 0, 0);
  return 1;
}
#endif

#ifdef STAND_ALONE_PS
int main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(stderr, "usage: raw_tops infile\n");
    exit(0);
  }
  raw_toPS(argv[1], stdout, 0);
  return 1;
}
#endif

