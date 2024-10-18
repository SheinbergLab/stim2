/*
 * NAME
 *   shader.c
 *
 * DESCRIPTION
 *   GLSL shader gobj
 *
 * DETAILS
 *
 *   In the current version, this module is designed to primarily 
 *   support procedural shaders.  A .glsl file is expected to be 
 *   used to build the shader, which is kept in a shader table.  
 *   Uniforms are made accessible to the tcl interface,
 *    with two special values automatically updated:
 *    "time": set to number of seconds since the shader 
 *       object's group was made visible
 *    "resolution": set to current window width and window height.  
 *   Other uniforms can be updated using shaderObjSetUniform.
 *  
 * EXAMPLE
 *   load shader
 *   shaderSetPath /path/to/shaders/
 *   set s [shaderBuild nebula]
 *   set o [shaderObj $s]
 *   scaleObj $o 5
 *   glistAddObject $o 0
 *   glistSetDynamic 0 1
 *   glistSetVisible 1
 *   redraw
 *
 * AUTHOR
 *    DLS / SUMMER-16 / SPRING-17 / WINTER-17
 *
 * CREDITS
 *  GLSW code for reading shader files: 
 *       Written by Philip Rideout in April 2010
 *       Covered by the MIT License
 *
 *   BSTRLIB written by Paul Hsieh in 2002-2008, and is covered by the BSD open source 
 * license and the GPL. Refer to the accompanying documentation for details 
 * on usage and license.
 *    
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

#include <glad/glad.h>
#include <GLFW/glfw3.h> 

#include <utilc.h>

#include "glsw.h"

/****************************************************************/
/*                 Stim Specific Headers                        */
/****************************************************************/

#include <stim2.h>
#include "shaderutils.h"

#ifndef M_PI
#define M_PI (3.14159265358979323)
#endif

#ifndef MAX_PATH
#define MAX_PATH 512
#endif

static int ShaderObjID = -1;	/* unique shader object id */

Tcl_HashTable shaderProgramTable; /* keep track of compiled/linked programs */
static int shaderProgramCount = 0;

#define NSAMPLERS 4		/* currently allow four textures in shader */

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

typedef struct _shader_obj {
  int type;
  GLuint texid[NSAMPLERS];	/* if >= 0, bind this texture  */
  UNIFORM_INFO *tex0;		/* Texture samples to share    */
  UNIFORM_INFO *tex1;		/* Will be called tex0-tex3    */
  UNIFORM_INFO *tex2;		
  UNIFORM_INFO *tex3;		
  UNIFORM_INFO *time;	        /* set if we have "time" uniform  */
  UNIFORM_INFO *resolution;     /* set if we have "resolution" uniform */
  UNIFORM_INFO *modelviewMat;   /* set if we have "modelviewMat" uniform */
  UNIFORM_INFO *projMat;        /* set if we have "projMat" uniform */
  SHADER_PROG *program;
  VAO_INFO *vao_info;		/* to track vertex attributes */
  Tcl_HashTable uniformTable;	/* local unique version */
  Tcl_HashTable attribTable;	/* local unique version */
} SHADER_OBJ;

static int uniform_set(Tcl_Interp *interp, Tcl_HashTable *table,
		       char *shader_name, char *name, char *valstr);

static void delete_vao_info(VAO_INFO *v);

static int parse_ints(char *str, int *vals, int n);
static int parse_floats(char *str, float *vals, int n);

/* from shaderimage.c */
int imageLoadCmd(ClientData clientData, Tcl_Interp *interp,
		 int argc, char *argv[]);
int imageCreateCmd(ClientData clientData, Tcl_Interp *interp,
		   int argc, char *argv[]);
int imageCreateFromStringCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[]);
int imageTextureIDCmd(ClientData  clientData, Tcl_Interp *interp,
		      int argc, char *argv[]);
int imageResetCmd(ClientData clientData, Tcl_Interp *interp,
		  int argc, char *argv[]);
void imageListReset(void);

int imageSetFilterType(ClientData cdata, Tcl_Interp * interp, 
		       int objc, Tcl_Obj * const objv[]);
  


/****************************************************************/
/*                      Shader Functions                        */
/****************************************************************/

SHADER_PROG *find_shader_program(char *shader_name) 
{
  Tcl_HashEntry *entryPtr;
  SHADER_PROG *sp;

  if ((entryPtr = Tcl_FindHashEntry(&shaderProgramTable, shader_name))) {
    sp = Tcl_GetHashValue(entryPtr);
    return sp;
  }
  
  else {
    return NULL;
  }
}

static void shaderObjDelete(GR_OBJ *o) 
{
  SHADER_OBJ *g = (SHADER_OBJ *) GR_CLIENTDATA(o);
  
  delete_uniform_table(&g->uniformTable);
  delete_attrib_table(&g->attribTable);
  delete_vao_info(g->vao_info);

  free((void *) g);
}

static void shaderObjReset(GR_OBJ *o) 
{
  SHADER_OBJ *g = (SHADER_OBJ *) GR_CLIENTDATA(o);
}



static void shaderObjDraw(GR_OBJ *m) 
{
  SHADER_OBJ *g = (SHADER_OBJ *) GR_CLIENTDATA(m);
  SHADER_PROG *sp = (SHADER_PROG *) g->program;
  float *v;

  glEnable(GL_TEXTURE_2D);
  glEnable(GL_TEXTURE_2D_ARRAY);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_BLEND);
  glUseProgram(sp->program);
  
  if (g->modelviewMat) {
    v = (float *) g->modelviewMat->val;
    stimGetMatrix(STIM_MODELVIEW_MATRIX, v);
  }
  if (g->projMat) {
    v = (float *) g->projMat->val;
    stimGetMatrix(STIM_PROJECTION_MATRIX, v);
  }

  update_uniforms(&g->uniformTable);

    
  /* bind associated texture to a shader sampler if associated */
  if (g->texid[0] >= 0 && g->tex0) {
    glActiveTexture(GL_TEXTURE0);
    switch(g->tex0->type) {
    case GL_SAMPLER_2D:
      glBindTexture(GL_TEXTURE_2D, g->texid[0]);
      break;
    case GL_SAMPLER_2D_ARRAY:
      glBindTexture(GL_TEXTURE_2D_ARRAY, g->texid[0]);
      break;
    }
  }
  if (g->texid[1] >= 0 && g->tex1) {
    glActiveTexture(GL_TEXTURE1);
    switch(g->tex1->type) {
    case GL_SAMPLER_2D:
      glBindTexture(GL_TEXTURE_2D, g->texid[1]);
      break;
    case GL_SAMPLER_2D_ARRAY:
      glBindTexture(GL_TEXTURE_2D_ARRAY, g->texid[1]);
      break;
    }
  }
  if (g->texid[2] >= 0 && g->tex2) {
    glActiveTexture(GL_TEXTURE2);
    switch(g->tex2->type) {
    case GL_SAMPLER_2D:
      glBindTexture(GL_TEXTURE_2D, g->texid[2]);
      break;
    case GL_SAMPLER_2D_ARRAY:
      glBindTexture(GL_TEXTURE_2D_ARRAY, g->texid[2]);
      break;
    }
  }
  if (g->texid[3] >= 0 && g->tex3) {
    glActiveTexture(GL_TEXTURE3);
    switch(g->tex3->type) {
    case GL_SAMPLER_2D:
      glBindTexture(GL_TEXTURE_2D, g->texid[3]);
      break;
    case GL_SAMPLER_2D_ARRAY:
      glBindTexture(GL_TEXTURE_2D_ARRAY, g->texid[3]);
      break;
    }
  }

  if (g->vao_info->narrays) {
    glBindVertexArray(g->vao_info->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, g->vao_info->nindices);
  }
  
  glUseProgram(0);
}

static void shaderObjUpdate(GR_OBJ *m) 
{
  SHADER_OBJ *g = (SHADER_OBJ *) GR_CLIENTDATA(m);
  float sec;
  float res[2];
  int w, h;
  if (g->time) {
    sec = getStimTime()/1000.0;
    memcpy(g->time->val, &sec, sizeof(float));
  }
  if (g->resolution) {
    getScreenInfo(NULL, NULL, &w, &h, NULL);
    res[0] = w; res[1] = h;
    memcpy(g->resolution->val, &res[0], 2*sizeof(float));
  }
}

static int shaderObjCreate(OBJ_LIST *olist, SHADER_PROG *sp)
{
  const char *name = "Shader";
  GR_OBJ *obj;
  SHADER_OBJ *g;
  Tcl_HashEntry *entryPtr;

  /* To be replaced with dynamic ones at some point */
  static GLfloat texcoords[] = { 0.0, 0.0,
				 0.0, 1.0,
				 1.0, 0.0,
				 1.0, 1.0 };
  static GLfloat points[] = { -.5, -.5, 0.0,
			      -.5, .5, 0.0,
			      .5, -.5, 0.0,
  			      .5, .5, 0.0 };

  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = ShaderObjID;

  GR_ACTIONFUNCP(obj) = shaderObjDraw;
  GR_RESETFUNCP(obj) = shaderObjReset;
  GR_DELETEFUNCP(obj) = shaderObjDelete;
  GR_UPDATEFUNCP(obj) = shaderObjUpdate;

  g = (SHADER_OBJ *) calloc(1, sizeof(SHADER_OBJ));
  GR_CLIENTDATA(obj) = g;

  g->program = sp;
  copy_uniform_table(&sp->uniformTable, &g->uniformTable);
  copy_attrib_table(&sp->attribTable, &g->attribTable);

  g->vao_info = (VAO_INFO *) calloc(1, sizeof(VAO_INFO));
  g->vao_info->narrays = 0;
  glGenVertexArrays(1, &g->vao_info->vao);
  glBindVertexArray(g->vao_info->vao);

  if ((entryPtr = Tcl_FindHashEntry(&g->attribTable, "vertex_position"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    g->vao_info->npoints = 12;
    g->vao_info->points = 
      (GLfloat *) calloc(g->vao_info->npoints, sizeof(GLfloat));
    memcpy(g->vao_info->points, points, 
	   g->vao_info->npoints*sizeof(GLfloat));
    
    glGenBuffers(1, &g->vao_info->points_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g->vao_info->points_vbo);
    glBufferData(GL_ARRAY_BUFFER, g->vao_info->npoints*sizeof(GLfloat),
		 g->vao_info->points, GL_STATIC_DRAW);
    
    
    glBindBuffer(GL_ARRAY_BUFFER, g->vao_info->points_vbo);
    glVertexAttribPointer(ainfo->location, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
    g->vao_info->nindices = 4;	/* single quad */
    g->vao_info->narrays++;
  }

  if ((entryPtr = Tcl_FindHashEntry(&g->attribTable, "vertex_texcoord"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    g->vao_info->ntexcoords = 8;
    g->vao_info->texcoords = 
      (GLfloat *) calloc(g->vao_info->ntexcoords, sizeof(GLfloat));
    memcpy(g->vao_info->texcoords, texcoords, 
	   g->vao_info->ntexcoords*sizeof(GLfloat));
    
    glGenBuffers(1, &g->vao_info->texcoords_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g->vao_info->texcoords_vbo);
    glBufferData(GL_ARRAY_BUFFER, g->vao_info->ntexcoords*sizeof(GLfloat),
		 g->vao_info->texcoords, GL_STATIC_DRAW);

    glVertexAttribPointer(ainfo->location, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
    g->vao_info->narrays++;
  }

  /* if the time uniform is found, set flag */
  if ((entryPtr = Tcl_FindHashEntry(&g->uniformTable, "time"))) {
    g->time = Tcl_GetHashValue(entryPtr);
    g->time->val = malloc(sizeof(float));
  }

  if ((entryPtr = Tcl_FindHashEntry(&g->uniformTable, "resolution"))) {
    g->resolution = Tcl_GetHashValue(entryPtr);
    g->resolution->val = malloc(sizeof(float)*2);
  }

  if ((entryPtr = Tcl_FindHashEntry(&g->uniformTable, "modelviewMat"))) {
    g->modelviewMat = Tcl_GetHashValue(entryPtr);
    g->modelviewMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&g->uniformTable, "projMat"))) {
    g->projMat = Tcl_GetHashValue(entryPtr);
    g->projMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&g->uniformTable, "tex0"))) {
    g->tex0 = Tcl_GetHashValue(entryPtr);
    g->tex0->val = malloc(sizeof(int));
    *((int *)(g->tex0->val)) = 0;
  }
  
  if ((entryPtr = Tcl_FindHashEntry(&g->uniformTable, "tex1"))) {
    g->tex1 = Tcl_GetHashValue(entryPtr);
    g->tex1->val = malloc(sizeof(int));
    *((int *)(g->tex1->val)) = 1;
  }
  
  if ((entryPtr = Tcl_FindHashEntry(&g->uniformTable, "tex2"))) {
    g->tex2 = Tcl_GetHashValue(entryPtr);
    g->tex2->val = malloc(sizeof(int));
    *((int *)(g->tex2->val)) = 2;
  }
  
  if ((entryPtr = Tcl_FindHashEntry(&g->uniformTable, "tex3"))) {
    g->tex3 = Tcl_GetHashValue(entryPtr);
    g->tex3->val = malloc(sizeof(int));
    *((int *)(g->tex3->val)) = 3;
  }
  
  g->texid[0] = -1;		/* initialize to no texture sampler */
  g->texid[1] = -1;		
  g->texid[2] = -1;		
  g->texid[3] = -1;		

   return(gobjAddObj(olist, obj));
}

static int set_default_uniforms(Tcl_Interp *interp, SHADER_OBJ *s)
{
  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch searchEntry;
  Tcl_HashTable *table = &s->program->defaultsTable;
    
  entryPtr = Tcl_FirstHashEntry(table, &searchEntry);
  if (entryPtr) {
    uniform_set(interp, &s->uniformTable, s->program->name, 
		Tcl_GetHashKey(table, entryPtr),
		Tcl_GetHashValue(entryPtr));
    while ((entryPtr = Tcl_NextHashEntry(&searchEntry))) {
      uniform_set(interp, &s->uniformTable, s->program->name, 
		  Tcl_GetHashKey(table, entryPtr),
		  Tcl_GetHashValue(entryPtr));
    }
  }
  
  return 1;
}
					  
static int shaderObjCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;
  SHADER_PROG *sp;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], ": shader_name", NULL);
    return(TCL_ERROR);
  }

  if (!(sp = find_shader_program(argv[1]))) {
    Tcl_AppendResult(interp, argv[0], ": shader \"", argv[1], "\" not found", 
		     NULL);
    return(TCL_ERROR);
  }

  if ((id = shaderObjCreate(olist, sp)) < 0) {
    Tcl_AppendResult(interp, argv[0], ": error creating shader", NULL);
    return(TCL_ERROR);
  }

  /* now copy default uniform values from shader program */
  set_default_uniforms(interp, GR_CLIENTDATA(OL_OBJ(olist,id)));

  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
  return(TCL_OK);
}

static int shaderObjSetSamplerCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  SHADER_OBJ *g;
  int id;
  int texid;
  int sampler = 0;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " shaderObj [textureID] [sampler]", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a shader object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != ShaderObjID) {
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

static int shaderSetPathCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  char oldpath[MAX_PATH];
  strncpy(oldpath, shaderPath, MAX_PATH-1);

  if (argc >= 2) {
    strncpy(shaderPath, argv[1], MAX_PATH-1);
  }

  Tcl_SetResult(interp, oldpath, TCL_VOLATILE);
  return(TCL_OK);
}

static int shaderBuildCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  SHADER_PROG shader_prog;	/* temp to hold new prog */
  char shader_name[64];
  Tcl_HashEntry *entryPtr;
  int newentry;
  int verbose = 1;
  SHADER_PROG *newprog;

  if (argc < 2) {
    Tcl_AppendResult(interp, argv[0], ": no shader file specified", NULL);
    return(TCL_ERROR);
  }

  if (build_prog_from_file(&shader_prog, argv[1], verbose) != GL_NO_ERROR) 
    return TCL_ERROR;

  
  newprog = (SHADER_PROG *) calloc(1, sizeof(SHADER_PROG));
  
  newprog->fragShader = shader_prog.fragShader;
  newprog->vertShader = shader_prog.vertShader;
  newprog->program = shader_prog.program;

  /* Now add uniforms into master table */
  Tcl_InitHashTable(&newprog->uniformTable, TCL_STRING_KEYS);
  add_uniforms_to_table(&newprog->uniformTable, newprog);

  /* Add default values from shader file */
  Tcl_InitHashTable(&newprog->defaultsTable, TCL_STRING_KEYS);
  add_defaults_to_table(interp, &newprog->defaultsTable, argv[1]);
  
  /* Now add attribs into master table */
  Tcl_InitHashTable(&newprog->attribTable, TCL_STRING_KEYS);
  add_attribs_to_table(&newprog->attribTable, newprog);

  sprintf(shader_name, "shader%d", shaderProgramCount++);
  strcpy(newprog->name, shader_name);
  entryPtr = Tcl_CreateHashEntry(&shaderProgramTable, shader_name, &newentry);
  Tcl_SetHashValue(entryPtr, newprog);
  Tcl_SetResult(interp, shader_name, TCL_VOLATILE);
  return(TCL_OK);
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

static int shader_prog_delete(SHADER_PROG *sp)
{
  glUseProgram(0);
  delete_uniform_table(&sp->uniformTable);
  delete_attrib_table(&sp->attribTable);
  delete_defaults_table(&sp->defaultsTable);
  glDetachShader(sp->program, sp->vertShader);
  glDetachShader(sp->program, sp->fragShader);
  glDeleteProgram(sp->program);
  glDeleteShader(sp->fragShader);
  glDeleteShader(sp->vertShader);
  free(sp);
  return 0;
}

static int shaderDeleteCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  Tcl_HashEntry *entryPtr;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, argv[0], ": no shader specified", NULL);
    return(TCL_ERROR);
  }

  if ((entryPtr = Tcl_FindHashEntry(&shaderProgramTable, argv[1]))) {
    SHADER_PROG *sp;
    if ((sp = Tcl_GetHashValue(entryPtr))) shader_prog_delete(sp);
    Tcl_DeleteHashEntry(entryPtr);
  }
  return TCL_OK;
}

static int shaderDeleteAllCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch searchEntry;
  SHADER_PROG *sp;

  entryPtr = Tcl_FirstHashEntry(&shaderProgramTable, &searchEntry);
  while (entryPtr) {
    sp = (SHADER_PROG *) Tcl_GetHashValue(entryPtr);
    shader_prog_delete(sp);
    Tcl_DeleteHashEntry(entryPtr);
    entryPtr = Tcl_NextHashEntry(&searchEntry);
  }
  shaderProgramCount = 0;

  /* Free all loaded textures as well */
  imageListReset();
  
  return 0;
}


static int uniform_names(Tcl_Interp *interp, Tcl_HashTable *table)
{
  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch searchEntry;
  Tcl_DString uniformList;
  
  Tcl_DStringInit(&uniformList);
  entryPtr = Tcl_FirstHashEntry(table, &searchEntry);
  if (entryPtr) {
    Tcl_DStringAppendElement(&uniformList, Tcl_GetHashKey(table, entryPtr));
    while ((entryPtr = Tcl_NextHashEntry(&searchEntry))) {
      Tcl_DStringAppendElement(&uniformList, Tcl_GetHashKey(table, entryPtr));
    }
  }
  Tcl_DStringResult(interp, &uniformList);
  return TCL_OK;
}

static int shaderUniformNamesCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  SHADER_PROG *sp;

  if (argc < 2) {
    Tcl_AppendResult(interp, argv[0], ": no shader specified", NULL);
    return(TCL_ERROR);
  }

  if (!(sp = find_shader_program(argv[1]))) {
    Tcl_AppendResult(interp, argv[0], ": shader \"", argv[1], "\" not found", 
		     NULL);
    return(TCL_ERROR);
  }
  return(uniform_names(interp, &sp->uniformTable));
}

static int uniform_defaults(Tcl_Interp *interp, Tcl_HashTable *table)
{
  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch searchEntry;
  Tcl_DString uniformList;
  
  Tcl_DStringInit(&uniformList);
  entryPtr = Tcl_FirstHashEntry(table, &searchEntry);
  if (entryPtr) {
    Tcl_DStringAppendElement(&uniformList, Tcl_GetHashKey(table, entryPtr));
    Tcl_DStringAppendElement(&uniformList, Tcl_GetHashValue(entryPtr));
    while ((entryPtr = Tcl_NextHashEntry(&searchEntry))) {
      Tcl_DStringAppendElement(&uniformList, Tcl_GetHashKey(table, entryPtr));
      Tcl_DStringAppendElement(&uniformList, Tcl_GetHashValue(entryPtr));
    }
  }
  Tcl_DStringResult(interp, &uniformList);
  return TCL_OK;
}

static int shaderDefaultSettingsCmd(ClientData clientData, Tcl_Interp *interp,
				    int argc, char *argv[])
{
  SHADER_PROG *sp;

  if (argc < 2) {
    Tcl_AppendResult(interp, argv[0], ": no shader specified", NULL);
    return(TCL_ERROR);
  }
  
  if (!(sp = find_shader_program(argv[1]))) {
    Tcl_AppendResult(interp, argv[0], ": shader \"", argv[1], "\" not found", 
		     NULL);
    return(TCL_ERROR);
  }
  return(uniform_defaults(interp, &sp->defaultsTable));
}


static int shaderObjUniformNamesCmd(ClientData clientData, Tcl_Interp *interp,
				    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  SHADER_OBJ *g;
  int id;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " shaderObj", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a shader object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != ShaderObjID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type shaderObj", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  return(uniform_names(interp, &g->uniformTable));
}


static int parse_ints(char *str, int *vals, int n)
{
  char *buffer = strdup(str);
  char *p;
  int i;

  p = strtok(buffer, " ");
  for (i = 0; i < n && p != NULL; i++) {
    vals[i] = atoi(p);
    p = strtok(NULL, " ");
  }

  free(buffer);
  return i;
}

static int parse_floats(char *str, float *vals, int n)
{
  char *buffer = strdup(str);
  char *p;
  int i;

  p = strtok(buffer, " ");
  for (i = 0; i < n && p != NULL; i++) {
    vals[i] = atof(p);
    p = strtok(NULL, " ");
  }

  free(buffer);
  return i;
}

static int uniform_set(Tcl_Interp *interp, Tcl_HashTable *table,
		       char *shader_name, char *name, char *valstr)
{
  Tcl_HashEntry *entryPtr;
  UNIFORM_INFO *uinfo;
  int ival;
  float fval;
  float fvals[16];
  
  if ((entryPtr = Tcl_FindHashEntry(table, name))) {
    uinfo = (UNIFORM_INFO *) Tcl_GetHashValue(entryPtr);
  }
  else {
    Tcl_AppendResult(interp, "uniform \"", name, "\" not found in shader \"", 
		     shader_name, "\"", NULL);
    return TCL_ERROR;
  }

  switch(uinfo->type) {
  case GL_BOOL: 
  case GL_INT:
    if (sscanf(valstr, "%d", &ival) == 1) {
      if (!uinfo->val) {
	uinfo->val = (int *) malloc(sizeof(int));
      }
      memcpy(uinfo->val, &ival, sizeof(int));
      return TCL_OK;
    }
    break;
  case GL_FLOAT: 
    if (sscanf(valstr, "%f", &fval) == 1) {
      if (!uinfo->val) 
	uinfo->val = (float *) malloc(sizeof(float));
      memcpy(uinfo->val, &fval, sizeof(float));
      return TCL_OK;
    }
    break;
  case GL_FLOAT_VEC2:
    if (parse_floats(valstr, fvals, 2) == 2) {
      if (!uinfo->val) 
	uinfo->val = (float *) malloc(2*sizeof(float));
      memcpy(uinfo->val, &fvals[0], 2*sizeof(float));
      return TCL_OK;
    }
    break;
  case GL_FLOAT_VEC3:
    if (parse_floats(valstr, fvals, 3) == 3) {
      if (!uinfo->val) 
	uinfo->val = (float *) malloc(3*sizeof(float));
      memcpy(uinfo->val, &fvals[0], 3*sizeof(float));
      return TCL_OK;
    }
    break;
  case GL_FLOAT_VEC4:
  case GL_FLOAT_MAT2:
    if (parse_floats(valstr, fvals, 4) == 4) {
      if (!uinfo->val) 
	uinfo->val = (float *) malloc(4*sizeof(float));
      memcpy(uinfo->val, &fvals[0], 4*sizeof(float));
      return TCL_OK;
    }
    break;
  case GL_FLOAT_MAT3:
    if (parse_floats(valstr, fvals, 9) == 9) {
      if (!uinfo->val) 
	uinfo->val = (float *) malloc(9*sizeof(float));
      memcpy(uinfo->val, &fvals[0], 9*sizeof(float));
      return TCL_OK;
    }
    break;
  case GL_FLOAT_MAT4:
    if (parse_floats(valstr, fvals, 16) == 16) {
      if (!uinfo->val) 
	uinfo->val = (float *) malloc(16*sizeof(float));
      memcpy(uinfo->val, &fvals[0], 16*sizeof(float));
      return TCL_OK;
    }
    break;
  case GL_SAMPLER_2D:
  case GL_SAMPLER_3D:
  case GL_SAMPLER_CUBE:
  case GL_SAMPLER_2D_SHADOW:
    break;
  }
  
  Tcl_AppendResult(interp, "unable to set uniform: \"", name, "\" in shader \"", 
		   shader_name, "\"", NULL);
  return TCL_ERROR;
}

static int uniform_get(Tcl_Interp *interp, Tcl_HashTable *table,
		       char *shader_name, char *name)
{
  Tcl_HashEntry *entryPtr;
  UNIFORM_INFO *uinfo;
  static char valstr[256];
  int *ival;
  float *fvals;
  
  if ((entryPtr = Tcl_FindHashEntry(table, name))) {
    uinfo = (UNIFORM_INFO *) Tcl_GetHashValue(entryPtr);
  }
  else {
    Tcl_AppendResult(interp, "uniform \"", name, "\" not found in shader \"", 
		     shader_name, "\"", NULL);
    return TCL_ERROR;
  }

  valstr[0] = 0;
  switch(uinfo->type) {
  case GL_BOOL: 
  case GL_INT:
    ival = (int *) uinfo->val;
    sprintf(valstr, "%d", ival[0]);
    break;
  case GL_FLOAT: 
    fvals = (float *) uinfo->val;
    sprintf(valstr, "%f", fvals[0]);
    break;
  case GL_FLOAT_VEC2:
    fvals = (float *) uinfo->val;
    sprintf(valstr, "%f %f", fvals[0], fvals[1]);
    break;
  case GL_FLOAT_VEC3:
    fvals = (float *) uinfo->val;
    sprintf(valstr, "%f %f %f", fvals[0], fvals[1], fvals[2]);
    break;
  case GL_FLOAT_VEC4:
  case GL_FLOAT_MAT2:
    fvals = (float *) uinfo->val;
    sprintf(valstr, "%f %f %f %f",
	    fvals[0], fvals[1], fvals[2], fvals[3]);
    break;
  case GL_FLOAT_MAT3:
    fvals = (float *) uinfo->val;
    sprintf(valstr, "%f %f %f %f %f %f %f %f",
	    fvals[0],	    fvals[1],	    fvals[2],	    fvals[3],
	    fvals[4],       fvals[5],	    fvals[6],	    fvals[7]);
    break;
  case GL_FLOAT_MAT4:
    fvals = (float *) uinfo->val;
    sprintf(valstr, "%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
	    fvals[0],	    fvals[1],	    fvals[2],	    fvals[3],
	    fvals[4],       fvals[5],	    fvals[6],	    fvals[7],
	    fvals[8],	    fvals[9],	    fvals[10],	    fvals[11],
	    fvals[12],      fvals[13],	    fvals[14],	    fvals[15]);	    
    break;
  case GL_SAMPLER_2D:
  case GL_SAMPLER_3D:
  case GL_SAMPLER_CUBE:
  case GL_SAMPLER_2D_SHADOW:
    break;
  }
  
  Tcl_SetResult(interp, valstr, TCL_STATIC);
  return TCL_OK;
}

static int shaderObjSetUniformCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  SHADER_OBJ *g;
  int id;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " shaderObj uniform [value]", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a shader object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != ShaderObjID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type shaderObj", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));
  if (argc > 3) {
    return(uniform_set(interp, &g->uniformTable, g->program->name, 
		       argv[2], argv[3]));
  }
  else {
    return(uniform_get(interp, &g->uniformTable, g->program->name,
		       argv[2]));
  }
}



/********************************************************************/
/*                  PACKAGE INITIALIZATION CODE                     */
/********************************************************************/
#ifdef _WIN32
EXPORT(int,Shader_Init) _ANSI_ARGS_((Tcl_Interp *interp))
#else
int Shader_Init(Tcl_Interp *interp)
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
  
  if (ShaderObjID < 0) ShaderObjID = gobjRegisterType();

  gladLoadGL();
  
  Tcl_InitHashTable(&shaderProgramTable, TCL_STRING_KEYS);

  Tcl_CreateCommand(interp, "shaderObj", 
		    (Tcl_CmdProc *) shaderObjCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderObjUniformNames", 
		    (Tcl_CmdProc *) shaderObjUniformNamesCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderObjSetUniform", 
		    (Tcl_CmdProc *) shaderObjSetUniformCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderObjSetSampler", 
		    (Tcl_CmdProc *) shaderObjSetSamplerCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "shaderSetPath", 
		    (Tcl_CmdProc *) shaderSetPathCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderBuild", 
		    (Tcl_CmdProc *) shaderBuildCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderDelete", 
		    (Tcl_CmdProc *) shaderDeleteCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderDeleteAll", 
		    (Tcl_CmdProc *) shaderDeleteAllCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderUniformNames", 
		    (Tcl_CmdProc *) shaderUniformNamesCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderDefaultSettings", 
		    (Tcl_CmdProc *) shaderDefaultSettingsCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);


  Tcl_CreateCommand(interp, "shaderImageLoad", (Tcl_CmdProc *) imageLoadCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderImageCreate",
		    (Tcl_CmdProc *) imageCreateCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderImageCreateFromString",
		    (Tcl_CmdProc *) imageCreateFromStringCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderImageID", (Tcl_CmdProc *) imageTextureIDCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "shaderImageReset", (Tcl_CmdProc *) imageResetCmd, 
		    NULL, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateObjCommand(interp, "shaderImageSetFilterType",
		       imageSetFilterType,
		       NULL, NULL);


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
