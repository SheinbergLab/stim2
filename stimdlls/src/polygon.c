/*
 * Polygon.c
 *  Draw polygonal shapes using vertex extensions
 */


#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tcl.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h> 

#include "df.h"
#include "tcl_dl.h"
#include <stim2.h>
#include "shaderutils.h"
#include "objname.h"

typedef struct _vao_info {
  GLuint vao;
  int narrays;
  int nindices;
  GLuint points_vbo;
  GLuint texcoords_vbo;
} VAO_INFO;


typedef struct polygon {
  int filled;
  int type;			/* draw type        */
  float linewidth;
  float pointsize;
  float color[4];
  int circ;                     /* treat poly as circ */
  float mouth_half;		/* sector half-mouth, radians (0 = no wedge) */
  float inner_rad;		/* annulus inner radius, uv units 0..0.5 (0 = solid) */
  int nverts;			/* number of x,y,xs */
  float *verts;			/* x,y,z triplets   */
  int ntexcoords;		/* number of u,vs   */
  float *texcoords;		/* u,v doubles vv   */

  UNIFORM_INFO *modelviewMat;
  UNIFORM_INFO *projMat;
  UNIFORM_INFO *uColor;
  UNIFORM_INFO *circle;
  UNIFORM_INFO *mouthHalf;
  UNIFORM_INFO *innerRad;
  UNIFORM_INFO *pointSize;
  SHADER_PROG *program;
  VAO_INFO *vao_info;		/* to track vertex attributes */
  Tcl_HashTable uniformTable;	/* local unique version */
  Tcl_HashTable attribTable;	/* local unique version */

} POLYGON;

static int PolygonID = -1;	/* unique polygon object id */
SHADER_PROG *PolygonShaderProg = NULL;
enum { POLY_VERTS_VBO, POLY_TEXCOORDS_VBO };

static void delete_vao_info(VAO_INFO *vinfo)
{
  glDeleteBuffers(1, &vinfo->points_vbo);
  glDeleteBuffers(1, &vinfo->texcoords_vbo);
  glDeleteVertexArrays(1, &vinfo->vao);
}

static void update_vbo(POLYGON *p, int type)
{
  float *vals;
  int n, d;
  int vbo;
  switch(type) {
  case POLY_VERTS_VBO:
    {
      d = 3;			/* 3D */
      vals = p->verts;
      n = p->nverts;
      vbo = p->vao_info->points_vbo;
    }
    break;
  case POLY_TEXCOORDS_VBO:
    {
      d = 2;			/* 2D */
      vals = p->texcoords;
      n = p->ntexcoords;
      vbo = p->vao_info->texcoords_vbo;
    }
    break;
  }

  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, d*n*sizeof(GLfloat), vals, GL_STATIC_DRAW);
  p->vao_info->nindices = n;
}



void polygonDraw(GR_OBJ *g) 
{
  POLYGON *p = (POLYGON *) GR_CLIENTDATA(g);
  SHADER_PROG *sp = (SHADER_PROG *) p->program;
  float *v;

  /* Update uniform table */
  if (p->modelviewMat) {
    v = (float *) p->modelviewMat->val;
    stimGetMatrix(STIM_MODELVIEW_MATRIX, v);
  }
  if (p->projMat) {
    v = (float *) p->projMat->val;
    stimGetMatrix(STIM_PROJECTION_MATRIX, v);
  }
  if (p->uColor) {
    v = (float *) p->uColor->val;
    v[0] = p->color[0];
    v[1] = p->color[1];
    v[2] = p->color[2];
    v[3] = p->color[3];
  }
  
  if (p->pointSize) {
    glEnable(GL_PROGRAM_POINT_SIZE);
    memcpy(p->pointSize->val, &p->pointsize, sizeof(float));
  }

  if (p->circle) {
    memcpy(p->circle->val, &p->circ, sizeof(int));
  }

  if (p->mouthHalf) {
    memcpy(p->mouthHalf->val, &p->mouth_half, sizeof(float));
  }

  if (p->innerRad) {
    memcpy(p->innerRad->val, &p->inner_rad, sizeof(float));
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  /* Line primitives honor the requested width (driver may clamp in core
     profiles, where widths > 1 are not guaranteed). */
  if (p->type == GL_LINES || p->type == GL_LINE_STRIP ||
      p->type == GL_LINE_LOOP) {
    glLineWidth(p->linewidth);
  }

  glUseProgram(sp->program);
  update_uniforms(&p->uniformTable);
  if (p->vao_info->narrays) {
    glBindVertexArray(p->vao_info->vao);
    glDrawArrays(p->type, 0, p->vao_info->nindices);
  }
  glUseProgram(0);
}


void polygonDelete(GR_OBJ *g) 
{
  POLYGON *p = (POLYGON *) GR_CLIENTDATA(g);
  if (p->verts) free(p->verts);
  if (p->texcoords) free(p->texcoords);

  delete_uniform_table(&p->uniformTable);
  delete_attrib_table(&p->attribTable);
  delete_vao_info(p->vao_info);

  free((void *) p);
}

#ifdef USE_UPDATE
void polygonUpdate(GR_OBJ *g) 
{
  POLYGON *p = (POLYGON *) GR_CLIENTDATA(g);
  /* Do something here */
}
#endif


int polygonCreate(OBJ_LIST *objlist, SHADER_PROG *sp)
{
  const char *name = "Polygon";
  GR_OBJ *obj;
  POLYGON *p;
  Tcl_HashEntry *entryPtr;

  static GLfloat p_texcoords[] = { 0., 0.,
				 1., 0.,
				 0., 1.,
				 1., 0.,
				 1., 1.,
				 0., 1 };

  static GLfloat p_verts[] = { -.5, -.5, 0,
			      .5, -.5, 0,
			      -.5, .5, 0.,
			      .5, -.5, 0.,
			      .5, .5, 0.,
			      -.5, .5, 0};

  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = PolygonID;

  GR_ACTIONFUNCP(obj) = polygonDraw;
  GR_DELETEFUNCP(obj) = polygonDelete;

  p = (POLYGON *) calloc(1, sizeof(POLYGON));
  GR_CLIENTDATA(obj) = p;
  
  /* Default to white */
  p->color[0] = 1.0;
  p->color[1] = 1.0;
  p->color[2] = 1.0;
  p->color[3] = 1.0;

  p->filled = 1;
  p->type = GL_TRIANGLES;

  p->linewidth = 1.0;
  
  /* Default polygon has no verts, no texcoords... They must be added! */
  p->nverts = 0;
  p->ntexcoords = 0;

  p->program = sp;
  copy_uniform_table(&sp->uniformTable, &p->uniformTable);
  copy_attrib_table(&sp->attribTable, &p->attribTable);

  p->vao_info = (VAO_INFO *) calloc(1, sizeof(VAO_INFO));
  p->vao_info->narrays = 0;
  glGenVertexArrays(1, &p->vao_info->vao);
  glBindVertexArray(p->vao_info->vao);

  if ((entryPtr = Tcl_FindHashEntry(&p->attribTable, "vertex_position"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    glGenBuffers(1, &p->vao_info->points_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, p->vao_info->points_vbo);
    glVertexAttribPointer(ainfo->location, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
    p->vao_info->narrays++;
  }

  if ((entryPtr = Tcl_FindHashEntry(&p->attribTable, "vertex_texcoord"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    glGenBuffers(1, &p->vao_info->texcoords_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, p->vao_info->texcoords_vbo);
    glVertexAttribPointer(ainfo->location, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
    p->vao_info->narrays++;
  }

  if ((entryPtr = Tcl_FindHashEntry(&p->uniformTable, "modelviewMat"))) {
    p->modelviewMat = Tcl_GetHashValue(entryPtr);
    p->modelviewMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&p->uniformTable, "projMat"))) {
    p->projMat = Tcl_GetHashValue(entryPtr);
    p->projMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&p->uniformTable, "uColor"))) {
    p->uColor = Tcl_GetHashValue(entryPtr);
    p->uColor->val = malloc(sizeof(float)*4);
  }

  if ((entryPtr = Tcl_FindHashEntry(&p->uniformTable, "circle"))) {
    p->circle = Tcl_GetHashValue(entryPtr);
    p->circle->val = calloc(1,sizeof(int));
  }

  if ((entryPtr = Tcl_FindHashEntry(&p->uniformTable, "mouthHalf"))) {
    p->mouthHalf = Tcl_GetHashValue(entryPtr);
    p->mouthHalf->val = calloc(1,sizeof(float));
  }

  if ((entryPtr = Tcl_FindHashEntry(&p->uniformTable, "innerRad"))) {
    p->innerRad = Tcl_GetHashValue(entryPtr);
    p->innerRad->val = calloc(1,sizeof(float));
  }

  if ((entryPtr = Tcl_FindHashEntry(&p->uniformTable, "pointSize"))) {
    p->pointSize = Tcl_GetHashValue(entryPtr);
    p->pointSize->val = calloc(1,sizeof(float));
  }

  /* Default to filled rectangle */
  p->verts = (GLfloat *) malloc(sizeof(float)*18);
  memcpy(p->verts, p_verts, sizeof(float)*18);
  p->nverts = 6;
  if (p->texcoords) free(p->texcoords);
  p->texcoords = (GLfloat *) malloc(sizeof(float)*12);
  memcpy(p->texcoords, p_texcoords, sizeof(float)*12);
  p->ntexcoords = 6;
  p->type = GL_TRIANGLES;
  update_vbo(p, POLY_VERTS_VBO);
  update_vbo(p, POLY_TEXCOORDS_VBO);

  p->circ = 0;
  p->filled = 1;
  
  return(gobjAddObj(objlist, obj));
}



static int polygonCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;
  if (argc < 1) {
    Tcl_AppendResult(interp, "usage:", argv[0], NULL);
    return TCL_ERROR;
  }

  if ((id = polygonCreate(olist, PolygonShaderProg)) < 0) {
    Tcl_SetResult(interp, "error creating polygon", TCL_STATIC);
    return(TCL_ERROR);
  }
  
  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
  return(TCL_OK);
}



static int polycircCmd(ClientData clientData, Tcl_Interp *interp,
		       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int id, circ;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage:", argv[0], " polygon 0|1", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], PolygonID, "polygon")) < 0)
    return TCL_ERROR;
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &circ) != TCL_OK) return TCL_ERROR;

  p->circ = circ;
  p->filled = 1;
  return TCL_OK;
}


/*
 * polysector polygon ?mouthDeg?
 *   Turn the (default unit-quad) polygon into an anti-aliased circular sector
 *   -- a "pac-man" -- by removing a wedge ("mouth") of the given angular width.
 *   The mouth is centred on +X; aim it with rotateObj. mouthDeg 0 = full disc.
 *   Operates on the masked round shape (implies circ=1), so scale the quad to
 *   the desired diameter; do not replace its verts with polyverts.
 */
static int polysectorCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int id;
  double mouth;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " polygon ?mouthDeg?", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], PolygonID, "polygon")) < 0)
    return TCL_ERROR;
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  /* Getter: return current mouth width in degrees */
  if (argc == 2) {
    char result[32];
    snprintf(result, sizeof(result), "%.6g", p->mouth_half * 2.0 * 180.0 / M_PI);
    Tcl_SetResult(interp, result, TCL_VOLATILE);
    return TCL_OK;
  }

  if (Tcl_GetDouble(interp, argv[2], &mouth) != TCL_OK) return TCL_ERROR;
  if (mouth < 0.0)   mouth = 0.0;
  if (mouth > 359.0) mouth = 359.0;   /* keep a sliver of shape */
  p->mouth_half = (mouth / 2.0) * M_PI / 180.0;
  p->circ = 1;
  p->filled = 1;
  return TCL_OK;
}


/*
 * polyannulus polygon ?innerFrac?
 *   Turn the (default unit-quad) polygon into an anti-aliased annulus (ring) by
 *   cutting a central hole of radius innerFrac * (outer radius). innerFrac in
 *   [0,1); 0 = solid disc. Combine with polysector to get an arc band. Implies
 *   circ=1, so scale the quad to the desired outer diameter.
 */
static int polyannulusCmd(ClientData clientData, Tcl_Interp *interp,
			  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int id;
  double frac;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " polygon ?innerFrac?", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], PolygonID, "polygon")) < 0)
    return TCL_ERROR;
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  /* Getter: return current inner radius as a fraction of the outer radius */
  if (argc == 2) {
    char result[32];
    snprintf(result, sizeof(result), "%.6g", p->inner_rad / 0.5);
    Tcl_SetResult(interp, result, TCL_VOLATILE);
    return TCL_OK;
  }

  if (Tcl_GetDouble(interp, argv[2], &frac) != TCL_OK) return TCL_ERROR;
  if (frac < 0.0)  frac = 0.0;
  if (frac > 0.99) frac = 0.99;
  p->inner_rad = frac * 0.5;   /* outer radius is 0.5 in uv units */
  p->circ = 1;
  p->filled = 1;
  return TCL_OK;
}


int combineDynlists(Tcl_Interp *interp, char *procname,
		    DYN_LIST *xlist, DYN_LIST *ylist, DYN_LIST *zlist,
		    int three_d, int *nOut,float  **vList)
{
  int type = 0;
  float *v, *verts;
  int nverts;
  int i;
  
  if (DYN_LIST_N(xlist) != DYN_LIST_N(ylist)) {
    Tcl_AppendResult(interp, procname, 
		     ": x and y vert lists must be same length", NULL);
    return TCL_ERROR;
  }

  if (DYN_LIST_DATATYPE(xlist) == DF_FLOAT && 
      DYN_LIST_DATATYPE(ylist) == DF_FLOAT) type = 0;
  else if (DYN_LIST_DATATYPE(xlist) == DF_LONG && 
	   DYN_LIST_DATATYPE(ylist) == DF_FLOAT) type = 1;
  else if (DYN_LIST_DATATYPE(xlist) == DF_FLOAT && 
	   DYN_LIST_DATATYPE(ylist) == DF_LONG) type = 2;
  else if (DYN_LIST_DATATYPE(xlist) == DF_LONG && 
	   DYN_LIST_DATATYPE(ylist) == DF_LONG) type = 3;
  else {
    Tcl_AppendResult(interp, procname, 
		     ": verts must be either longs or floats", NULL);
    return TCL_ERROR;
  }

  /* type 0-3 (any mix of float/long x,y lists) are all handled below */

  if (zlist && three_d) {
    if (DYN_LIST_DATATYPE(zlist) != DYN_LIST_DATATYPE(xlist)) {
      Tcl_AppendResult(interp, procname, 
		       ": z verts must be the same data type as x verts",NULL);
      return TCL_ERROR;
    }
    if (DYN_LIST_N(zlist) != DYN_LIST_N(xlist)) {
      Tcl_AppendResult(interp, procname, 
		       ": number of z verts must equal number of x verts",
		       NULL);
      return TCL_ERROR;
    }
  }

  verts = (float *) calloc(DYN_LIST_N(xlist)*(three_d?3:2), sizeof(float));
  v = verts;
  
  switch (type) {
  case 0:
    {
      float *xvals, *yvals, *zvals;
      xvals = (float *) DYN_LIST_VALS(xlist);
      yvals = (float *) DYN_LIST_VALS(ylist);
      if (zlist && three_d) zvals = (float *) DYN_LIST_VALS(zlist);
      if (!three_d) {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	}
      }
      else {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	  *v++ = !zlist ? 0.0 : zvals[i];
	}
      }
      nverts = DYN_LIST_N(xlist);
    }
    break;
  case 1:
    {
      int *xvals, *zvals;
      float *yvals;
      xvals = (int *) DYN_LIST_VALS(xlist);
      yvals = (float *) DYN_LIST_VALS(ylist);
      if (zlist && three_d) zvals = (int *) DYN_LIST_VALS(zlist);
      if (!three_d) {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	}
      }
      else {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	  *v++ = !zlist ? 0.0 : zvals[i];
	}
      }
      nverts = DYN_LIST_N(xlist);
    }
    break;
  case 2:
    {
      float *xvals, *zvals;
      int *yvals;
      xvals = (float *) DYN_LIST_VALS(xlist);
      yvals = (int *) DYN_LIST_VALS(ylist);
      if (zlist && three_d) zvals = (float *) DYN_LIST_VALS(zlist);
      if (!three_d) {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	}
      }
      else {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	  *v++ = !zlist ? 0.0 : zvals[i];
	}
      }
      nverts = DYN_LIST_N(xlist);
    }
    break;
  case 3:
    {
      int *xvals, *yvals, *zvals;
      xvals = (int *) DYN_LIST_VALS(xlist);
      yvals = (int *) DYN_LIST_VALS(ylist);
      if (zlist && three_d) zvals = (int *) DYN_LIST_VALS(zlist);
      if (!three_d) {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	}
      }
      else {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	  *v++ = !zlist ? 0.0 : zvals[i];
	}
      }
      nverts = DYN_LIST_N(xlist);
    }
    break;
  }

  if (nOut) *nOut = nverts;
  if (vList) *vList = verts;
  return TCL_OK;
}

static int polyvertsCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int id;
  DYN_LIST *xlist, *ylist, *zlist = NULL;
  float *verts;
  int nverts;

  if (argc < 4) {
    Tcl_AppendResult(interp, "usage:", argv[0],
		     "polygon xlist ylist [zlist]", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], PolygonID, "polygon")) < 0)
    return TCL_ERROR;
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (tclFindDynList(interp, argv[2], &xlist) != TCL_OK) return TCL_ERROR;
  if (tclFindDynList(interp, argv[3], &ylist) != TCL_OK) return TCL_ERROR;
  if (argc > 4) {
    if (tclFindDynList(interp, argv[4], &zlist) != TCL_OK) return TCL_ERROR;
  }

  if (combineDynlists(interp, argv[0],
		      xlist, ylist, zlist, 1, &nverts, &verts) !=
      TCL_OK) {
    return TCL_ERROR;
  }
    
  if (p->verts) free(p->verts);  
  p->verts = verts;
  p->nverts = nverts;
  
  update_vbo(p, POLY_VERTS_VBO);

  /* May not have tex coords, but shader expects, fill with zeroes */
  if (p->ntexcoords != p->nverts) {
    if (p->texcoords) free(p->texcoords);
    p->ntexcoords = p->nverts;
    p->texcoords = calloc(p->ntexcoords, 2*sizeof(float));
    update_vbo(p, POLY_TEXCOORDS_VBO);
  }
  
  return(TCL_OK);
}

static int polytexcoordsCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int id;
  DYN_LIST *xlist, *ylist;
  float *verts;
  int nverts;

  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " polygon xlist ylist", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], PolygonID, "polygon")) < 0)
    return TCL_ERROR;
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (tclFindDynList(interp, argv[2], &xlist) != TCL_OK) return TCL_ERROR;
  if (tclFindDynList(interp, argv[3], &ylist) != TCL_OK) return TCL_ERROR;

  if (combineDynlists(interp, argv[0], xlist, ylist,
		      NULL, 0, &nverts, &verts) != TCL_OK) {
    return TCL_ERROR;
  }
    
  if (p->texcoords) free(p->texcoords);  
  p->texcoords = verts;
  p->ntexcoords = nverts;
  
  update_vbo(p, POLY_TEXCOORDS_VBO);
  
  return(TCL_OK);
}



static int polycolorCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  double r, g, b, a;
  int id;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " polygon ?r g b ?a??", NULL);
    return TCL_ERROR;
  }
  
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], PolygonID, "polygon")) < 0)
    return TCL_ERROR;
  p = GR_CLIENTDATA(OL_OBJ(olist,id));
  
  /* Getter: return current color */
  if (argc == 2) {
    char result[128];
    snprintf(result, sizeof(result), "%.6g %.6g %.6g %.6g", 
             p->color[0], p->color[1], p->color[2], p->color[3]);
    Tcl_SetResult(interp, result, TCL_VOLATILE);
    return TCL_OK;
  }
  
  /* Setter: need at least r g b */
  if (argc < 5) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " polygon ?r g b ?a??", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetDouble(interp, argv[2], &r) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &g) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &b) != TCL_OK) return TCL_ERROR;
  if (argc > 5) {
    if (Tcl_GetDouble(interp, argv[5], &a) != TCL_OK) return TCL_ERROR;
  }
  else {
    a = 1.0;
  }
  p->color[0] = r;
  p->color[1] = g;
  p->color[2] = b;
  p->color[3] = a;
  return(TCL_OK);
}

static int polyfillCmd(ClientData clientData, Tcl_Interp *interp,
		       int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int fill, id;
  double linewidth;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " polygon fill? linewidth", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], PolygonID, "polygon")) < 0)
    return TCL_ERROR;
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &fill) != TCL_OK) return TCL_ERROR;
  p->filled = fill;
  if (!p->filled) p->type = GL_LINE_LOOP;

  if (argc > 3) {
    if (Tcl_GetDouble(interp, argv[3], &linewidth) != TCL_OK) return TCL_ERROR;
    p->linewidth = linewidth;
  }
  return(TCL_OK);
}

static int polylinewidthCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int id;
  double width;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " polygon ?linewidth?", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], PolygonID, "polygon")) < 0)
    return TCL_ERROR;
  p = GR_CLIENTDATA(OL_OBJ(olist,id));
  
  /* Getter */
  if (argc == 2) {
    char result[32];
    snprintf(result, sizeof(result), "%.6g", p->linewidth);
    Tcl_SetResult(interp, result, TCL_VOLATILE);
    return TCL_OK;
  }
  
  /* Setter */
  if (Tcl_GetDouble(interp, argv[2], &width) != TCL_OK) return TCL_ERROR;
  p->linewidth = width;
  return(TCL_OK);
}

static int polytypeCmd(ClientData clientData, Tcl_Interp *interp,
		       int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int id;
  double size;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " polygon ?type?", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist),
			 argv[1], PolygonID, "polygon")) < 0)
    return TCL_ERROR;
  p = GR_CLIENTDATA(OL_OBJ(olist,id));
  
  /* Getter */
  if (argc == 2) {
    const char *type_str;
    switch (p->type) {
      case GL_TRIANGLES:     type_str = "triangles"; break;
      case GL_TRIANGLE_STRIP: type_str = "triangle_strip"; break;
      case GL_TRIANGLE_FAN:  type_str = "triangle_fan"; break;
      case GL_LINES:         type_str = "lines"; break;
      case GL_LINE_STRIP:    type_str = "line_strip"; break;
      case GL_LINE_LOOP:     type_str = "line_loop"; break;
      case GL_POINTS:        type_str = "points"; break;
      default:               type_str = "unknown"; break;
    }
    Tcl_SetResult(interp, (char *)type_str, TCL_STATIC);
    return TCL_OK;
  }

  if (!strcmp(argv[2], "quads") || !strcmp(argv[2], "QUADS")) {
    Tcl_AppendResult(interp, argv[0], ": QUADS no longer supported", NULL);
    return TCL_ERROR;
  }
  
  if (!strcmp(argv[2], "polygon") || !strcmp(argv[2], "POLYGON")) {
    p->filled = 1;
    p->type = GL_TRIANGLE_FAN;
  }
  else if (!strcmp(argv[2], "triangles") || !strcmp(argv[2], "TRIANGLES")) {
    p->filled = 1;
    p->type = GL_TRIANGLES;
  }
  else if (!strcmp(argv[2], "triangle_strip") || 
	   !strcmp(argv[2], "TRIANGLE_STRIP")) {
    p->filled = 1;
    p->type = GL_TRIANGLE_STRIP;
  }
  else if (!strcmp(argv[2], "triangle_fan") || 
	   !strcmp(argv[2], "TRIANGLE_FAN")) {
    p->filled = 1;
    p->type = GL_TRIANGLE_FAN;
  }
  else if (!strcmp(argv[2], "lines") || !strcmp(argv[2], "LINES")) {
    p->filled = 0;
    p->type = GL_LINES;
  }
  else if (!strcmp(argv[2], "line_strip") || !strcmp(argv[2], "LINE_STRIP")) {
    p->filled = 0;
    p->type = GL_LINE_STRIP;
  }
  else if (!strcmp(argv[2], "line_loop") || !strcmp(argv[2], "LINE_LOOP")) {
    p->filled = 0;
    p->type = GL_LINE_LOOP;
  }
  else if (!strcmp(argv[2], "points") || !strcmp(argv[2], "POINTS")) {
    p->filled = 0;
    p->type = GL_POINTS;
    size = 1.0;
    p->pointsize = size;
    p->circ = 2;
    p->filled = 1;
  }

  return(TCL_OK);
}

static int polypointsizeCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int id;
  double size;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " polygon ?pointsize?", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], PolygonID, "polygon")) < 0)
    return TCL_ERROR;
  p = GR_CLIENTDATA(OL_OBJ(olist,id));
  
  /* Getter */
  if (argc == 2) {
    char result[32];
    snprintf(result, sizeof(result), "%.6g", p->pointsize);
    Tcl_SetResult(interp, result, TCL_VOLATILE);
    return TCL_OK;
  }
  
  /* Setter */
  if (Tcl_GetDouble(interp, argv[2], &size) != TCL_OK) return TCL_ERROR;
  p->pointsize = size;
  return(TCL_OK);
}

int polygonShaderCreate(Tcl_Interp *interp)
{
  PolygonShaderProg = (SHADER_PROG *) calloc(1, sizeof(SHADER_PROG));

  const char* vertex_shader =
  #ifndef STIM2_USE_GLES
    "# version 330\n"
  #else
    "# version 300 es\n"  
  #endif
    "in vec3 vertex_position;"
    "in vec2 vertex_texcoord;"
    "out vec2 texcoord;"
    "uniform mat4 projMat;"
    "uniform mat4 modelviewMat;"
    "uniform float pointSize;"
    
    "void main () {"
    " gl_PointSize = pointSize;"
    " texcoord = vertex_texcoord;"
    " gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);"
    "}";

  const char* fragment_shader =
  #ifndef STIM2_USE_GLES
    "# version 330\n"
  #else
    "# version 300 es\n"  
  #endif
  
    "#ifdef GL_ES\n"
    "precision mediump float;"
    "precision mediump int;\n"
    "#endif\n"

    "uniform vec4 uColor;"
    "uniform int circle;"
    "uniform float mouthHalf;"   /* sector half-mouth, radians; <=0 => no wedge */
    "uniform float innerRad;"    /* annulus inner radius, uv units; <=0 => solid */
    "in vec2 texcoord;"
    "out vec4 frag_color;"
    "void main () {"
    " if (circle == 0) { frag_color = uColor; return; }"
    " if (circle == 2) {"            /* point-sprite disc */
    "   vec2 coord = gl_PointCoord - vec2(0.5);"
    "   float t = 1.0 - smoothstep(0.4, 0.5, length(coord));"
    "   frag_color = vec4(uColor.rgb, uColor.a*t);"
    "   return;"
    " }"
    /* circle == 1 : anti-aliased round mask -- disc, annulus, sector (pac-man),
       or arc band, depending on innerRad/mouthHalf. The mouth is centred on +X;
       rotate the object to aim it. */
    /* edge softness in uv units (quad is 1 wide). NB: do NOT use fwidth() here
       -- the unit quad is two triangles and the texcoord gradient differs across
       their shared diagonal, so fwidth() jumps there and paints a faint diagonal
       seam through the shape. A constant width is scale-proportional and seam-free. */
    " float aa = 0.012;"
    " vec2 uv = texcoord - vec2(0.5);"
    " float r = length(uv);"
    " float alpha = 1.0 - smoothstep(0.5 - aa, 0.5, r);"           /* outer rim */
    " if (innerRad > 0.0) alpha *= smoothstep(innerRad - aa, innerRad + aa, r);"
    " if (mouthHalf > 0.0) {"                                       /* cut wedge */
    "   float ang = atan(uv.y, uv.x);"
    "   float aaA = aa / max(r, aa);"      /* ~constant linear softness on the wedge edges */
    "   alpha *= smoothstep(-aaA, aaA, abs(ang) - mouthHalf);"
    " }"
    " if (alpha <= 0.0) discard;"
    " frag_color = vec4(uColor.rgb, uColor.a * alpha);"
    "}";
  
  if (build_prog(PolygonShaderProg, vertex_shader, fragment_shader, 0) == -1) {



    Tcl_AppendResult(interp, "polygon : error building polygon shader", NULL);
    return TCL_ERROR;
  }

  /* Now add uniforms into master table */
  Tcl_InitHashTable(&PolygonShaderProg->uniformTable, TCL_STRING_KEYS);
  add_uniforms_to_table(&PolygonShaderProg->uniformTable, PolygonShaderProg);

  /* Now add attribs into master table */
  Tcl_InitHashTable(&PolygonShaderProg->attribTable, TCL_STRING_KEYS);
  add_attribs_to_table(&PolygonShaderProg->attribTable, PolygonShaderProg);

  return TCL_OK;
}

#ifdef _WIN32
EXPORT(int, Polygon_Init) (Tcl_Interp *interp)
#else
int Polygon_Init(Tcl_Interp *interp)
#endif
{
  OBJ_LIST *OBJList = getOBJList();
  
  if (
#ifdef USE_TCL_STUBS
      Tcl_InitStubs(interp, "8.5-", 0)
#else
      Tcl_PkgRequire(interp, "Tcl", "8.5-", 0)
#endif
      == NULL) {
    return TCL_ERROR;
  }
  
  if (PolygonID < 0) PolygonID = gobjRegisterType("polygon");

  gladLoadGL();
    
  polygonShaderCreate(interp);

  Tcl_CreateCommand(interp, "polygon", (Tcl_CmdProc *) polygonCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polyverts", (Tcl_CmdProc *) polyvertsCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polytexcoords", (Tcl_CmdProc *) polytexcoordsCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polycolor", (Tcl_CmdProc *) polycolorCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polycirc", (Tcl_CmdProc *) polycircCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polysector", (Tcl_CmdProc *) polysectorCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polyannulus", (Tcl_CmdProc *) polyannulusCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polyfill", (Tcl_CmdProc *) polyfillCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polylinewidth", (Tcl_CmdProc *) polylinewidthCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polytype", (Tcl_CmdProc *) polytypeCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polypointsize", (Tcl_CmdProc *) polypointsizeCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  return TCL_OK;
}

#ifdef WIN32
BOOL APIENTRY
DllEntryPoint(hInst, reason, reserved)
    HINSTANCE hInst;
    DWORD reason;
    LPVOID reserved;
{
	return TRUE;
}
#endif
