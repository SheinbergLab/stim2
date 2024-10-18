/* Shader utils to be used among loadable modules */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tcl.h>
#include <math.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h> 

#include <stim2.h>
#include "shaderutils.h"

#include "glsw.h"

char shaderPath[MAX_PATH];	/* where to find shaders */

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
    case GL_SAMPLER_2D_ARRAY: return "sampler2Darray";
    case GL_SAMPLER_CUBE: return "samplerCube";
    case GL_SAMPLER_2D_SHADOW: return "sampler2DShadow";
    default: break;
  }
  return "other";
}

int add_uniforms_to_table(Tcl_HashTable *utable, SHADER_PROG *sp)
{
  int verbose = 0;
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
      if (verbose)
	fprintf(stdout, "%s: %s\n", uinfo->name,
		GL_type_to_string(type));
    }
  }

  free(name);
  return TCL_OK;
}

int copy_uniform_table(Tcl_HashTable *source, Tcl_HashTable *dest)
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

int delete_uniform_table(Tcl_HashTable *utable)
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


int add_defaults_to_table(Tcl_Interp *interp, Tcl_HashTable *dtable,
			  char *shadername)
{
  char sname[256];
  const char *u;
  char *pch;
  Tcl_Size argc;
  char **argv;
  Tcl_HashEntry *entryPtr;
  int newentry;
  
  glswShutdown();
  glswInit();
  if (shaderPath[0] == 0) {
    glswSetPath("c:/stim/shaders/", ".glsl");
  }
  else {
    glswSetPath(shaderPath, ".glsl");
  }

  sprintf(sname, "%s.Uniforms", shadername);
  u = glswGetShader(sname);
  if (!u) {
    goto error;
  }
  
  /* For each line with text, set uniform to specified val */
  pch = (char *) strtok((char *) u, "\n");
  while (pch != NULL) {
    if (pch[0] != '#') {
      if (Tcl_SplitList(interp, pch, &argc, (const char ***) &argv) == TCL_OK) {
	if (argc == 2) {
	  entryPtr = Tcl_CreateHashEntry(dtable, argv[0], &newentry);
	  Tcl_SetHashValue(entryPtr, argv[1]);
	}
      }
    }
    pch = (char *) strtok(NULL, "\n");
  }

  glswShutdown();
  return 0;

 error:
  glswShutdown();
  return -1;
}

int delete_defaults_table(Tcl_HashTable *utable)
{
  /* All strings, so allocation is taken care of by Tcl */
  Tcl_DeleteHashTable(utable);
  return 0;
}

int add_attribs_to_table(Tcl_HashTable *atable, SHADER_PROG *sp)
{
  int verbose = 0;
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

  if (verbose)
    fprintf(stdout, "%d active attribs / maxlength = %d\n", 
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
      if (verbose) fprintf(stdout, "%s: %s [%d@%d]\n", 
			   ainfo->name, GL_type_to_string(type),
			   size, location);
    }
  }
  free(name);
  return TCL_OK;
}

int copy_attrib_table(Tcl_HashTable *source, Tcl_HashTable *dest)
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

int delete_attrib_table(Tcl_HashTable *atable)
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

int update_uniforms(Tcl_HashTable *utable)
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
      case GL_SAMPLER_2D_ARRAY:
      case GL_SAMPLER_3D:
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

int build_prog(SHADER_PROG *sp, const char *v, const char *f, int verbose)
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
    glDeleteProgram(sp->program);
    fprintf(stdout, "Program could not link");
    goto error;
  }

  return err;
  
 error:
  return -1;
}


int build_prog_from_file(SHADER_PROG *sp, char *shadername, int verbose)
{
  char sname[256];
  const char *f, *v;

  glswShutdown();
  glswInit();
  if (shaderPath[0] == 0) {
    glswSetPath("c:/stim/shaders/", ".glsl");
  }
  else {
    glswSetPath(shaderPath, ".glsl");
  }

#ifndef STIM2_USE_GLES
  glswAddDirectiveToken("", "#version 330");
#else
  glswAddDirectiveToken("", "#version 300 es");
#endif
  sprintf(sname, "%s.Vertex", shadername);
  v = glswGetShader(sname);
  if (!v) {
    fprintf(stdout, "%s", glswGetError());
    goto error;
  }

  sprintf(sname, "%s.Fragment", shadername);
  f = glswGetShader(sname);
  if (!f) {
    fprintf(stdout, "%s", glswGetError());
    goto error;
  }
  
  if (build_prog(sp, v, f, verbose) == -1) goto error;

  glswShutdown();
  return GL_NO_ERROR;

 error:

  glswShutdown();
  return -1;
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

GLenum LinkProgram(GLuint program, int verbose)
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


void printShaderInfoLog(GLuint obj)
{
  int infologLength = 0;
  int charsWritten  = 0;
  char *infoLog;
  
  glGetShaderiv(obj, GL_INFO_LOG_LENGTH,&infologLength);
  
  if (infologLength > 0)
    {
      infoLog = (char *)malloc(infologLength);
      glGetShaderInfoLog(obj, infologLength, &charsWritten, infoLog);
      fprintf(stdout, "%s\n", infoLog);
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
	fprintf (stderr, "ERROR: GL shader index %i did not compile \n",
		 *shader);
	printShaderInfoLog (*shader);
	return -1;
      }
    }
  return GL_NO_ERROR;
}
