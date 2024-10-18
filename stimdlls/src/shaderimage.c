/*
 * NAME
 *   shaderimage.c
 *
 * DESCRIPTION
 *  Simplified version of image.c to use with shaders
 * 
 * DETAILS 
 *  Provide support for loading images into OpenGL textures that can
 * be attached to shaders.  A key difference from the original
 * image library is the elimination of the power of two requirement.
 * 
 *
 * AUTHOR
 *    DLS / MAR-98 / MAR-17
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
#include <tcl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h> 

#include <df.h>
#include <tcl_dl.h>
#include <rawapi.h>

/****************************************************************/
/*                 Stim Specific Headers                        */
/****************************************************************/

#include <stim2.h>
#include "targa.h"

/****************************************************************/
/*                      Local Datatypes                         */
/****************************************************************/

#define MAX_IMAGES 8192		/* each collection can contain  */

struct _imagelist;

typedef struct _imagedata {
  int id;			/* handle for this image data  */
  struct _imagelist *imagelist;	/* to which list do I belong?  */
  int h, w;			/* height and width of the img */
  int nlayers;			/* if > 0, 3D texture          */
  int format;			/* RGB, RGBA, ALPHA, LUMINANCE */
  int datatype;			/* UNSIGNED_CHAR, FLOAT, ...   */
  int filter;			/* NEAREST or LINEAR           */
  float contrast;		/* adjust SCALE/BIAS upon load */
  int wrap;			/* GL_CLAMP or GL_REPEAT       */
  void *pixels;                 /* actual pixels for reloading */
  int imageid;			/* texture object id           */
  float aspect;			/* y/x aspect ratio for image  */
} IMAGE_DATA;

typedef struct _imagelist {
  int ntextures;
  GLuint texids[MAX_IMAGES];
  IMAGE_DATA idatas[MAX_IMAGES];
} IMAGE_LIST;

static int FilterType = GL_LINEAR; /* default filter type       */
static int AlphaFilterType = GL_LINEAR; /* default alpha filter */

#define GL_CLAMP_TO_EDGE  0x812F /* from the OpenGL 1.2 spec    */
static int WrapType = GL_CLAMP_TO_EDGE; /* default wrap mode    */

/****************************************************************/
/*                    Function Prototypes                       */
/****************************************************************/

static int LoadRGBAFile(char *filename, IMAGE_DATA *);
static int LoadTGAFile(char *filename, IMAGE_DATA *);
static int LoadPNGFile(char *filename, IMAGE_DATA *idata);
void imageListReset(void);
static int imageCreate(DYN_LIST *dl, int width, int height, int nlayers,
		       int filter, float, int, int);

/*************************************************************************
 * Object: IMAGE_LIST
 * Purpose: Store actual textures loaded from image files
 * Tcl Command: imgload
 * Callbacks:  None
 * TclCmdFunc: imageLoadCmd
 *************************************************************************/
 
static IMAGE_LIST ImageList; /* image list  */

static void imageAddTexture(IMAGE_DATA *idata)
{
  IMAGE_LIST *imagelist;
  int id;
  id = idata->id;
  imagelist = idata->imagelist;

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, idata->w);

  glGenTextures(1, &(imagelist->texids[id]));

  if (idata->nlayers <= 1) {
    glBindTexture(GL_TEXTURE_2D, imagelist->texids[id]);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, idata->wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, idata->wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, idata->filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, idata->filter);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
		 idata->w, idata->h, 0, 
		 idata->format, idata->datatype, 
		 idata->pixels);
  }
  else {
    glBindTexture(GL_TEXTURE_2D_ARRAY, imagelist->texids[id]);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, idata->wrap);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, idata->wrap);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, idata->filter);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, idata->filter);

    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA,
		 idata->w, idata->h, idata->nlayers, 0,
		 idata->format, idata->datatype, idata->pixels);
  }
  idata->imageid = id;
}

static int imageLoad(char *filename, int width, int height, 
	      int filter, int wrap, float contrast, int format, 
	      int grayscale, int listid)
{
  IMAGE_DATA *idata;		/* Object holder for image pixels */
  int status = 0;
  IMAGE_LIST *imagelist = &ImageList;

  if (imagelist->ntextures >= MAX_IMAGES) return -1;
  idata = &(imagelist->idatas[imagelist->ntextures]);
  idata->w = width;
  idata->h = height;
  idata->nlayers = 0;		/* 2D Image */
  idata->filter = filter;
  idata->wrap = wrap;
  idata->contrast = contrast;
  idata->imagelist = imagelist;

  if (format >= 0) idata->format = format;

  /* If possible, try to find a raw format of the file */
  if (strstr(filename, ".rgb") || strstr(filename, ".raw")) 
    status = LoadRGBAFile(filename, idata);
  else if (strstr(filename, ".tga"))
    status = LoadTGAFile(filename, idata);
  else if (strstr(filename, ".png"))
    status = LoadPNGFile(filename, idata);

  if (status <= 0 ) return -1;
  
  idata->id = imagelist->ntextures++;
  imageAddTexture(idata);

  return idata->imageid;
}

int imageLoadCmd(ClientData clientData, Tcl_Interp *interp,
		 int argc, char *argv[])
{
  double contrast = 1.0;
  int id, listid = 0;
  int w = 0, h = 0, filter = FilterType, wrap = WrapType, format = -1;
  int grayscale = 0;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " filename [width height] [filter]", NULL);
    return TCL_ERROR;
  }

  if (argc > 2) {		/* height and width specified */
    if (Tcl_GetInt(interp, argv[2], &w) != TCL_OK) return TCL_ERROR;
  }
  if (argc > 3) {
    if (Tcl_GetInt(interp, argv[3], &h) != TCL_OK) return TCL_ERROR;
  }
  if (argc > 4) {
    if (!strcmp(argv[4], "NEAREST") || !strcmp(argv[4], "nearest")) 
      filter = GL_NEAREST;
    else if (!strcmp(argv[4], "LINEAR") || !strcmp(argv[4], "linear")) 
      filter = GL_LINEAR;
    else {
      Tcl_AppendResult(interp, "unknown filter type: \"", argv[4], "\"", NULL);
      return TCL_ERROR;
    }
  }
  
  if ((id = imageLoad(argv[1], w, h, filter, wrap,
		      contrast, format, grayscale, listid)) < 0) {
    char buf[128];
    if (strlen(argv[1]) < 128) 
      Tcl_AppendResult(interp, argv[0],
		       ": unable to load image \"", argv[1], "\"", NULL);
    else {
      strncpy(buf, argv[1], 100);
      buf[100] = '\0';		/* null terminate... */
      Tcl_AppendResult(interp, argv[0],
		       ": unable to load image \"", buf, "...\"", NULL);
    }
    return(TCL_ERROR);
  }

  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));

  return(TCL_OK);
}

int imageSetFilterType(ClientData cdata, Tcl_Interp * interp, 
		       int objc, Tcl_Obj * const objv[])
{
  if (objc < 2) {
    Tcl_WrongNumArgs(interp, 1, objv, "filter");
    return TCL_ERROR;
  }
  const char *filter = Tcl_GetString(objv[1]);
  
  if (!strcmp(filter, "NEAREST") || !strcmp(filter, "nearest")) 
    FilterType = GL_NEAREST;
  else if (!strcmp(filter, "LINEAR") || !strcmp(filter, "linear")) 
    FilterType = GL_LINEAR;
  else {
    Tcl_AppendResult(interp, "unknown filter type: \"", filter, "\"", NULL);
    return TCL_ERROR;
  }
  
  return TCL_OK;
}

int imageTextureIDCmd(ClientData  clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  int listid = 0;
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  IMAGE_LIST *imagelist = &ImageList;

  int imageid;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " imageid", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &imageid) != TCL_OK) return TCL_ERROR;
  if (imageid >= imagelist->ntextures) {
    Tcl_AppendResult(interp, "imageMake: image id out of range", NULL);
    return(TCL_ERROR);
  }

  Tcl_SetObjResult(interp, Tcl_NewIntObj(imagelist->texids[imageid]));
  return(TCL_OK);
}


void imageListReset(void)
{
  IMAGE_LIST *ilist = &ImageList;  
  int i;
  char *pixels;
  for (i = 0; i < ilist->ntextures; i++) {
    glDeleteTextures(1, &ilist->texids[i]);
    pixels = (char *) (&ilist->idatas[i])->pixels;
    if (pixels) {
      free(pixels);
      (&ilist->idatas[i])->pixels = NULL;
    }
  }
  ilist->ntextures = 0;
}

int imageResetCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  imageListReset();
  return TCL_OK;
}

static int dynListToPixels(DYN_LIST *dl, IMAGE_DATA *idata)
{
  DYN_LIST **sublists;		/* For RGB and RGBA specification */
  int n, i;
  int size;

  if (idata->nlayers == 0) size = idata->w*idata->h;
  else size = idata->nlayers*idata->w*idata->h;
    
  idata->aspect = (float) (idata->w)/idata->h;
  switch (DYN_LIST_DATATYPE(dl)) {
  case DF_FLOAT:
    n = DYN_LIST_N(dl);
    if (idata->format < 0) {
      if (DYN_LIST_N(dl) % size) return 0;
      else switch (DYN_LIST_N(dl) / size) {
      case 1: idata->format = GL_R8; break;
      case 3: idata->format = GL_RGB; break;
      case 4: idata->format = GL_RGBA; break;
      }
    }
    idata->datatype = GL_FLOAT;

    idata->pixels = (float *) calloc(n, sizeof(float));
    if (!idata->pixels) return 0;
    memcpy(idata->pixels, DYN_LIST_VALS(dl), n*sizeof(float));
    break;
  case DF_CHAR:
    n = DYN_LIST_N(dl);
    if (idata->format < 0) {
      if (DYN_LIST_N(dl) % size) return 0;
      else switch (DYN_LIST_N(dl) / size) {
      case 1: idata->format = GL_R8; break;
      case 3: idata->format = GL_RGB; break;
      case 4: idata->format = GL_RGBA; break;
      }
    }

    idata->datatype = GL_UNSIGNED_BYTE;
    idata->pixels = (char *) calloc(n, sizeof(char));
    if (!idata->pixels) return 0;
    memcpy(idata->pixels, DYN_LIST_VALS(dl), n*sizeof(char));
    break;
  case DF_LONG:
    n = DYN_LIST_N(dl);
    if (idata->format < 0) {
      if (DYN_LIST_N(dl) % size) return 0;
      else switch (DYN_LIST_N(dl) / size) {
      case 1: idata->format = GL_R8; break;
      case 3: idata->format = GL_RGB; break;
      case 4: idata->format = GL_RGBA; break;
      }
    }
    idata->datatype = GL_INT;
    idata->pixels = (long *) calloc(n, sizeof(long));
    if (!idata->pixels) return 0;
    memcpy(idata->pixels, DYN_LIST_VALS(dl), n*sizeof(long));
    break;
  case DF_LIST:		/* Supports only RGB and RGBA chars for now */
    sublists = (DYN_LIST **) DYN_LIST_VALS(dl);
    if (!DYN_LIST_N(dl)) return 0;

    /* Check the lengths and datatypes */
    for (i = 1; i < DYN_LIST_N(dl); i++) {
      if (DYN_LIST_N(sublists[i]) != DYN_LIST_N(sublists[0])) 
	return 0;
      if (DYN_LIST_DATATYPE(sublists[i]) != DYN_LIST_DATATYPE(sublists[0])) 
	return 0;
    }
    n = DYN_LIST_N(sublists[0]);
    if (size != n) return 0;
    
    switch (DYN_LIST_N(dl)) {
    case 3:			/* RGB */
      switch (DYN_LIST_DATATYPE(sublists[0])) {
      case DF_CHAR:
	{
	  char *r, *g, *b, *pix;
	  idata->format = GL_RGB;
	  idata->datatype = GL_UNSIGNED_BYTE;

	  /* Now we interleave the data */
	  r = (char *) DYN_LIST_VALS(sublists[0]);
	  g = (char *) DYN_LIST_VALS(sublists[1]);
	  b = (char *) DYN_LIST_VALS(sublists[2]);
	  idata->pixels = (char *) calloc(n, 3*sizeof(char));
	  if (!idata->pixels) return 0;
	  pix = idata->pixels;
	  for (i = 0; i < n; i++) {
	    *pix++ = *r++;
	    *pix++ = *g++;
	    *pix++ = *b++;
	  }
	}
      }
      break;
    case 4: 
      switch (DYN_LIST_DATATYPE(sublists[0])) {
      case DF_CHAR:
	{
	  char *r, *g, *b, *a, *pix;
	  int i;
	  idata->format = GL_RGBA;
	  idata->datatype = GL_UNSIGNED_BYTE;

	  /* Now we interleave the data */
	  r = (char *) DYN_LIST_VALS(sublists[0]);
	  g = (char *) DYN_LIST_VALS(sublists[1]);
	  b = (char *) DYN_LIST_VALS(sublists[2]);
	  a = (char *) DYN_LIST_VALS(sublists[3]);
	  idata->pixels = (char *) calloc(n, 4*sizeof(char));
	  if (!idata->pixels) return 0;
	  pix = idata->pixels;
	  for (i = 0; i < n; i++) {
	    *pix++ = *r++;
	    *pix++ = *g++;
	    *pix++ = *b++;
	    *pix++ = *a++;
	  }
	}
      }
      break;
    }
    break;
  default:
    return 0;
  }
  return 1;
}


/* See if this image is 3D or just w x h */
static int imageGetDepth(DYN_LIST *dl, int w, int h)
{
  DYN_LIST **sublists;
  int n;
  switch (DYN_LIST_DATATYPE(dl)) {
  case DF_LIST:
    {
      if (DYN_LIST_N(dl) != 3 && DYN_LIST_N(dl) != 4) return -1;
      sublists = (DYN_LIST **) DYN_LIST_VALS(dl);
      n = DYN_LIST_N(sublists[0]);
      if (n % (w*h)) return -1;
      else return (n / (w*h));
    }
    break;
  case DF_LONG:
  case DF_FLOAT:
  case DF_CHAR:
    {
      n = DYN_LIST_N(dl);
      /* try RGBA, then RGB, then LUM */
      if (n%(w*h*4) == 0) return n/(w*h*4);
      if (n%(w*h*3) == 0) return n/(w*h*3);
      if (n%(w*h) == 0) return n/(w*h);
      return -1;
    }
    break;
  default:
    return -1;
    break;
  }
}


static int imageCreate(DYN_LIST *dl, int width, int height, int nlayers,
		       int filter, float contrast, int format, int listid)
{
  IMAGE_DATA *idata;		/* Object holder for image pixels */
  IMAGE_LIST *imagelist = &ImageList;

  if (imagelist->ntextures >= MAX_IMAGES) return -1;
  idata = &(imagelist->idatas[imagelist->ntextures]);
  idata->imagelist = imagelist;
  idata->w = width;
  idata->h = height;
  idata->nlayers = nlayers;
  idata->filter = filter;
  idata->contrast = 1.0;
  idata->wrap = WrapType;

  if (format >= 0) idata->format = format;
  else idata->format = -1;

  if (!(dynListToPixels(dl, idata))) return -1;
  idata->id = imagelist->ntextures++;

  imageAddTexture(idata);
  return idata->imageid;
}

int imageCreateCmd(ClientData clientData, Tcl_Interp *interp,
			  int argc, char *argv[])
{
  int listid = 0;
  int id;
  float contrast = 1.0;
  DYN_LIST *dl;
  int w = 0, h = 0, filter = GL_NEAREST, format = -1;
  int nlayers = 0;
  
  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " dynlist width height [filter format]", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[1], &dl) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &w) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &h) != TCL_OK) return TCL_ERROR;

  nlayers = imageGetDepth(dl, w, h);
  if (nlayers < 0) {
    Tcl_AppendResult(interp, argv[0], ": invalid image data", (char *) NULL);
    return(TCL_ERROR);
  }
  
  if (argc > 4) {
    if (!strcmp(argv[4], "NEAREST") || !strcmp(argv[4], "nearest")) 
      filter = GL_NEAREST;
    else if (!strcmp(argv[4], "LINEAR") || !strcmp(argv[4], "linear")) 
      filter = GL_LINEAR;
  }

  if ((id = imageCreate(dl, w, h, nlayers,
			filter, contrast, format, listid)) < 0) {
    Tcl_AppendResult(interp, argv[0],
		     ": unable to create image from dynlist \"", argv[1],
		     "\"", NULL);
    return(TCL_ERROR);
  }

  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));

  return(TCL_OK);
}


int imageCreateFromStringCmd(ClientData cdata, Tcl_Interp * interp, 
				    int objc, Tcl_Obj * const objv[])
{
  unsigned char *data;
  int id;
  Tcl_Size length;
  float contrast = 1.0;
  DYN_LIST *dl;
  int w = 0, h = 0, filter = GL_NEAREST, format = -1;
  int nlayers;
  
  if (objc < 2) {
    Tcl_WrongNumArgs(interp, 1, objv, "data width height filter format");
    return TCL_ERROR;
  }
  
  data = (unsigned char *) Tcl_GetByteArrayFromObj(objv[1], &length);
  if (!data) {
    Tcl_AppendResult(interp, Tcl_GetString(objv[0]), ": invalid data");
    return(TCL_ERROR);
  }
  
  if (Tcl_GetIntFromObj(interp, objv[2], &w) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetIntFromObj(interp, objv[3], &h) != TCL_OK) return TCL_ERROR;

  if (objc > 4) {
    char *filtername = Tcl_GetString(objv[4]);
    if (!strcmp(filtername, "NEAREST") || !strcmp(filtername, "nearest")) 
      filter = GL_NEAREST;
    else if (!strcmp(filtername, "LINEAR") || !strcmp(filtername, "linear")) 
      filter = GL_LINEAR;
  }
  if (objc > 5) {
    char *formatname = Tcl_GetString(objv[5]);
    if (!strcmp(formatname, "ALPHA") || !strcmp(formatname, "alpha")) 
      format = GL_ALPHA;
  }

  dl = dfuCreateDynListWithVals(DF_CHAR, length, data);
  nlayers = imageGetDepth(dl, w, h);
  if (nlayers < 0) {
    Tcl_AppendResult(interp, "imgfromstring: invalid image data",
		     (char *) NULL);
    return(TCL_ERROR);
  }
  
  if ((id = imageCreate(dl, w, h, nlayers,
			filter, contrast, format, 0)) < 0) {
    Tcl_AppendResult(interp, 
		     Tcl_GetString(objv[0]),
		     ": unable to create image from dynlist \"",
		     Tcl_GetString(objv[1]), "\"", NULL); 

    dfuFreeDynList(dl);
    return(TCL_ERROR);
  }

  dfuFreeDynList(dl);

  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
  return(TCL_OK);
}




/****************************************************************/
/*                       Utility Funcs                          */
/****************************************************************/


/****************************************************************
 * Function
 *   LoadRGBAFile
 *
 * Description
 *   Read in a raw RGBA file
 ****************************************************************/

static int LoadRGBAFile(char *filename, IMAGE_DATA *idata)
{
  FILE *fp;
  int d = 0, size, header_bytes;
  
  int w = idata->w;
  int h = idata->h;

  if (!raw_getImageDims(filename, &w, &h, &d, &header_bytes)) {
    return 0;
  }

  if (!(fp = fopen(filename, "rb"))) return 0;

  /* Skip header if there */
  if (header_bytes) fseek(fp, header_bytes, SEEK_SET);

  idata->w = w;
  idata->h = h;

  idata->datatype = GL_UNSIGNED_BYTE;

  switch (d) {
  case 1: 
    if (idata->format != GL_ALPHA) {
      idata->format = GL_R8;
    }  
    break;
  case 3: idata->format = GL_RGB;        break;
  case 4: idata->format = GL_RGBA;       break;
  default: fclose(fp);   return 0;       break;
  }

  size = w*h*d;
  idata->pixels = (unsigned char *) calloc(1, size);
  idata->aspect = (float) (idata->w) / idata->h;

  if (!idata->pixels) {
    fclose(fp);
    return 0;
  }
  if (fread(idata->pixels, size, 1, fp) != 1) {
    free(idata->pixels); 
    fclose(fp);
    return 0;
  }
  fclose(fp);
  return 1;
}


static int LoadTGAFile(char *filename, IMAGE_DATA *idata)
{    
  int d;
  tga_image img;
  if (tga_read(&img, filename) != TGA_NOERR) return -1;

  /* this is the opposite of image.c - why?! */
  if (!tga_is_top_to_bottom(&img)) tga_flip_vert(&img);

  if (tga_is_right_to_left(&img)) tga_flip_horiz(&img);
  tga_swap_red_blue(&img);

  //  if (!tgaLoad (filename, &p, TGA_NO_PASS)) return -1;

  idata->w = img.width;
  idata->h = img.height;
  d = img.pixel_depth/8;

  idata->datatype = GL_UNSIGNED_BYTE;

  switch (d) {
  case 1:
    if (idata->format != GL_ALPHA) {
      idata->format = GL_R8;
    }  
    break;
  case 3: idata->format = GL_RGB;        break;
  case 4: idata->format = GL_RGBA;       break;
  default:   return 0;       break;
  }

  idata->aspect = (float) (idata->w) / idata->h;
  idata->pixels = img.image_data;
  if (img.image_id) free(img.image_id);
  if (img.color_map_data) free(img.color_map_data);

  return 1;
}


/*read the information from the header and store it in the LodePNG_Info. return value is error*/
static int PNG_GetInfo(const unsigned char* in, size_t inlength, int *w, int *h, int *bitDepth, int *colorType)
{
  extern unsigned LodePNG_read32bitInt(const unsigned char* buffer);

  if(inlength == 0 || in == 0) return -1;
  if(inlength < 29) return -1;

  if(in[0] != 137 || in[1] != 80 || in[2] != 78 || in[3] != 71
     || in[4] != 13 || in[5] != 10 || in[6] != 26 || in[7] != 10)
  {
    return -1;
  }
  if(in[12] != 'I' || in[13] != 'H' || in[14] != 'D' || in[15] != 'R')
  {
    return -1;
  }

  /*read the values given in the header*/
  if (w) *w = LodePNG_read32bitInt(&in[16]);
  if (h) *h = LodePNG_read32bitInt(&in[20]);
  if (bitDepth) *bitDepth = in[24];
  if (colorType) *colorType = in[25];
  return 0;
}

static int LoadPNGFile(char *filename, IMAGE_DATA *idata)
{    
  extern unsigned LodePNG_loadFile(unsigned char** out, size_t* outsize, const char* filename);
  extern unsigned LodePNG_decode(unsigned char** out, unsigned* w, unsigned* h, const unsigned char* in,
				 size_t insize, unsigned colorType, unsigned bitDepth);

  unsigned char *pixeldata;	/* returned from decode */
  unsigned int w, h;
  int d;
  unsigned char* buffer;
  size_t buffersize;
  unsigned error;
  int bitDepth;
  int colorType;

  error = LodePNG_loadFile(&buffer, &buffersize, filename);
  if (error) return -1;

  error = PNG_GetInfo(buffer, buffersize, NULL, NULL, &bitDepth, &colorType); 
  if (error) return -1;
  
  if (colorType != 0 && colorType != 2 && colorType != 6) 
    return -1; /* only handle these three (RGBA, RGB, GRAYSCALE) */
  
  error = LodePNG_decode(&pixeldata, &w, &h, buffer, buffersize, colorType, bitDepth);
  free(buffer);
  if (error) return -1;

  idata->w = w;
  idata->h = h;

  if (colorType == 6) {
    d = 4;			
    idata->format = GL_RGBA;
  } else if (colorType == 2) {
    d = 3;			
    idata->format = GL_RGB;
  } else if (colorType == 0) {
    d = 1;			
    idata->format = GL_R8;
  }

  idata->datatype = GL_UNSIGNED_BYTE;
  idata->aspect = (float) (idata->w) / idata->h;
  idata->pixels = pixeldata;
  return 1;
}

