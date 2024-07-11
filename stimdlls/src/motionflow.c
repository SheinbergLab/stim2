/*
 * motionflow.c
 *  Module to show a flowfield of moving dots based on series of flow fields
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

#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <utilc.h>

#include "glsw.h"

#include <utilc.h>
#include <prmutil.h>

/* If you want access to dlsh connectivity, include these */
#include "df.h"
#include "tcl_dl.h"

#include <stim2.h>		/* Stim header      */

#if !defined(PI)
#define PI 3.1415926
#define TWO_PI 6.283185307
#endif

typedef struct _dot {
  float pos[3];
  int lifetime, frames;
} DOT;

typedef struct _flowfield {
  int field_width;
  int field_height;
  float *field_dx;
  float *field_dy;
} FLOWFIELD;

typedef struct _vao_info {
  GLuint vao;
  int narrays;
  int nindices;
  int npoints;
  GLfloat *points;
  GLuint points_vbo;
} VAO_INFO;

typedef enum MASK_TYPE { MASK_NONE, MASK_LAST }
  MASK_TYPE;

typedef struct {
  DOT *dots;
  int num_dots;
  MASK_TYPE mask_type;		/* MASK_NONE */
  float mask_radius;
  float color[4];
  float pointsize;
  int lifetime;
  int loop;
  VAO_INFO *vao_info;		/* to track vertex attributes */
  float field_framerate;
  int field_interpolate;	/* smooth transition between fields? */
  int field_nframes;
  FLOWFIELD *fields;
  int field_curframe;
  int field_lastframe;
  float field_duration;
} MOTIONFLOW;

static char shaderPath[MAX_PATH];	/* where to find shaders */
static int MotionflowID = -1;	/* unique object id */

void motionflowDraw(GR_OBJ *g) 
{
  MOTIONFLOW *s = (MOTIONFLOW *) GR_CLIENTDATA(g);
  DOT *dots = s->dots;

  glPushMatrix();
  glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_LIGHTING);
  glEnable(GL_POINT_SMOOTH);
  glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
  glPointSize(s->pointsize);

  glColor4fv(s->color);

  if (s->vao_info->narrays) {
    glBindVertexArray(s->vao_info->vao);
    glDrawArrays(GL_POINTS, 0, s->vao_info->nindices);
  }
  
  glPopAttrib();
  glPopMatrix();
}

static void delete_vao_info(VAO_INFO *vinfo)
{
  if (vinfo->npoints) {
    glDeleteBuffers(1, &vinfo->points_vbo);
    free(vinfo->points);
  }
  glDeleteVertexArrays(1, &vinfo->vao);
}


static void free_field(FLOWFIELD *f)
{
  free(f->field_dx);
  free(f->field_dy);
}

void motionflowDelete(GR_OBJ *g) 
{
  int i;
  MOTIONFLOW *s = (MOTIONFLOW *) GR_CLIENTDATA(g);
  if (s->dots) free(s->dots);

  for (i = 0; i < s->field_nframes; i++) {
    free_field(&s->fields[i]);
  }
  free(s->fields);
  
  delete_vao_info(s->vao_info);
  free((void *) s);
}

void motionflowUpdate(GR_OBJ *g) 
{
  MOTIONFLOW *s = (MOTIONFLOW *) GR_CLIENTDATA(g);
  int i, n = 0;
  int x_ind, y_ind, ii;
  float aspect, aa;
  float vx, vy;
  GLfloat *points = s->vao_info->points;
  float frame_duration = getFrameDuration();
  float curtime, curframe;
  float time_scaler, space_scaler;
  int f0, f1;
  float prop_f0, prop_f1;

  /* what time during playback are we, in ms? */
  //	    curtime = s->field_curframe*frame_duration;
  curtime = getStimTime();
  if (!s->loop && curtime > s->field_duration) {
    s->vao_info->nindices = n/3;
    glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->points_vbo);
    glBufferData(GL_ARRAY_BUFFER, n*sizeof(GLfloat),
		 s->vao_info->points, GL_STATIC_DRAW);
    return;
  }
  
  for (i = 0; i < s->num_dots; i++) {
    if (s->dots[i].frames >= s->dots[i].lifetime) {
      s->dots[i].pos[0] = frand()-0.5;
      s->dots[i].pos[1] = frand()-0.5;
      s->dots[i].frames = 0;
    }
    else {
      if (s->field_nframes >= 1) {
	aspect = s->fields[0].field_width/(float) s->fields[0].field_height;
	aa = 0.5/aspect;
	if (s->dots[i].pos[1] > aa ||
	    s->dots[i].pos[1] < -aa) {
	  vx = vy = 0;
	}
	else {
	  /* Find corresponding indices into flow field matrices */
	  x_ind = (s->dots[i].pos[0]+.5)*s->fields[0].field_width;
	  if (x_ind >= s->fields[0].field_width) x_ind = s->fields[0].field_width-1;
	  else if (x_ind < 0) x_ind = 0;
	  
	  y_ind = s->fields[0].field_height-((s->dots[i].pos[1]+aa)*s->fields[0].field_width)-1;
	  if (y_ind < 0) y_ind = 0;
	  else if (y_ind >= s->fields[0].field_height) y_ind = s->fields[0].field_height-1;
	  
	  ii = y_ind*s->fields[0].field_width+x_ind;
	  time_scaler = (frame_duration*s->field_framerate*.001);
	  space_scaler = (1.0/s->fields[0].field_width)*GR_SX(g);

	  if (s->field_nframes == 1) {
	    vx = s->fields[0].field_dx[ii]*time_scaler*space_scaler;
	    vy = s->fields[0].field_dy[ii]*time_scaler*space_scaler;
	  }
	  else {
	    /* which frame is that? */
	    curframe = curtime*(s->field_framerate*.001);

	    /* likely between frames f0 and f1 */
	    f0 = ((int) curframe) % s->field_nframes;
	    f1 = f0+1;
	    if (f1 >= s->field_nframes) f1 = 0;
#if DEBUG
	    if (s->field_lastframe != f0) {
	      fprintf(getConsoleFP(), "%d: %.4f %.4f [%d]\n", f0, curtime, curframe, getStimTime());
	    }
#endif	    
	    s->field_lastframe = f0;

	    if (s->field_interpolate) {
	      prop_f1 = curframe-(int)curframe;
	    }
	    else {
	      prop_f1 = 0.0;
	    }
	    prop_f0 = 1.0-prop_f1;

	    vx = s->fields[f0].field_dx[ii]*time_scaler*space_scaler * prop_f0;
	    vx += s->fields[f1].field_dx[ii]*time_scaler*space_scaler * prop_f1;

	    vy = s->fields[f0].field_dy[ii]*time_scaler*space_scaler * prop_f0;
	    vy += s->fields[f1].field_dy[ii]*time_scaler*space_scaler * prop_f1;
	  }
	  
	  s->dots[i].pos[0] += vx;
	  s->dots[i].pos[1] += vy;
	}
	s->dots[i].frames++;
      }
    }
    if (s->mask_type == MASK_NONE) {
      if (s->dots[i].pos[1] < aa &&
	  s->dots[i].pos[1] > -aa &&
	  s->dots[i].pos[0] > -0.5 &&
	  s->dots[i].pos[0] < 0.5) {
	/* Make sure dot is within flow field dimensions */
	*points++ = s->dots[i].pos[0];
	*points++ = s->dots[i].pos[1];
	*points++ = s->dots[i].pos[2];
	n+=3;
      }
    }
  }

  s->field_curframe++;
  
  s->vao_info->nindices = n/3;
  glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->points_vbo);
  glBufferData(GL_ARRAY_BUFFER, n*sizeof(GLfloat),
	       s->vao_info->points, GL_STATIC_DRAW);
}

static int setPositions(MOTIONFLOW *s)
{
  int i;
  for (i = 0; i < s->num_dots; i++) {
    s->dots[i].pos[0] = frand()-0.5;
    s->dots[i].pos[1] = frand()-0.5;
    s->dots[i].pos[2] = 0;
  }
  return TCL_OK;
}


static int setLifetimes(MOTIONFLOW *s, int lifetime)
{
  int i;
  for (i = 0; i < s->num_dots; i++) {
    s->dots[i].lifetime = lifetime;
    s->dots[i].frames = rand()%lifetime;
  }
  return TCL_OK;
}

int motionflowCreate(OBJ_LIST *objlist, int n, int lifetime)
{
  const char *name = "Motionflow";
  GR_OBJ *obj;
  MOTIONFLOW *s;
  
  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = MotionflowID;

  GR_ACTIONFUNC(obj) = motionflowDraw;
  GR_DELETEFUNC(obj) = motionflowDelete;
  GR_UPDATEFUNC(obj) = motionflowUpdate;

  s = (MOTIONFLOW *) calloc(1, sizeof(MOTIONFLOW));
  GR_CLIENTDATA(obj) = s;

  /* Default to white */
  s->color[0] = s->color[1] = s->color[2] = s->color[3] = 1.;
  s->pointsize = 1.0;
    
  s->num_dots = n;
  s->dots = (DOT *) calloc(n, sizeof(DOT));
  s->lifetime = lifetime;
  setPositions(s);
  setLifetimes(s, lifetime);
  s->loop = 0;
  
  s->mask_type = MASK_NONE;
  s->mask_radius = 0.5;

  /* No default flow fields */
  s->field_nframes = 0;
  
  /* Create vertex array object to hold buffer of verts to send to shader */
  s->vao_info = (VAO_INFO *) calloc(1, sizeof(VAO_INFO));
  s->vao_info->narrays = 1;
  glGenVertexArrays(1, &s->vao_info->vao);
  glBindVertexArray(s->vao_info->vao);

  s->vao_info->npoints = s->num_dots;
  s->vao_info->points = 
    (GLfloat *) calloc(s->vao_info->npoints*3, sizeof(GLfloat));
  
  glGenBuffers(1, &s->vao_info->points_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->points_vbo);
  glBufferData(GL_ARRAY_BUFFER, s->vao_info->npoints*3*sizeof(GLfloat),
	       s->vao_info->points, GL_STATIC_DRAW);
  
  
  glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->points_vbo);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
  glEnableVertexAttribArray(0);
  s->vao_info->nindices = s->num_dots;
  s->vao_info->narrays++;
  
  return(gobjAddObj(objlist, obj));
}


static int motionflowCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id, n, lifetime;

  int verbose = 1;
  
  if (argc < 3) {
    interp->result = "usage: motionflow n lifetime";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &n) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &lifetime) != TCL_OK) return TCL_ERROR;

  if ((id = motionflowCreate(olist, n, lifetime)) < 0) {
    sprintf(interp->result, "error creating motionflow");
    return(TCL_ERROR);
  }
  
  sprintf(interp->result,"%d", id);
  return(TCL_OK);
}


static void add_field(FLOWFIELD *f, int w, int h, float *xv, float *yv)
{
  int nbytes;
  f->field_width = w;
  f->field_height = h;
  nbytes = w*h*sizeof(float);
  f->field_dx = (float *) malloc(nbytes);
  f->field_dy = (float *) malloc(nbytes);
  memcpy(f->field_dx, xv, nbytes);
  memcpy(f->field_dy, yv, nbytes);
  return;
}
		      
static void fill_sample_field(FLOWFIELD *f, int w, int h, float *fillvals)
{  
  int i, j, half_w, half_h;

  f->field_width = w;
  f->field_height = h;
  f->field_dx = (float *) calloc(w*h, sizeof(float));
  f->field_dy = (float *) calloc(w*h, sizeof(float));

  half_h = h/2;
  half_w = w/2;
  for (i = 0; i < h; i++) {
    for (j = 0; j < w; j++) {
      if (i < half_h && j < half_w) {
	f->field_dx[i*w+j] = fillvals[0];
	f->field_dy[i*w+j] = fillvals[1];
      }
      else if (i >= half_h && j < half_w) {
	f->field_dx[i*w+j] = fillvals[2];
	f->field_dy[i*w+j] = fillvals[3];
      }
      else if (i < half_h && j >= half_w) {
	f->field_dx[i*w+j] = fillvals[4];
	f->field_dy[i*w+j] = fillvals[5];
      }
      else {
	f->field_dx[i*w+j] = fillvals[6];
	f->field_dy[i*w+j] = fillvals[7];
      }
    }
  }
}

static int motionflowSetFieldsCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONFLOW *s;
  int id;
  int w, h;
  int i, j;
  DYN_LIST *fields, *dimlist, *xlist, *ylist, *field_specs;
  int loop = 0, add_blank = 1;

  //  float fillvals[8];
  
  if (argc < 3) {
    interp->result = "usage: motionflow_pointsize motionflow fieldlist [loop=0] [add_blank=1]";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionflow object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionflowID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type motionflow", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[2], &fields) != TCL_OK)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (argc > 3)
    if (Tcl_GetInt(interp, argv[3], &loop) != TCL_OK) return TCL_ERROR;

  if (argc > 4)
    if (Tcl_GetInt(interp, argv[4], &add_blank) != TCL_OK) return TCL_ERROR;

  //  s->field_nframes = 2;

  s->field_nframes = DYN_LIST_N(fields)+add_blank;
  
  s->field_curframe = 0;
  s->field_interpolate = 1;
  s->field_framerate = 25.0;
  s->fields = calloc(s->field_nframes, sizeof(FLOWFIELD));
  s->loop = loop;
  
  if (add_blank) {
    field_specs = ((DYN_LIST **) DYN_LIST_VALS(fields))[0];
    dimlist = ((DYN_LIST **) DYN_LIST_VALS(field_specs))[0];
    w = s->fields[0].field_width =  ((int *) DYN_LIST_VALS(dimlist))[0];
    h = s->fields[0].field_height = ((int *) DYN_LIST_VALS(dimlist))[1];
    s->fields[0].field_dx = (float *) calloc(w*h, sizeof(float));
    s->fields[0].field_dy = (float *) calloc(w*h, sizeof(float));
  }
  
  for (i = 0, j = add_blank; j < s->field_nframes; i++, j++) {
    field_specs = ((DYN_LIST **) DYN_LIST_VALS(fields))[i];
    dimlist = ((DYN_LIST **) DYN_LIST_VALS(field_specs))[0];
    xlist = ((DYN_LIST **) DYN_LIST_VALS(field_specs))[1];
    ylist = ((DYN_LIST **) DYN_LIST_VALS(field_specs))[2];
    add_field(&s->fields[j],
	      ((int *) DYN_LIST_VALS(dimlist))[0],
	      ((int *) DYN_LIST_VALS(dimlist))[1],
	      (float *) DYN_LIST_VALS(xlist),
	      (float *) DYN_LIST_VALS(ylist));
  }
  s->field_duration = (1000./s->field_framerate)*s->field_nframes;
  
#ifdef DEBUG
  fprintf(getConsoleFP(), "%f\n", s->field_duration);
  fillvals[0] = -3; fillvals[1] = -3;  fillvals[2] = 3.; fillvals[3] = -3;
  fillvals[4] = -3; fillvals[5] = 3;   fillvals[6] = 3.; fillvals[7] = 3;
  fill_sample_field(&s->fields[0], w, h, fillvals);

  fillvals[0] = -3; fillvals[1] = 3;  fillvals[2] = 3.; fillvals[3] = 3;
  fillvals[4] = -3; fillvals[5] = -3;   fillvals[6] = 3.; fillvals[7] = -3;
  fill_sample_field(&s->fields[1], w, h, fillvals);
#endif

  return(TCL_OK);
}

static int motionflowPointsizeCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONFLOW *s;
  int id;
  double pointsize;
  
  if (argc < 3) {
    interp->result = "usage: motionflow_pointsize motionflow pointsize";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionflow object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionflowID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type motionflow", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &pointsize) != TCL_OK) return TCL_ERROR;
  s->pointsize = pointsize;

  return(TCL_OK);
}


static int motionflowMaskTypeCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONFLOW *s;
  int id;
  int type;
  
  if (argc < 3) {
    interp->result = "usage: motionflow_masktype motionflow type";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionflow object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionflowID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type motionflow", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &type) != TCL_OK) return TCL_ERROR;
  if (type < 0 || type >= MASK_LAST) {
    Tcl_AppendResult(interp, argv[0], ": invalid mask type specified", NULL);
    return TCL_ERROR;
  }
  s->mask_type = type;

  return(TCL_OK);
}

static int motionflowMaskRadiusCmd(ClientData clientData, Tcl_Interp *interp,
				    int argc, char *argv[])
{
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONFLOW *s;
  int id;
  double radius;
  
  if (argc < 3) {
    interp->result = "usage: motionflow_maskradius motionflow radius";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionflow object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionflowID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type motionflow", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &radius) != TCL_OK) return TCL_ERROR;
  s->mask_radius = radius;

  return(TCL_OK);
}


static int motionflowColorCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
 MOTIONFLOW *s;
  double r, g, b, a;
  int id;

  if (argc < 5) {
    interp->result = "usage: motionflow_color motionflow r g b ?a?";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a polygon object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionflowID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type motionflow", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &r) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &g) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &b) != TCL_OK) return TCL_ERROR;
  if (argc > 5) {
    if (Tcl_GetDouble(interp, argv[5], &a) != TCL_OK) return TCL_ERROR;
  }
  else {
    a = 1.0;
  }

  s->color[0] = r;
  s->color[1] = g;
  s->color[2] = b;
  s->color[3] = a;

  return(TCL_OK);
}

EXPORT(int,Motionflow_Init) _ANSI_ARGS_((Tcl_Interp *interp))
{
  OBJ_LIST *OBJList = getOBJList();

  if (
#ifdef USE_TCL_STUBS
      Tcl_InitStubs(interp, "8.5", 0)
#else
      Tcl_PkgRequire(interp, "Tcl", "8.5", 0)
#endif
      == NULL) {
    return TCL_ERROR;
  }
  
  glewInit();

  if (GLEW_ARB_vertex_shader && 
      GLEW_ARB_fragment_shader &&
      GLEW_ARB_shading_language_100) {
  }
  else {
    Tcl_SetResult(interp, "shader: no GLSL support", TCL_STATIC);
    return TCL_ERROR;
  }


  if (MotionflowID < 0) MotionflowID = gobjRegisterType();

  Tcl_CreateCommand(interp, "motionflow", (Tcl_CmdProc *) motionflowCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionflow_pointsize", 
		    (Tcl_CmdProc *) motionflowPointsizeCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionflow_setfields", 
		    (Tcl_CmdProc *) motionflowSetFieldsCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionflow_masktype", 
		    (Tcl_CmdProc *) motionflowMaskTypeCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionflow_maskradius", 
		    (Tcl_CmdProc *) motionflowMaskRadiusCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionflow_color", 
		    (Tcl_CmdProc *) motionflowColorCmd, 
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
