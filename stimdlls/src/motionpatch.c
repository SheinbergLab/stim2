/*
 * motionpatch.c
 *  Module to show a flowfield of moving dots
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

#include "open-simplex-noise.h"

#include <tcl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h> 

#include <utilc.h>
#include <prmutil.h>

/* If you want access to dlsh connectivity, include these */
#include "df.h"
#include "tcl_dl.h"

#include <stim2.h>		/* Stim header      */
#include "shaderutils.h"

#if !defined(PI)
#define PI 3.1415926
#define TWO_PI 6.283185307
#endif

typedef struct _dot {
  float pos[3];
  float speed[3];
  int lifetime, frames;
  int coherent;			/* flag as either coherent or not */
} DOT;

typedef struct _vao_info {
  GLuint vao;
  int narrays;
  int nindices;
  int npoints;
  GLfloat *points;
  GLuint points_vbo;
  int ntexcoords;
  GLfloat *texcoords;
  GLuint texcoords_vbo;
} VAO_INFO;

typedef enum MASK_TYPE { MASK_NONE, MASK_CIRCLE, MASK_HEXAGON, MASK_LAST }
  MASK_TYPE;

typedef enum SAMPLER_MASK_TYPE { SMASK_NONE, SMASK_ALPHA, SMASK_ONE_MINUS_ALPHA, SMASK_TWO_COLOR, SMASK_LAST }
  SAMPLER_MASK_TYPE;


#define MAX_NOISE_CTX 4
#define NSAMPLERS 1

typedef struct {
  DOT *dots;
  int num_dots;
  MASK_TYPE mask_type;		/* MASK_NONE, MASK_CIRCLE, MASK_HEXAGON */
  float mask_radius;
  float coherence;
  float color1[4];
  float color2[4];
  float pointsize;
  float speed;
  float direction;
  int   samplermaskmode;
  int lifetime;
  VAO_INFO *vao_info;		/* to track vertex attributes */
  int64_t noise_seed[MAX_NOISE_CTX];	/* for noise generation       */
  struct osn_context *ctx[MAX_NOISE_CTX];   /* context for noise funcs    */
  float noise_period;           /* for x and y                */
  float noise_z;		/* current z                  */
  int   noise_update_z;		/* update based on StimTime   */
  float noise_update_rate;	/* for the z variable         */
  int set_direction_by_noise;

  GLuint       texid[NSAMPLERS]; /* To use as a mask for the dots */
  UNIFORM_INFO *tex0;		 /* Texture samples to share      */
  
  UNIFORM_INFO *modelviewMat;   /* set if we have "modelviewMat" uniform */
  UNIFORM_INFO *projMat;        /* set if we have "projMat" uniform */
  UNIFORM_INFO *uColor1;        /* set if we have "uColor" uniform */
  UNIFORM_INFO *uColor2;        /* set if we have "uColor" uniform */
  UNIFORM_INFO *pointSize;
  UNIFORM_INFO *samplerMaskMode;/* 0: ignore, 1: use alpha, 2: use 1-alpha */
  
  SHADER_PROG *program;
  Tcl_HashTable uniformTable;	/* local unique version */
  Tcl_HashTable attribTable;	/* local unique version */

} MOTIONPATCH;

static int MotionpatchID = -1;	/* unique object id */
SHADER_PROG *MotionpatchShaderProg = NULL;

void motionpatchDraw(GR_OBJ *g) 
{
  MOTIONPATCH *s = (MOTIONPATCH *) GR_CLIENTDATA(g);
  SHADER_PROG *sp = (SHADER_PROG *) s->program;
  float *v;

  /* Update uniform table */
  if (s->modelviewMat) {
    v = (float *) s->modelviewMat->val;
    stimGetMatrix(STIM_MODELVIEW_MATRIX, v);
  }
  if (s->projMat) {
    v = (float *) s->projMat->val;
    stimGetMatrix(STIM_PROJECTION_MATRIX, v);
  }
  if (s->uColor1) {
    v = (float *) s->uColor1->val;
    v[0] = s->color1[0];
    v[1] = s->color1[1];
    v[2] = s->color1[2];
    v[3] = s->color1[3];
  }
  if (s->uColor2) {
    v = (float *) s->uColor2->val;
    v[0] = s->color2[0];
    v[1] = s->color2[1];
    v[2] = s->color2[2];
    v[3] = s->color2[3];
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  if (s->pointSize) {
    glEnable(GL_PROGRAM_POINT_SIZE);
    memcpy(s->pointSize->val, &s->pointsize, sizeof(float));
  }

  if (s->samplerMaskMode) {
    /* so shader knows how to deal with the sampler mask */
    memcpy(s->samplerMaskMode->val, &s->samplermaskmode, sizeof(int));
  }
    
  glUseProgram(sp->program);
  update_uniforms(&s->uniformTable);


  /* bind associated texture to a shader sampler if associated */
  if (s->texid[0] >= 0 && s->tex0) {
    glActiveTexture(GL_TEXTURE0);
    switch(s->tex0->type) {
    case GL_SAMPLER_2D:
      glBindTexture(GL_TEXTURE_2D, s->texid[0]);
      break;
    case GL_SAMPLER_2D_ARRAY:
      glBindTexture(GL_TEXTURE_2D_ARRAY, s->texid[0]);
      break;
    }
  }

  
  if (s->vao_info->narrays) {
    glBindVertexArray(s->vao_info->vao);
    glDrawArrays(GL_POINTS, 0, s->vao_info->nindices);
  }
  glUseProgram(0);
}

static void delete_vao_info(VAO_INFO *vinfo)
{
  if (vinfo->npoints) {
    glDeleteBuffers(1, &vinfo->points_vbo);
    free(vinfo->points);
  }
  if (vinfo->ntexcoords) {
    glDeleteBuffers(1, &vinfo->texcoords_vbo);
    free(vinfo->texcoords);
  }
  glDeleteVertexArrays(1, &vinfo->vao);
}


void motionpatchDelete(GR_OBJ *g) 
{
  int i;
  MOTIONPATCH *s = (MOTIONPATCH *) GR_CLIENTDATA(g);
  if (s->dots) free(s->dots);
  for (i = 0; i < MAX_NOISE_CTX; i++) {
    if (s->ctx[i]) open_simplex_noise_free(s->ctx[i]);
  }
  delete_vao_info(s->vao_info);
  free((void *) s);
}

int inHexagon(float in_x, float in_y)
{
  float x, y, l2, px, py;
  x = in_x*2;
  y = in_y*2;
  l2 = x * x + y * y;
  if (l2 > 1.0f) return 0;
  if (l2 < 0.75f) return 1;
  // Check against borders
  px = x * 1.15470053838f; // 2/sqrt(3)
  if (px > 1.0f || px < -1.0f) return 0;
  py = 0.5f * px + y;
  if (py > 1.0f || py < -1.0f) return 0;
  if (px - py > 1.0f || px - py < -1.0f) return 0;
  return 1;
}

void motionpatchUpdate(GR_OBJ *g) 
{
  MOTIONPATCH *s = (MOTIONPATCH *) GR_CLIENTDATA(g);
  int i, npoints = 0, ntexcoords = 0;
  float vx, vy;
  GLfloat *points = s->vao_info->points;
  GLfloat *texcoords = s->vao_info->texcoords;
  float r2;
  float value1, value2;
  
  
  if (s->mask_type == MASK_CIRCLE)
    r2 = s->mask_radius*s->mask_radius; 
    
  if (s->noise_update_z)
    s->noise_z = getStimTime()/1000. * s->noise_update_rate;

  for (i = 0; i < s->num_dots; i++) {
    if (s->dots[i].lifetime >= 0 &&
	s->dots[i].frames >= s->dots[i].lifetime) {
      s->dots[i].pos[0] = frand()-0.5;
      s->dots[i].pos[1] = frand()-0.5;
      s->dots[i].frames = 0;

      if (s->set_direction_by_noise) {
	value1 = open_simplex_noise3(s->ctx[0],
				     s->dots[i].pos[0] * s->noise_period,
				     s->dots[i].pos[1] * s->noise_period,
				     s->noise_z);
	value2 = open_simplex_noise3(s->ctx[1],
				     s->dots[i].pos[0] * s->noise_period,
				     s->dots[i].pos[1] * s->noise_period,
				     s->noise_z);
	s->direction = atan2(value2, value1);
      }
      
      /* If this dot is coherent, set motion direction */
      if (s->dots[i].coherent) {
	s->dots[i].speed[0] = cos(s->direction)*s->speed;
	s->dots[i].speed[1] = sin(s->direction)*s->speed;
      }
      /* If this dot is incoherent, randomize motion direction */
      else {
	float angle = frand()*2*PI;
	s->dots[i].speed[0] = cos(angle)*s->speed;
	s->dots[i].speed[1] = sin(angle)*s->speed;
      }
    }
    else {
      if (s->set_direction_by_noise) {
	value1 = open_simplex_noise3(s->ctx[0],
				     s->dots[i].pos[0] * s->noise_period,
				     s->dots[i].pos[1] * s->noise_period,
				     s->noise_z);
	value2 = open_simplex_noise3(s->ctx[1],
				     s->dots[i].pos[0] * s->noise_period,
				     s->dots[i].pos[1] * s->noise_period,
				     s->noise_z);
	s->direction = atan2(value2, value1);
      
	/* If this dot is coherent, set motion direction */
	if (s->dots[i].coherent) {
	  s->dots[i].speed[0] = cos(s->direction)*s->speed;
	  s->dots[i].speed[1] = sin(s->direction)*s->speed;
	}
	/* If this dot is incoherent, randomize motion direction */
	/**** It's possible to not update this on every frame ****/
	else {
	  float angle = frand()*2*PI;
	  s->dots[i].speed[0] = cos(angle)*s->speed;
	  s->dots[i].speed[1] = sin(angle)*s->speed;
	}
      }
      /* speed needs to take into account scale */
      vx = (s->dots[i].speed[0])/GR_SX(g);
      vy = (s->dots[i].speed[1])/GR_SY(g);
      s->dots[i].pos[0] += vx;
      s->dots[i].pos[1] += vy;
      s->dots[i].frames++;
    }
    if (s->mask_type == MASK_NONE) {
      *points++ = s->dots[i].pos[0];
      *points++ = s->dots[i].pos[1];
      *points++ = s->dots[i].pos[2];
      npoints+=3;

      *texcoords++ = s->dots[i].pos[0] + 0.5;
      *texcoords++ = s->dots[i].pos[1] + 0.5;
      ntexcoords+=2;
    }
    else if (s->mask_type == MASK_CIRCLE) {
      if ((s->dots[i].pos[0]*s->dots[i].pos[0]+
	   s->dots[i].pos[1]*s->dots[i].pos[1]) < r2) {
	*points++ = s->dots[i].pos[0];
	*points++ = s->dots[i].pos[1];
	*points++ = s->dots[i].pos[2];
	npoints+=3;

	*texcoords++ = s->dots[i].pos[0] + 0.5;
	*texcoords++ = s->dots[i].pos[1] + 0.5;
	ntexcoords+=2;
      }
    }
    else if (s->mask_type == MASK_HEXAGON) {
      if (inHexagon(s->dots[i].pos[0], s->dots[i].pos[1])) {
	*points++ = s->dots[i].pos[0];
	*points++ = s->dots[i].pos[1];
	*points++ = s->dots[i].pos[2];
	npoints+=3;
	
	*texcoords++ = s->dots[i].pos[0] + 0.5;
	*texcoords++ = s->dots[i].pos[1] + 0.5;
	ntexcoords+=2;
      }
    }
  }

  s->vao_info->nindices = npoints/3;
  glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->points_vbo);
  glBufferData(GL_ARRAY_BUFFER, npoints*sizeof(GLfloat),
	       s->vao_info->points, GL_STATIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->texcoords_vbo);
  glBufferData(GL_ARRAY_BUFFER, ntexcoords*sizeof(GLfloat),
	       s->vao_info->texcoords, GL_STATIC_DRAW);
}

static int setPositions(MOTIONPATCH *s)
{
  int i;
  for (i = 0; i < s->num_dots; i++) {
    s->dots[i].pos[0] = frand()-0.5;
    s->dots[i].pos[1] = frand()-0.5;
    s->dots[i].pos[2] = 0;
  }
  return TCL_OK;
}


static int setSpeeds(MOTIONPATCH *s, float vx, float vy)
{
  int i;
  float angle;
  for (i = 0; i < s->num_dots; i++) {
    if (s->dots[i].coherent) {
      s->dots[i].speed[0] = vx;
      s->dots[i].speed[1] = vy;
      s->dots[i].speed[2] = 0;
    }
    else {
      angle = frand()*2*PI;
      s->dots[i].speed[0] = cos(angle)*s->speed;
      s->dots[i].speed[1] = sin(angle)*s->speed;
    }
  }
  return TCL_OK;
}


static int setLifetimes(MOTIONPATCH *s, int lifetime)
{
  int i;
  for (i = 0; i < s->num_dots; i++) {
    s->dots[i].lifetime = lifetime;
    s->dots[i].frames = rand()%lifetime;
  }
  return TCL_OK;
}

static int setCoherences(MOTIONPATCH *s, float coherence)
{
  int i;
  float angle;
  for (i = 0; i < s->num_dots; i++) {
    s->dots[i].coherent = (frand() < coherence);
    if (!s->dots[i].coherent) {
      angle = frand()*2*PI;
      s->dots[i].speed[0] = cos(angle)*s->speed;
      s->dots[i].speed[1] = sin(angle)*s->speed;
    }
  }
  return TCL_OK;
}

int motionpatchCreate(OBJ_LIST *objlist, SHADER_PROG *sp,
		      int n, float speed, int lifetime)
{
  const char *name = "Motionpatch";
  GR_OBJ *obj;
  MOTIONPATCH *s;
  Tcl_HashEntry *entryPtr;

  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = MotionpatchID;

  GR_ACTIONFUNCP(obj) = motionpatchDraw;
  GR_DELETEFUNCP(obj) = motionpatchDelete;
  GR_UPDATEFUNCP(obj) = motionpatchUpdate;

  s = (MOTIONPATCH *) calloc(1, sizeof(MOTIONPATCH));
  GR_CLIENTDATA(obj) = s;

  /* Default to white */
  s->color1[0] = s->color1[1] = s->color1[2] = s->color1[3] = 1.;
  s->color2[0] = s->color2[1] = s->color2[2] = s->color2[3] = 1.;
  s->pointsize = 1.0;
  s->samplermaskmode = SMASK_NONE;
  
  s->num_dots = n;
  s->dots = (DOT *) calloc(n, sizeof(DOT));
  s->speed = speed;
  s->direction = 0.0;
  s->lifetime = lifetime;
  s->coherence = 1.0;
  setPositions(s);
  setSpeeds(s, cos(s->direction)*speed, sin(s->direction)*speed);
  setLifetimes(s, lifetime);
  setCoherences(s, s->coherence);

  s->mask_type = MASK_NONE;
  s->mask_radius = 0.5;
  
  s->noise_seed[0] = 77374;
  s->noise_seed[1] = 32452153;
  open_simplex_noise(s->noise_seed[0], &s->ctx[0]);
  open_simplex_noise(s->noise_seed[1], &s->ctx[1]);

  s->program = sp;
  copy_uniform_table(&sp->uniformTable, &s->uniformTable);
  copy_attrib_table(&sp->attribTable, &s->attribTable);

  /* Create vertex array object to hold buffer of verts to send to shader */
  s->vao_info = (VAO_INFO *) calloc(1, sizeof(VAO_INFO));
  s->vao_info->narrays = 0;

  glGenVertexArrays(1, &s->vao_info->vao);
  glBindVertexArray(s->vao_info->vao);  
  
  if ((entryPtr = Tcl_FindHashEntry(&s->attribTable, "vertex_position"))) {
    s->vao_info->npoints = s->num_dots;
    s->vao_info->points = 
      (GLfloat *) calloc(s->vao_info->npoints*3, sizeof(GLfloat));
    
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    glGenBuffers(1, &s->vao_info->points_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->points_vbo);
    glBufferData(GL_ARRAY_BUFFER, s->vao_info->npoints*3*sizeof(GLfloat),
		 s->vao_info->points, GL_STATIC_DRAW);
    glVertexAttribPointer(ainfo->location, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
    s->vao_info->nindices = s->num_dots;
    s->vao_info->narrays++;
  }

  if ((entryPtr = Tcl_FindHashEntry(&s->attribTable, "vertex_texcoord"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    s->vao_info->ntexcoords = s->num_dots;
    s->vao_info->texcoords = 
      (GLfloat *) calloc(s->vao_info->ntexcoords*2, sizeof(GLfloat));
    
    glGenBuffers(1, &s->vao_info->texcoords_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->texcoords_vbo);
    glBufferData(GL_ARRAY_BUFFER, s->vao_info->ntexcoords*sizeof(GLfloat),
		 s->vao_info->texcoords, GL_STATIC_DRAW);

    glVertexAttribPointer(ainfo->location, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
    s->vao_info->narrays++;
  }

  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "modelviewMat"))) {
    s->modelviewMat = Tcl_GetHashValue(entryPtr);
    s->modelviewMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "projMat"))) {
    s->projMat = Tcl_GetHashValue(entryPtr);
    s->projMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "tex0"))) {
    s->tex0 = Tcl_GetHashValue(entryPtr);
    s->tex0->val = malloc(sizeof(int));
    *((int *)(s->tex0->val)) = 0;
  }
  
  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "uColor1"))) {
    s->uColor1 = Tcl_GetHashValue(entryPtr);
    s->uColor1->val = malloc(sizeof(float)*4);
  }
  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "uColor2"))) {
    s->uColor2 = Tcl_GetHashValue(entryPtr);
    s->uColor2->val = malloc(sizeof(float)*4);
  }
  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "pointSize"))) {
    s->pointSize = Tcl_GetHashValue(entryPtr);
    s->pointSize->val = calloc(1,sizeof(float));
  }
  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "samplerMaskMode"))) {
    s->samplerMaskMode = Tcl_GetHashValue(entryPtr);
    s->samplerMaskMode->val = calloc(1,sizeof(int));
  }

  s->texid[0] = -1;		/* initialize to no texture sampler */

  return(gobjAddObj(objlist, obj));
}


static int motionpatchCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id, n, lifetime;
  double speed;
  char *handle;

  int verbose = 1;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " n speed lifetime", NULL);
    return TCL_ERROR;
  }
  else handle = argv[1];
  if (Tcl_GetInt(interp, argv[1], &n) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &speed) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &lifetime) != TCL_OK) return TCL_ERROR;

  if ((id = motionpatchCreate(olist, MotionpatchShaderProg,
			      n, speed, lifetime)) < 0) {
    Tcl_SetResult(interp, "error creating motionpatch", TCL_STATIC);
    return(TCL_ERROR);
  }

  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
  return(TCL_OK);
}


static int motionpatchSetSamplerCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *g;
  int id;
  int texid;
  int sampler = 0;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch [textureID] [sampler]", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a shader object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type shaderObj", NULL);
    return TCL_ERROR;
  }

  if (argc > 2) {
    if (Tcl_GetInt(interp, argv[2], &texid) != TCL_OK) return TCL_ERROR;
  }

  if (argc > 3) {
    if (Tcl_GetInt(interp, argv[3], &sampler) != TCL_OK) return TCL_ERROR;
  }
  if (sampler < 0 || sampler >= NSAMPLERS) {
    Tcl_AppendResult(interp, argv[0], ": sampler out of range", NULL);
    return TCL_ERROR;
  }

  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  g->texid[sampler] = texid;

  return(TCL_OK);
}


static int motionpatchSpeedCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double speed;
  
  if (argc < 3) {
    Tcl_AppendResult(interp,
		     "usage: ", argv[0],
		     " motionpatch_speed motionpatch speed", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionpatch object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not a motionpatch", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &speed) != TCL_OK) return TCL_ERROR;
  s->speed = speed;
  setSpeeds(s, cos(s->direction)*speed, sin(s->direction)*speed);

  return(TCL_OK);
  
}

static int motionpatchUseNoiseDirectionCmd(ClientData clientData, Tcl_Interp *interp,
					   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double period;
  int use_noise;
  double rate = 0.0;
  
  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch useNoise period [rate]", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionpatch object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not a motionpatch", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &use_noise) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &period) != TCL_OK) return TCL_ERROR;
  if (argc > 4) {
    if (Tcl_GetDouble(interp, argv[4], &rate) != TCL_OK) return TCL_ERROR;
  }
  s->noise_period = period;
  s->noise_update_rate = rate;
  s->set_direction_by_noise = use_noise;

  return(TCL_OK);
  
}

static int motionpatchSetSeedCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  int ctxid;
  int seed;
  
  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch context_id seed", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionpatch object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not a motionpatch", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &ctxid) != TCL_OK) return TCL_ERROR;
  if (ctxid < 0 || ctxid >= MAX_NOISE_CTX) {
    Tcl_AppendResult(interp, argv[0], ": invalid noise context", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[3], &seed) != TCL_OK) return TCL_ERROR;

  if (s->ctx[ctxid]) 
    open_simplex_noise_free(s->ctx[ctxid]);
  s->noise_seed[ctxid] = seed;
  open_simplex_noise(s->noise_seed[ctxid], &s->ctx[ctxid]);

  return(TCL_OK);
}

static int motionpatchSetNoiseZCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double noise_z;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch noise_z", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionpatch object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not a motionpatch", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &noise_z) != TCL_OK) return TCL_ERROR;
  s->noise_z = noise_z;

  return(TCL_OK);
}

static int motionpatchNoiseUpdateZCmd(ClientData clientData, Tcl_Interp *interp,
				      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  int do_update;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch update_by_stimtime", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionpatch object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not a motionpatch", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &do_update) != TCL_OK) return TCL_ERROR;
  s->noise_update_z = do_update;

  return(TCL_OK);
}


static int motionpatchDirectionCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double direction;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch direction", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionpatch object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not a motionpatch", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &direction) != TCL_OK) return TCL_ERROR;
  s->direction = direction;
  setSpeeds(s, cos(s->direction)*(s->speed), sin(s->direction)*(s->speed));

  return(TCL_OK);
  
}

static int motionpatchCoherenceCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double coherence;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch coherence", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionpatch object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not a motionpatch", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &coherence) != TCL_OK) return TCL_ERROR;
  if (coherence < 0.0 || coherence > 1.0) {
    Tcl_AppendResult(interp, argv[0],
		     ": coherence must between between 0.0 and 1.0", NULL);
    return TCL_ERROR;
  }
  s->coherence = coherence;
  setCoherences(s, coherence);
  return(TCL_OK);
}


static int motionpatchPointsizeCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double pointsize;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch pointsize", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionpatch object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type motionpatch", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &pointsize) != TCL_OK) return TCL_ERROR;
  s->pointsize = pointsize;

  return(TCL_OK);
}


static int motionpatchMaskTypeCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  int type;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch type", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionpatch object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type motionpatch", NULL);
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

static int motionpatchSamplerMaskModeCmd(ClientData clientData, Tcl_Interp *interp,
					 int argc, char *argv[])
{
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  int mode;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch mode", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionpatch object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type motionpatch", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &mode) != TCL_OK) return TCL_ERROR;
  if (mode < 0 || mode >= SMASK_LAST) {
    Tcl_AppendResult(interp, argv[0], ": invalid sampler mask mode specified", NULL);
    return TCL_ERROR;
  }
  s->samplermaskmode = mode;

  return(TCL_OK);
}

static int motionpatchMaskRadiusCmd(ClientData clientData, Tcl_Interp *interp,
				    int argc, char *argv[])
{
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double radius;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch radius", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a motionpatch object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type motionpatch", NULL);
    return TCL_ERROR;
  }
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &radius) != TCL_OK) return TCL_ERROR;
  s->mask_radius = radius;

  return(TCL_OK);
}


static int motionpatchColorCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  double r, g, b, a;
  int id;

  if (argc < 5) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch r g b ?a?", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a polygon object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != MotionpatchID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type motionpatch", NULL);
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

  if (!strcmp(argv[0], "motionpatch_color")) {
    s->color1[0] = r;
    s->color1[1] = g;
    s->color1[2] = b;
    s->color1[3] = a;
  }
  else {
    s->color2[0] = r;
    s->color2[1] = g;
    s->color2[2] = b;
    s->color2[3] = a;
  }

  return(TCL_OK);
}

int motionpatchShaderCreate(Tcl_Interp *interp)
{
  MotionpatchShaderProg = (SHADER_PROG *) calloc(1, sizeof(SHADER_PROG));

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
    " texcoord  = vertex_texcoord;"
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

    "uniform sampler2D tex0;"
    "uniform int samplerMaskMode;"
    "in vec2 texcoord;"
    "uniform vec4 uColor1;"
    "uniform vec4 uColor2;"
    "out vec4 frag_color;"
    "void main () {"
    " vec3 texColor = texture(tex0, vec2(texcoord.s, 1.0-texcoord.t)).rgb;"
    " float texAlpha = texture(tex0, vec2(texcoord.s, 1.0-texcoord.t)).a;"
    " float alpha = 1.0;"
    " vec3 color;"
    " if (samplerMaskMode == 0) { alpha = uColor1.a; color = uColor1.rgb; }"
    " else if (samplerMaskMode == 1) { alpha = texAlpha; color = uColor1.rgb; }"
    " else if (samplerMaskMode == 2) { alpha = 1.0-texAlpha; color = uColor1.rgb; }"
    " else if (samplerMaskMode == 3) { if (texAlpha < 0.5) { alpha = uColor1.a; color = uColor1.rgb; }"
    "                                  else { alpha = uColor2.a; color = uColor2.rgb;} }"
    " frag_color = vec4 (color, alpha);"
    "}";
  
  if (build_prog(MotionpatchShaderProg,
		 vertex_shader, fragment_shader, 0) == -1) {
    Tcl_AppendResult(interp,
		     "motionpatch : error building motionpatch shader", NULL);
    return TCL_ERROR;
  }

  /* Now add uniforms into master table */
  Tcl_InitHashTable(&MotionpatchShaderProg->uniformTable, TCL_STRING_KEYS);
  add_uniforms_to_table(&MotionpatchShaderProg->uniformTable,
			MotionpatchShaderProg);

  /* Now add attribs into master table */
  Tcl_InitHashTable(&MotionpatchShaderProg->attribTable, TCL_STRING_KEYS);
  add_attribs_to_table(&MotionpatchShaderProg->attribTable,
		       MotionpatchShaderProg);

  return TCL_OK;
}


#ifdef WIN32
EXPORT(int,Motionpatch_Init) _ANSI_ARGS_((Tcl_Interp *interp))
#else
int Motionpatch_Init(Tcl_Interp *interp)
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
  
  if (MotionpatchID < 0) MotionpatchID = gobjRegisterType();

  gladLoadGL();

  motionpatchShaderCreate(interp);

  Tcl_CreateCommand(interp, "motionpatch", (Tcl_CmdProc *) motionpatchCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_speed",
		    (Tcl_CmdProc *) motionpatchSpeedCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_direction",
		    (Tcl_CmdProc *) motionpatchDirectionCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_useNoiseDirection",
		    (Tcl_CmdProc *) motionpatchUseNoiseDirectionCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_setSampler",
		    (Tcl_CmdProc *) motionpatchSetSamplerCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_setSeed",
		    (Tcl_CmdProc *) motionpatchSetSeedCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_setNoiseZ",
		    (Tcl_CmdProc *) motionpatchSetNoiseZCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_noiseUpdateZ",
		    (Tcl_CmdProc *) motionpatchNoiseUpdateZCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_coherence",
		    (Tcl_CmdProc *) motionpatchCoherenceCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_pointsize", 
		    (Tcl_CmdProc *) motionpatchPointsizeCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_masktype", 
		    (Tcl_CmdProc *) motionpatchMaskTypeCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_samplermaskmode", 
		    (Tcl_CmdProc *) motionpatchSamplerMaskModeCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_maskradius", 
		    (Tcl_CmdProc *) motionpatchMaskRadiusCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_color", 
		    (Tcl_CmdProc *) motionpatchColorCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_color2", 
		    (Tcl_CmdProc *) motionpatchColorCmd, 
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
