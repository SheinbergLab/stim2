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

#include <GL/glew.h>
#include <GLFW/glfw3.h> 

#include <utilc.h>

#include "df.h"
#include "tcl_dl.h"
#include <stim.h>

typedef struct _attrib_info {
  GLint size;
  GLenum type;
  int location;
  GLchar *name;
} ATTRIB_INFO;


typedef struct _uniform_info {
  char *name;
  GLenum type;
  int location;
  void *val;			
} UNIFORM_INFO;

typedef struct _shader_prog {
  char name[64];
  GLuint     fragShader;
  GLuint     vertShader;
  GLuint     program;
  Tcl_HashTable   uniformTable;	/* master copy */
  Tcl_HashTable   attribTable;	/* master copy */
  Tcl_HashTable   defaultsTable;
} SHADER_PROG;

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


typedef struct polygon {
  int angle;			/* rotation angle   */
  int filled;
  int tessellated;		/* use gluTess routines */
  int tessid;			/* display list */
  int type;			/* draw type        */
  float linewidth;
  float pointsize;
  float color[4];
  float *verts;			/* x,y,z triplets   */
  int three_d;			/* is z specified?  */
  int nverts;
  int colori;
  int aa;			/* anti-alias?      */
  int blend;

  UNIFORM_INFO *modelviewMat;   /* set if we have "modelviewMat" uniform */
  UNIFORM_INFO *projMat;        /* set if we have "projMat" uniform */
  UNIFORM_INFO *uColor;         /* set if we have "uColor" uniform */
  SHADER_PROG *program;
  VAO_INFO *vao_info;		/* to track vertex attributes */
  Tcl_HashTable uniformTable;	/* local unique version */
  Tcl_HashTable attribTable;	/* local unique version */

} POLYGON;

static int PolygonID = -1;	/* unique polygon object id */
SHADER_PROG *PolygonShaderProg = NULL;

FILE *getConsoleFP(void) {
  return stderr;
}

char *GL_type_to_string (GLenum type) {
  switch (type) {
    case GL_BOOL: return "bool";
    case GL_INT: return "int";
    case GL_FLOAT: return "float";
    case GL_FLOAT_VEC2: return "vec2";
    case GL_FLOAT_VEC3: return "vec3";
    case GL_FLOAT_VEC4: return "vec4";
    case GL_FLOAT_MAT2: return "mat2";
    case GL_FLOAT_MAT3: return "mat3";
    case GL_FLOAT_MAT4: return "mat4";
    case GL_SAMPLER_2D: return "sampler2D";
    case GL_SAMPLER_3D: return "sampler3D";
    case GL_SAMPLER_CUBE: return "samplerCube";
    case GL_SAMPLER_2D_SHADOW: return "sampler2DShadow";
    default: break;
  }
  return "other";
}

static int add_uniforms_to_table(Tcl_HashTable *utable, SHADER_PROG *sp)
{
  int total = -1, maxlength = -1;
  int i;
  char *name;
  UNIFORM_INFO *uinfo;
  Tcl_HashEntry *entryPtr;
  int newentry;

  glGetProgramiv(sp->program, GL_ACTIVE_UNIFORMS, &total ); 
  if (total <= 0) 
    return TCL_OK;

  glGetProgramiv(sp->program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxlength);
  name = malloc(maxlength+1);
  for(i = 0; i < total; i++)  {
    int name_len=-1, num=-1;
    GLenum type = GL_ZERO;
    GLint location;
    glGetActiveUniform(sp->program, i, maxlength,
			&name_len, &num, &type, name);
    name[name_len] = 0;
    location = glGetUniformLocation(sp->program, name);

    if (location >= 0) {
      uinfo = (UNIFORM_INFO *) calloc(1, sizeof(UNIFORM_INFO));
      uinfo->name = strdup(name); /* make local copy */
      uinfo->type = type;
      uinfo->location = location;
      uinfo->val = NULL;
      entryPtr = Tcl_CreateHashEntry(&sp->uniformTable, uinfo->name, &newentry);
      Tcl_SetHashValue(entryPtr, uinfo);

      fprintf(getConsoleFP(), "%s: %s\n", uinfo->name, GL_type_to_string(type));
    }
  }
  free(name);
  return TCL_OK;
}

static int copy_uniform_table(Tcl_HashTable *source, Tcl_HashTable *dest)
{
  Tcl_HashEntry *entryPtr, *newEntryPtr;
  Tcl_HashSearch searchEntry;
  int newentry;
  UNIFORM_INFO *uinfo, *new_uinfo;

  Tcl_InitHashTable(dest, TCL_STRING_KEYS);
  entryPtr = Tcl_FirstHashEntry(source, &searchEntry);
  while (entryPtr) {
    uinfo = Tcl_GetHashValue(entryPtr);
    new_uinfo = (UNIFORM_INFO *) calloc(1, sizeof(UNIFORM_INFO));
    new_uinfo->name = strdup(uinfo->name);
    new_uinfo->type = uinfo->type;
    new_uinfo->location = uinfo->location;
    newEntryPtr = Tcl_CreateHashEntry(dest, new_uinfo->name, &newentry);
    Tcl_SetHashValue(newEntryPtr, new_uinfo);
    entryPtr = Tcl_NextHashEntry(&searchEntry);
  }
  return 0;
}

static int delete_uniform_table(Tcl_HashTable *utable)
{
  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch searchEntry;
  UNIFORM_INFO *uinfo;

  entryPtr = Tcl_FirstHashEntry(utable, &searchEntry);
  while (entryPtr) {
    uinfo = Tcl_GetHashValue(entryPtr);
    if (uinfo->name) free(uinfo->name); /* also the hash key */
    if (uinfo->val) free(uinfo->val);
    free(uinfo);
    entryPtr = Tcl_NextHashEntry(&searchEntry);
  }
  Tcl_DeleteHashTable(utable);

  return 0;
}


static int add_attribs_to_table(Tcl_HashTable *atable, SHADER_PROG *sp)
{
  int total = -1, maxlength = -1;
  int i;
  char *name;
  ATTRIB_INFO *ainfo;
  Tcl_HashEntry *entryPtr;
  int newentry;

  glGetProgramiv(sp->program, GL_ACTIVE_ATTRIBUTES, &total); 

  if (total <= 0) return TCL_OK;

  glGetProgramiv(sp->program, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &maxlength);
  name = malloc(maxlength+1);

  fprintf(getConsoleFP(), "%d active attribs / maxlength = %d\n", 
	  total, maxlength);

  for(i = 0; i < total; i++)  {
    int name_len=-1, size=-1;
    GLenum type = GL_ZERO;
    GLint location;
    glGetActiveAttrib(sp->program, i, maxlength,
		      &name_len, &size, &type, name);
    name[name_len] = 0;
    location = glGetAttribLocation(sp->program, name);
    if (location >= 0) {

      ainfo = (ATTRIB_INFO *) calloc(1, sizeof(ATTRIB_INFO));
      ainfo->name = malloc(name_len+1); /* make local copy */
      strcpy(ainfo->name, name);
      ainfo->size = size;
      ainfo->type = type;
      ainfo->location = location;
      entryPtr = Tcl_CreateHashEntry(&sp->attribTable, ainfo->name, &newentry);
      Tcl_SetHashValue(entryPtr, ainfo);
      fprintf(getConsoleFP(), "%s: %s [%d@%d]\n", 
	      ainfo->name, GL_type_to_string(type), size, location);
    }
  }
  free(name);
  return TCL_OK;
}

static int copy_attrib_table(Tcl_HashTable *source, Tcl_HashTable *dest)
{
  Tcl_HashEntry *entryPtr, *newEntryPtr;
  Tcl_HashSearch searchEntry;
  int newentry;
  ATTRIB_INFO *ainfo, *new_ainfo;

  Tcl_InitHashTable(dest, TCL_STRING_KEYS);
  entryPtr = Tcl_FirstHashEntry(source, &searchEntry);
  while (entryPtr) {
    ainfo = Tcl_GetHashValue(entryPtr);
    new_ainfo = (ATTRIB_INFO *) calloc(1, sizeof(ATTRIB_INFO));
    new_ainfo->name = strdup(ainfo->name);
    new_ainfo->type = ainfo->type;
    new_ainfo->size = ainfo->size;
    new_ainfo->location = ainfo->location;
    newEntryPtr = Tcl_CreateHashEntry(dest, new_ainfo->name, &newentry);
    Tcl_SetHashValue(newEntryPtr, new_ainfo);
    entryPtr = Tcl_NextHashEntry(&searchEntry);
  }
  return 0;
}

static int delete_attrib_table(Tcl_HashTable *atable)
{
  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch searchEntry;
  ATTRIB_INFO *ainfo;
  
  for (entryPtr = Tcl_FirstHashEntry(atable, &searchEntry);
       entryPtr != NULL;
       entryPtr = Tcl_NextHashEntry(&searchEntry)) {
    ainfo = (ATTRIB_INFO *) Tcl_GetHashValue(entryPtr);
    if (ainfo->name) free(ainfo->name); /* also the hash key */
    free(ainfo);
  }
  Tcl_DeleteHashTable(atable);

  return 0;
}

static int update_uniforms(Tcl_HashTable *utable)
{
  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch searchEntry;
  UNIFORM_INFO *uinfo;

  entryPtr = Tcl_FirstHashEntry(utable, &searchEntry);
  while (entryPtr) {
    uinfo = Tcl_GetHashValue(entryPtr);
    if (uinfo->val) {
      switch (uinfo->type) {
      case GL_BOOL: 
      case GL_INT:
      case GL_SAMPLER_2D:
	glUniform1iv(uinfo->location, 1, uinfo->val);
	break;
      case GL_FLOAT:
	glUniform1fv(uinfo->location, 1, uinfo->val);
	break;
      case GL_FLOAT_VEC2:
	glUniform2fv(uinfo->location, 1, uinfo->val);
	break;
      case GL_FLOAT_VEC3:
	glUniform3fv(uinfo->location, 1, uinfo->val);
	break;
      case GL_FLOAT_VEC4:
	glUniform4fv(uinfo->location, 1, uinfo->val);
	break;
      case GL_FLOAT_MAT2:
	glUniformMatrix2fv(uinfo->location, 1, 0, uinfo->val);
	break;
      case GL_FLOAT_MAT3:
	glUniformMatrix3fv(uinfo->location, 1, 0, uinfo->val);
	break;
      case GL_FLOAT_MAT4:
       	glUniformMatrix4fv(uinfo->location, 1, 0, uinfo->val);
	break;
      default: 
	break;
      }
    }
    entryPtr = Tcl_NextHashEntry(&searchEntry);
  }
  return 0;
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

/********************************************************************/
/*                           SHADER CODE                            */
/********************************************************************/

void printProgramInfoLog (GLuint program) {
  int max_length = 2048;
  int actual_length = 0;
  char log[2048];
  glGetProgramInfoLog (program, max_length, &actual_length, log);
  printf (" program info log for GL index %u:\n %s", program, log);
}

static GLenum LinkProgram(GLuint program, int verbose)
{
  glLinkProgram(program);

  // check if link was successful
  int params = -1;
  glGetProgramiv (program, GL_LINK_STATUS, &params);
  if (GL_TRUE != params) {
    fprintf (stderr, "ERROR: could not link shader program GL index %u\n", program);
    printProgramInfoLog (program);
    return -1;
  }
  return GL_NO_ERROR;
}


static void printShaderInfoLog(GLuint obj)
{
  int infologLength = 0;
  int charsWritten  = 0;
  char *infoLog;
  
  glGetShaderiv(obj, GL_INFO_LOG_LENGTH,&infologLength);
  
  if (infologLength > 0)
    {
      infoLog = (char *)malloc(infologLength);
      glGetShaderInfoLog(obj, infologLength, &charsWritten, infoLog);
      fprintf(getConsoleFP(), "%s\n",infoLog);
      free(infoLog);
    }
}

GLenum CompileProgram(GLenum target, const GLchar* sourcecode, GLuint *shader,
		      int verbose)
{
  if (sourcecode != 0)
    {
      *shader = glCreateShader(target);
      glShaderSource(*shader,1,(const GLchar **)&sourcecode,0);
      glCompileShader(*shader);
      
      // check for compile errors
      int params = -1;
      glGetShaderiv (*shader, GL_COMPILE_STATUS, &params);
      if (GL_TRUE != params) {
	fprintf (stderr, "ERROR: GL shader index %i did not compile \n", *shader);
	printShaderInfoLog (*shader);
	return -1;
      }
    }
  return GL_NO_ERROR;
}

static int build_prog(SHADER_PROG *sp, const char *v, const char *f, int verbose)
{
  int err;

  err = CompileProgram(GL_VERTEX_SHADER, v, &sp->vertShader, verbose);
  if (GL_NO_ERROR != err) {
    glDeleteShader(sp->fragShader);
    glDeleteShader(sp->vertShader);
    goto error;
  }

  err = CompileProgram(GL_FRAGMENT_SHADER, f, &sp->fragShader, verbose);
  if (GL_NO_ERROR != err) {
    glDeleteShader(sp->fragShader);
    goto error;
  }

  sp->program = glCreateProgram();
  glAttachShader(sp->program,sp->vertShader);
  glAttachShader(sp->program,sp->fragShader);

  err = LinkProgram(sp->program, verbose);

  if (GL_NO_ERROR != err) {
    glDeleteShader(sp->fragShader);
    glDeleteShader(sp->vertShader);
    glDeleteObjectARB(sp->program);
    fprintf(getConsoleFP(), "Program could not link");
    goto error;
  }

  return err;
  
 error:
  return -1;
}

#ifdef OLD_DRAW
void polygonDraw(GR_OBJ *g) 
{
  POLYGON *p = (POLYGON *) GR_CLIENTDATA(g);
  int i, j;

  glPushMatrix();
  glPushAttrib(GL_COLOR_BUFFER_BIT | GL_ENABLE_BIT);
  glRotated(p->angle, 0, 0, 1);
  if (p->colori >= 0) {
    glIndexi(p->colori); /* Color Index (for overlay) */
  }
  else glColor4fv(p->color);	      /* Normal (rgb mode)         */

  if (p->aa) {
    if (p->type == GL_LINES || 
	p->type == GL_POINTS ||
	p->type == GL_LINE_LOOP ||
	p->type == GL_LINE_STRIP) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_LINE_SMOOTH);
      glEnable(GL_POINT_SMOOTH);
      glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
      glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
    }
  }

  if (p->blend) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  if (p->tessellated) {
    glCallList(p->tessid);
  }
  else {
    if (p->filled) {
      glBegin(p->type);
    }
    else {
      glLineWidth(p->linewidth);
      glPointSize(p->pointsize);
      glBegin(p->type);
    }
    /* This should be substituted with vertex array... */
    if (p->three_d) {
      glEnable(GL_DEPTH_TEST);
      for (i = 0, j = 0; i < p->nverts; i++, j+=3) {
	glVertex3fv(&p->verts[j]);
      }
    }
    else {
      for (i = 0, j = 0; i < p->nverts; i++, j+=2) {
	glVertex2fv(&p->verts[j]);
      }
    }
    glEnd();
  }

  glPopAttrib();
  glPopMatrix();
}

#else
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

  glUseProgram(sp->program);
  update_uniforms(&p->uniformTable);
  glEnable (GL_DEPTH_TEST); // enable depth-testing
  glDepthFunc (GL_LESS); // depth-testing interprets a smaller value as "closer"
  if (p->vao_info->narrays) {
    glBindVertexArray(p->vao_info->vao);
    glDrawArrays(GL_TRIANGLES, 0, p->vao_info->nindices);
  }
  glUseProgram(0);
}
#endif

void polygonDelete(GR_OBJ *g) 
{
  POLYGON *p = (POLYGON *) GR_CLIENTDATA(g);
  if (p->verts) free(p->verts);
  if (p->tessellated) {
    glDeleteLists(p->tessid, 1);
  }

  delete_uniform_table(&p->uniformTable);
  delete_attrib_table(&p->attribTable);
  delete_vao_info(p->vao_info);

  if (p->modelviewMat->val) free(p->modelviewMat->val);
  if (p->projMat->val) free(p->projMat->val);
  if (p->uColor->val) free(p->uColor->val);
  
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

  /* To be replaced with dynamic ones at some point */
  static GLfloat qpoints[] = { -.5, -.5, 0.0,
			      -.5, .5, 0.0,
			      .5, .5, 0.0,
			      .5, -.5, 0.0 };
  
  GLfloat points[] = { 0.0f, 0.5f, 0.0f,
		       0.5f, -0.5f, 0.0f,
		       -0.5f, -0.5f, 0.0f };

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
  p->type = GL_POLYGON;
  p->colori = -1;		/* Don't use color index mode */

  p->linewidth = 1.0;
  
  /* Default polygon has no verts... They must be added! */
  p->nverts = 0;

  p->program = sp;
  copy_uniform_table(&sp->uniformTable, &p->uniformTable);
  copy_attrib_table(&sp->attribTable, &p->attribTable);

  p->vao_info = (VAO_INFO *) calloc(1, sizeof(VAO_INFO));
  p->vao_info->narrays = 0;
  glGenVertexArrays(1, &p->vao_info->vao);
  glBindVertexArray(p->vao_info->vao);


  if ((entryPtr = Tcl_FindHashEntry(&p->attribTable, "vertex_position"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    p->vao_info->npoints = sizeof(points)/sizeof(GLfloat);
    p->vao_info->points = 
      (GLfloat *) calloc(p->vao_info->npoints, sizeof(GLfloat));
    memcpy(p->vao_info->points, points, 
	   p->vao_info->npoints*sizeof(GLfloat));
    
    glGenBuffers(1, &p->vao_info->points_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, p->vao_info->points_vbo);
    glBufferData(GL_ARRAY_BUFFER, p->vao_info->npoints*sizeof(GLfloat),
		 p->vao_info->points, GL_STATIC_DRAW);
    
    
    glVertexAttribPointer(ainfo->location, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindBuffer(GL_ARRAY_BUFFER, p->vao_info->points_vbo);
    glEnableVertexAttribArray(ainfo->location);
    p->vao_info->nindices = p->vao_info->npoints/3;	/* tris */
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

  return(gobjAddObj(objlist, obj));
}


static int polygonCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;
  char *handle;
  if (argc < 1) {
    interp->result = "usage: polygon";
    return TCL_ERROR;
  }
  else handle = argv[1];

  if ((id = polygonCreate(olist, PolygonShaderProg)) < 0) {
    sprintf(interp->result,"error creating polygon");
    return(TCL_ERROR);
  }
  
  sprintf(interp->result,"%d", id);
  return(TCL_OK);
}

static int polytessCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  double *coords;		/* tess coords */
  GLUtesselator *tess;

  int i, j, k, id;
  
  if (argc < 2) {
    interp->result = "usage: polytess polygon";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a polygon object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != PolygonID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type polygon", NULL);
    return TCL_ERROR;
  }
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (!p->nverts) {
    Tcl_AppendResult(interp, argv[0], 
		     ": no verts in polygon objects to tesselate", NULL);
    return TCL_ERROR;
  }

  if (p->tessellated) {
    glDeleteLists(p->tessid, 1);
  }

  j = p->nverts*3;
  coords = (double *) calloc(j, sizeof(double));
  for (i = 0, j = 0, k = 0; i < p->nverts; i++) {
    coords[j++] = p->verts[k++];
    coords[j++] = p->verts[k++];
    if (p->three_d) 
      coords[j++] = p->verts[k++];
    else
      coords[j++] = 0.0;
  }

  p->tessid = glGenLists(1);

  tess = gluNewTess();
  glNewList(p->tessid, GL_COMPILE);
  gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
  gluTessCallback(tess, GLU_TESS_BEGIN, glBegin);  
  gluTessCallback(tess, GLU_TESS_VERTEX, glVertex3dv);  
  gluTessCallback(tess, GLU_TESS_END, glEnd);  
  gluTessBeginPolygon(tess, NULL); 
  gluTessBeginContour(tess); 
  for (i = 0, j = 0; i < p->nverts; i++, j+=3) {
    gluTessVertex(tess, &coords[j], &coords[j]);
  }
  gluTessEndContour(tess); 
  gluTessEndPolygon(tess); 

  glEndList();
  
  free(coords);
  gluDeleteTess(tess);
  p->tessellated = 1;

  return TCL_OK;
}

static int polyvertsCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  DYN_LIST *xlist, *ylist, *zlist;
  int type = 0, i;
  float *v;
  int id;

  if (argc < 4) {
    interp->result = "usage: polyverts polygon xlist ylist [zlist]";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != PolygonID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type polygon", NULL);
    return TCL_ERROR;
  }
    
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (tclFindDynList(interp, argv[2], &xlist) != TCL_OK) {
    return TCL_ERROR;
  }
  if (tclFindDynList(interp, argv[3], &ylist) != TCL_OK) {
    return TCL_ERROR;
  }
  if (argc > 4) {
    p->three_d = 1;
    if (tclFindDynList(interp, argv[4], &zlist) != TCL_OK) {
      return TCL_ERROR;
    }
  }
  else {
    p->three_d = 0;
  }
  
  if (DYN_LIST_N(xlist) != DYN_LIST_N(ylist)) {
    Tcl_AppendResult(interp, argv[0], 
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
    Tcl_AppendResult(interp, argv[0], 
		     ": verts must be either longs or floats", NULL);
    return TCL_ERROR;
  }

  /* Fix below */
  if (type != 0) {
    Tcl_AppendResult(interp, argv[0], 
		     ": verts must be all floats", NULL);
    return TCL_ERROR;
  }

  if (p->three_d) {
    if (DYN_LIST_DATATYPE(zlist) != DYN_LIST_DATATYPE(xlist)) {
      Tcl_AppendResult(interp, argv[0], 
		       ": z verts must be the same data type as x verts",NULL);
      return TCL_ERROR;
    }
    if (DYN_LIST_N(zlist) != DYN_LIST_N(xlist)) {
      Tcl_AppendResult(interp, argv[0], 
		       ": number of z verts must equal number of x verts",
		       NULL);
      return TCL_ERROR;
    }
  }

  /* free old verts and allocate verts space and use v as pointer */
  if (p->verts) free(p->verts);
  p->verts = (float *) calloc(DYN_LIST_N(xlist)*(p->three_d ? 3 : 2), 
				  sizeof(float));
  v = p->verts;

  switch (type) {
  case 0:
    {
      float *xvals, *yvals, *zvals;
      xvals = (float *) DYN_LIST_VALS(xlist);
      yvals = (float *) DYN_LIST_VALS(ylist);
      if (p->three_d) zvals = (float *) DYN_LIST_VALS(zlist);
      if (!p->three_d) {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	}
      }
      else {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	  *v++ = zvals[i];
	}
      }
      p->nverts = DYN_LIST_N(xlist);
    }
    break;
  case 1:
    {
      int *xvals, *zvals;
      float *yvals;
      xvals = (int *) DYN_LIST_VALS(xlist);
      yvals = (float *) DYN_LIST_VALS(ylist);
      if (p->three_d) zvals = (int *) DYN_LIST_VALS(zlist);
      if (!p->three_d) {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	}
      }
      else {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	  *v++ = zvals[i];
	}
      }
      p->nverts = DYN_LIST_N(xlist);
    }
    break;
  case 2:
    {
      float *xvals, *zvals;
      int *yvals;
      xvals = (float *) DYN_LIST_VALS(xlist);
      yvals = (int *) DYN_LIST_VALS(ylist);
      zvals = (float *) DYN_LIST_VALS(zlist);
      if (!p->three_d) {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	}
      }
      else {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	  *v++ = zvals[i];
	}
      }
      p->nverts = DYN_LIST_N(xlist);
    }
    break;
  case 3:
    {
      int *xvals, *yvals, *zvals;
      xvals = (int *) DYN_LIST_VALS(xlist);
      yvals = (int *) DYN_LIST_VALS(ylist);
      zvals = (int *) DYN_LIST_VALS(zlist);
      if (!p->three_d) {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	}
      }
      else {
	for (i = 0; i < DYN_LIST_N(xlist); i++) {
	  *v++ = xvals[i];
	  *v++ = yvals[i];
	  *v++ = zvals[i];
	}
      }
      p->nverts = DYN_LIST_N(xlist);
    }
    break;
  }
  return(TCL_OK);
}

static int polycolorCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  double r, g, b, a;
  int id;

  if (argc < 5) {
    interp->result = "usage: polycolor polygon r g b ?a?";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a polygon object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != PolygonID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type polygon", NULL);
    return TCL_ERROR;
  }
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &r) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &g) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &b) != TCL_OK) return TCL_ERROR;
  if (argc > 5) {
    if (Tcl_GetDouble(interp, argv[5], &a) != TCL_OK) return TCL_ERROR;
  }
  else {
    a = 1.0;
  }

  if (a < 1.) {
    p->blend = 1;
  }

  p->color[0] = r;
  p->color[1] = g;
  p->color[2] = b;
  p->color[3] = a;

  return(TCL_OK);
}

static int polycolorindexCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int n;
  int id;

  if (argc < 3) {
    interp->result = "usage: polycolorIndex polygon index";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a polygon object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != PolygonID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type polygon", NULL);
    return TCL_ERROR;
  }
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &n) != TCL_OK) return TCL_ERROR;

  p->colori = n;
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
    interp->result = "usage: polyfill polygon fill? linewidth";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a polygon object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != PolygonID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type polygon", NULL);
    return TCL_ERROR;
  }
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

static int polytypeCmd(ClientData clientData, Tcl_Interp *interp,
		       int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int id;
  double size;
  
  if (argc < 3) {
    interp->result = "usage: polytype polygon type";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a polygon object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != PolygonID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type polygon", NULL);
    return TCL_ERROR;
  }
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (!strcmp(argv[2], "quads") || !strcmp(argv[2], "QUADS")) {
    p->filled = 1;
    p->type = GL_QUADS;
  }
  else if (!strcmp(argv[2], "polygon") || !strcmp(argv[2], "POLYGON")) {
    p->filled = 1;
    p->type = GL_POLYGON;
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
  }

  return(TCL_OK);
}

static int polyangleCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int angle, id;
  
  if (argc < 3) {
    interp->result = "usage: polyangle polygon angle";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a polygon object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != PolygonID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type polygon", NULL);
    return TCL_ERROR;
  }
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &angle) != TCL_OK) return TCL_ERROR;
  p->angle = angle;

  return(TCL_OK);
}

static int polypointsizeCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int id;
  double size;
  
  if (argc < 3) {
    interp->result = "usage: polypointsize polygon pointsize";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a polygon object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != PolygonID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type polygon", NULL);
    return TCL_ERROR;
  }
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &size) != TCL_OK) return TCL_ERROR;
  p->pointsize = size;

  return(TCL_OK);
}


static int polyaaCmd(ClientData clientData, Tcl_Interp *interp,
		     int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  POLYGON *p;
  int id, aa;
  
  if (argc < 3) {
    interp->result = "usage: polyaa polygon aa?";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a polygon object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != PolygonID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type polygon", NULL);
    return TCL_ERROR;
  }
  p = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &aa) != TCL_OK) return TCL_ERROR;
  p->aa = aa;

  return(TCL_OK);
}


int polygonShaderCreate(Tcl_Interp *interp)
{
  PolygonShaderProg = (SHADER_PROG *) calloc(1, sizeof(SHADER_PROG));

  const char* vertex_shader =
    "# version 330\n"
    "in vec3 vertex_position;"
    "uniform mat4 projMat;"
    "uniform mat4 modelviewMat;"

    "void main () {"
    " gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);"
    "}";

  const char* fragment_shader =
    "# version 330\n"
    "uniform vec4 uColor;"
    "out vec4 frag_color;"
    "void main () {"
    " frag_color = vec4 (uColor);"
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

#ifdef WIN32
EXPORT(int,Polygon_Init) _ANSI_ARGS_((Tcl_Interp *interp))
#else
int Polygon_Init(Tcl_Interp *interp)
#endif
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
  
  if (PolygonID < 0) PolygonID = gobjRegisterType();
  polygonShaderCreate(interp);
  
  Tcl_CreateCommand(interp, "polygon", (Tcl_CmdProc *) polygonCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polyverts", (Tcl_CmdProc *) polyvertsCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polycolor", (Tcl_CmdProc *) polycolorCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polycolorIndex", 
		    (Tcl_CmdProc *) polycolorindexCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polytess", (Tcl_CmdProc *) polytessCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polyfill", (Tcl_CmdProc *) polyfillCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polytype", (Tcl_CmdProc *) polytypeCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polyangle", (Tcl_CmdProc *) polyangleCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polypointsize", (Tcl_CmdProc *) polypointsizeCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "polyaa", (Tcl_CmdProc *) polyaaCmd, 
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
