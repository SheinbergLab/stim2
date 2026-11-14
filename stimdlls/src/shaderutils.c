/* Shader utils to be used among loadable modules */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tcl.h>
#include <math.h>

#ifdef _WIN32
#include <io.h>
#define F_OK 0
#define access _access
#else
#include <unistd.h>
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h> 

#include <stim2.h>
#include "shaderutils.h"

#include "glsw.h"

/********************************************************************/
/*                     SHADER PATH MANAGEMENT                       */
/********************************************************************/

/*
 * Shader search path - like PATH environment variable
 *
 * Two parts, searched in this order until a shader file is found:
 *
 *   - the user's list: shaderAddPath() appends, shaderPrependPath() puts
 *     first, shaderRemovePath() takes one out, shaderClearPaths() empties
 *     it, shaderSetPath() makes it one directory;
 *   - the system directory (shaderSetSystemPath): the shaders that ship
 *     with stim2.  It is not in the user's list, so nothing done to that
 *     list loses the shipped shaders, and a user directory searched before
 *     it can override one of them by name.
 */

#define MAX_SHADER_PATHS 16

static struct {
    char paths[MAX_SHADER_PATHS][MAX_PATH];
    int count;
    char system_path[MAX_PATH];    /* searched after the list; "" = none */
    char suffix[32];
    char resolved_path[MAX_PATH];  /* last successful path */
    int glsw_initialized;
} shader_config = {
    .count = 0,
    .suffix = ".glsl",
    .resolved_path = "",
    .glsw_initialized = 0
};

/* Legacy global for backwards compatibility */
char shaderPath[MAX_PATH];

static void glsw_forget(void)
{
    if (shader_config.glsw_initialized) {
        glswShutdown();
        shader_config.glsw_initialized = 0;
    }
}

/* the same directory, whether or not either is written with a trailing
   slash */
static int same_dir(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    while (la > 1 && (a[la-1] == '/' || a[la-1] == '\\')) la--;
    while (lb > 1 && (b[lb-1] == '/' || b[lb-1] == '\\')) lb--;
    return la == lb && strncmp(a, b, la) == 0;
}

static int find_path(const char *path)
{
    int i;
    for (i = 0; i < shader_config.count; i++)
        if (same_dir(shader_config.paths[i], path)) return i;
    return -1;
}

/*
 * shaderClearPaths - Empty the user's list (the system directory stays)
 */
void shaderClearPaths(void)
{
    shader_config.count = 0;
    shader_config.resolved_path[0] = '\0';
    shaderPath[0] = '\0';   /* or the legacy global would come back as a path */
    glsw_forget();
}

/*
 * shaderRemovePath - Take one directory out of the user's list
 *
 * Returns: 1 if it was there, 0 if not
 */
int shaderRemovePath(const char *path)
{
    int i, at;

    if (!path || !path[0]) return 0;
    if ((at = find_path(path)) < 0) return 0;
    for (i = at; i < shader_config.count - 1; i++)
        strcpy(shader_config.paths[i], shader_config.paths[i+1]);
    shader_config.count--;
    glsw_forget();
    return 1;
}

/*
 * shaderAddPath - Append a path to the user's list
 *
 * A directory already in the list is not added again (it keeps its place).
 * Returns: 1 on success, 0 if path list is full
 */
int shaderAddPath(const char *path)
{
    if (!path || !path[0]) return 0;
    if (find_path(path) >= 0) return 1;
    if (shader_config.count >= MAX_SHADER_PATHS) return 0;
    
    strncpy(shader_config.paths[shader_config.count], path, MAX_PATH - 1);
    shader_config.paths[shader_config.count][MAX_PATH - 1] = '\0';
    shader_config.count++;
    
    /* Reset glsw so it picks up new path on next use */
    glsw_forget();
    
    return 1;
}

/*
 * shaderPrependPath - Put a path at the front of the user's list
 *
 * Useful for letting one directory override the others.  A directory
 * already in the list is moved to the front.
 * Returns: 1 on success, 0 if path list is full
 */
int shaderPrependPath(const char *path)
{
    int i;
    char keep[MAX_PATH];
    
    if (!path || !path[0]) return 0;
    strncpy(keep, path, MAX_PATH - 1);   /* path may point into the list */
    keep[MAX_PATH - 1] = '\0';
    shaderRemovePath(keep);
    if (shader_config.count >= MAX_SHADER_PATHS) return 0;
    
    /* Shift existing paths down */
    for (i = shader_config.count; i > 0; i--) {
        strcpy(shader_config.paths[i], shader_config.paths[i-1]);
    }
    
    strcpy(shader_config.paths[0], keep);
    shader_config.count++;
    
    glsw_forget();
    
    return 1;
}

/*
 * shaderSetSystemPath - The directory of the shaders that ship with stim2,
 * searched after the user's list.  "" or NULL: none.
 */
void shaderSetSystemPath(const char *path)
{
    strncpy(shader_config.system_path, path ? path : "", MAX_PATH - 1);
    shader_config.system_path[MAX_PATH - 1] = '\0';
    glsw_forget();
}

const char *shaderGetSystemPath(void)
{
    return shader_config.system_path;
}

/*
 * shaderSetPath - Make the user's list this one directory (the system
 * directory is still searched after it)
 */
void shaderSetPath(const char *path)
{
    shaderClearPaths();
    shaderAddPath(path);
}

/*
 * shaderSetSuffix - Set shader file suffix (default: ".glsl")
 */
void shaderSetSuffix(const char *suffix)
{
    if (suffix) {
        strncpy(shader_config.suffix, suffix, sizeof(shader_config.suffix) - 1);
        shader_config.suffix[sizeof(shader_config.suffix) - 1] = '\0';
    }
}

/*
 * shaderGetSuffix - Get current shader file suffix
 */
const char *shaderGetSuffix(void)
{
    return shader_config.suffix;
}

/*
 * shaderGetPath - Get the resolved path for the last loaded shader
 */
const char *shaderGetPath(void)
{
    return shader_config.resolved_path;
}

/*
 * shaderGetPaths - Get all search paths as a colon-separated string
 *
 * Returns pointer to static buffer - copy if you need to keep it.
 */
const char *shaderGetPaths(void)
{
    static char pathlist[MAX_SHADER_PATHS * MAX_PATH];
    int i, offset = 0;
    
    pathlist[0] = '\0';
    
    for (i = 0; i < shader_config.count; i++) {
        if (i > 0) {
#ifdef _WIN32
            pathlist[offset++] = ';';
#else
            pathlist[offset++] = ':';
#endif
        }
        strcpy(pathlist + offset, shader_config.paths[i]);
        offset += strlen(shader_config.paths[i]);
    }
    
    return pathlist;
}

/*
 * shaderGetPathCount - Get number of paths in search list
 */
int shaderGetPathCount(void)
{
    return shader_config.count;
}

/*
 * shaderGetPathN - Get the Nth path (0-indexed)
 */
const char *shaderGetPathN(int n)
{
    if (n < 0 || n >= shader_config.count) return NULL;
    return shader_config.paths[n];
}

/*
 * get_default_paths - Add platform-specific default paths
 */
static void add_default_paths(void)
{
#ifdef _WIN32
    shaderAddPath("./shaders/");
    shaderAddPath("c:/stim/shaders/");
#elif defined(__APPLE__)
    shaderAddPath("./shaders/");
    shaderAddPath("/Applications/stim2.app/Contents/Resources/shaders/");
#else
    shaderAddPath("./shaders/");
    shaderAddPath("/usr/share/stim/shaders/");
    shaderAddPath("/usr/local/share/stim/shaders/");
#endif
}

/*
 * sync_legacy_path - Check if old global was set directly
 */
static void sync_legacy_path(void)
{
    if (shaderPath[0] && shader_config.count == 0) {
        shaderAddPath(shaderPath);
    }
}

/* is shadername's file in dir?  then dir is the resolved path */
static int try_dir(const char *dir, const char *shadername)
{
    char fullpath[MAX_PATH * 2];

    snprintf(fullpath, sizeof(fullpath), "%s%s%s",
             dir, shadername, shader_config.suffix);
    if (access(fullpath, F_OK) != 0) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s%s",
                 dir, shadername, shader_config.suffix);
        if (access(fullpath, F_OK) != 0) return 0;
    }
    strncpy(shader_config.resolved_path, dir, MAX_PATH - 1);
    shader_config.resolved_path[MAX_PATH - 1] = '\0';
    return 1;
}

/*
 * find_shader_file - Search for a shader file: the user's list in order,
 * then the system directory
 *
 * On success, stores the directory in shader_config.resolved_path and
 * returns 1. Returns 0 if not found.
 */
static int find_shader_file(const char *shadername)
{
    int i;
    
    sync_legacy_path();
    
    /* Nowhere to look at all (a module that set nothing): the defaults */
    if (shader_config.count == 0 && !shader_config.system_path[0]) {
        add_default_paths();
    }
    
    for (i = 0; i < shader_config.count; i++) {
        if (try_dir(shader_config.paths[i], shadername)) return 1;
    }
    if (shader_config.system_path[0] &&
        try_dir(shader_config.system_path, shadername)) return 1;
    
    /* Not found anywhere */
    return 0;
}

/*
 * ensure_glsw_initialized - Initialize glsw for the directory a shader was
 * found in
 *
 * glsw reads from one directory at a time, so it is set up again when the
 * shader just found is in another one than the last.  (This compared the
 * path with shader_config.resolved_path, which is the very string the
 * callers pass: always equal, so once a shader had been read from one
 * directory, one found in another could not be read.)
 */
static char glsw_dir[MAX_PATH];     /* the directory glsw is set up for */

static int ensure_glsw_initialized(const char *path)
{
    char pathbuf[MAX_PATH];
    size_t len;
    
    if (shader_config.glsw_initialized) {
        /* Check if we need to reinit for a different path */
        if (strcmp(path, glsw_dir) != 0) {
            glswShutdown();
            shader_config.glsw_initialized = 0;
        } else {
            return 1;
        }
    }
    
    /* Ensure path has trailing slash for glsw */
    len = strlen(path);
    if (len > 0 && len < MAX_PATH - 2) {
        strcpy(pathbuf, path);
        if (pathbuf[len-1] != '/' && pathbuf[len-1] != '\\') {
            pathbuf[len] = '/';
            pathbuf[len+1] = '\0';
        }
    } else {
        strncpy(pathbuf, path, MAX_PATH - 1);
        pathbuf[MAX_PATH - 1] = '\0';
    }
    
    glswInit();
    glswSetPath(pathbuf, shader_config.suffix);
    
#ifndef STIM2_USE_GLES
    glswAddDirectiveToken("", "#version 330");
#else
    glswAddDirectiveToken("", "#version 300 es");
    glswAddDirectiveToken("Fragment", "precision highp float;");
    glswAddDirectiveToken("Fragment", "precision highp int;");
#endif
    
    strncpy(glsw_dir, path, MAX_PATH - 1);
    glsw_dir[MAX_PATH - 1] = '\0';
    if (path != shader_config.resolved_path) {
        strncpy(shader_config.resolved_path, path, MAX_PATH - 1);
        shader_config.resolved_path[MAX_PATH - 1] = '\0';
    }
    shader_config.glsw_initialized = 1;
    
    return 1;
}

/*
 * shaderReset - Force reinitialization on next shader load
 */
void shaderReset(void)
{
    if (shader_config.glsw_initialized) {
        glswShutdown();
        shader_config.glsw_initialized = 0;
    }
    shader_config.resolved_path[0] = '\0';
}

/*
 * shaderShutdown - Clean shutdown of shader system
 */
void shaderShutdown(void)
{
    shaderReset();
    shaderClearPaths();
}

/********************************************************************/
/*                        UTILITY FUNCTIONS                         */
/********************************************************************/

char *GL_type_to_string(GLenum type) {
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

static char *get_uniform_basename(const char *uniformName) {
    char *baseName = strdup(uniformName); 
    if (!baseName) return NULL;

    /* Find opening bracket if it exists (for arrays) */
    char *bracketPos = strchr(baseName, '[');
    if (bracketPos) {
        *bracketPos = '\0';
    }

    return baseName;
}

/********************************************************************/
/*                       UNIFORM MANAGEMENT                         */
/********************************************************************/

int add_uniforms_to_table(Tcl_HashTable *utable, SHADER_PROG *sp)
{
    int verbose = 0;
    int total = -1, maxlength = -1;
    int i;
    char *name;
    UNIFORM_INFO *uinfo;
    Tcl_HashEntry *entryPtr;
    int newentry;

    glGetProgramiv(sp->program, GL_ACTIVE_UNIFORMS, &total); 
    if (total <= 0) 
        return TCL_OK;

    glGetProgramiv(sp->program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxlength);
    name = malloc(maxlength + 1);

    for (i = 0; i < total; i++) {
        int name_len = -1, size = -1;
        GLenum type = GL_ZERO;
        GLint location;

        glGetActiveUniform(sp->program, i, maxlength,
                           &name_len, &size, &type, name);
        name[name_len] = 0;
        location = glGetUniformLocation(sp->program, name);

        if (location >= 0) {
            uinfo = (UNIFORM_INFO *)calloc(1, sizeof(UNIFORM_INFO));
            uinfo->name = get_uniform_basename(name);
            uinfo->type = type;
            uinfo->size = size;
            uinfo->location = location;
            uinfo->val = NULL;
            entryPtr = Tcl_CreateHashEntry(&sp->uniformTable, uinfo->name, &newentry);
            Tcl_SetHashValue(entryPtr, uinfo);
            if (verbose)
                fprintf(stdout, "%s: %s (%d)\n", uinfo->name,
                        GL_type_to_string(type), uinfo->size);
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
        new_uinfo = (UNIFORM_INFO *)calloc(1, sizeof(UNIFORM_INFO));
        new_uinfo->name = strdup(uinfo->name);
        new_uinfo->type = uinfo->type;
        new_uinfo->size = uinfo->size;
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
        if (uinfo->name) free(uinfo->name);
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
    char *pch, *u_copy;
    Tcl_Size argc;
    char **argv;
    Tcl_HashEntry *entryPtr;
    int newentry;

    /* Find the shader file in the search path */
    if (!find_shader_file(shadername)) {
        /* Not found - not necessarily an error, might not have defaults */
        return 0;
    }
    
    ensure_glsw_initialized(shader_config.resolved_path);

    sprintf(sname, "%s.Uniforms", shadername);
    u = glswGetShader(sname);
    if (!u) {
        /* No uniforms section is not an error */
        return 0;
    }

    /* glswGetShader returns pointer to internal buffer, so copy it
     * since strtok modifies the string */
    u_copy = strdup(u);
    if (!u_copy) return -1;

    /* For each line with text, set uniform to specified val */
    pch = strtok(u_copy, "\n");
    while (pch != NULL) {
        if (pch[0] != '#') {
            if (Tcl_SplitList(interp, pch, &argc, (const char ***)&argv) == TCL_OK) {
                if (argc == 2) {
		  /* Rejoin argv[1..argc-1] as the value string */
		  Tcl_DString ds;
		  Tcl_DStringInit(&ds);
		  for (int i = 1; i < argc; i++) {
		    if (i > 1) Tcl_DStringAppend(&ds, " ", 1);
		    Tcl_DStringAppend(&ds, argv[i], -1);
		  }
		  entryPtr = Tcl_CreateHashEntry(dtable, argv[0], &newentry);
		  Tcl_SetHashValue(entryPtr, strdup(Tcl_DStringValue(&ds)));
		  Tcl_DStringFree(&ds);		  
                }
                Tcl_Free((char *)argv);
            }
        }
        pch = strtok(NULL, "\n");
    }

    free(u_copy);
    return 0;
}

int delete_defaults_table(Tcl_HashTable *dtable)
{
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch searchEntry;
    char *val;

    /* Free the strdup'd values */
    entryPtr = Tcl_FirstHashEntry(dtable, &searchEntry);
    while (entryPtr) {
        val = Tcl_GetHashValue(entryPtr);
        if (val) free(val);
        entryPtr = Tcl_NextHashEntry(&searchEntry);
    }
    Tcl_DeleteHashTable(dtable);
    return 0;
}

/********************************************************************/
/*                       ATTRIBUTE MANAGEMENT                       */
/********************************************************************/

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
    name = malloc(maxlength + 1);

    if (verbose)
        fprintf(stdout, "%d active attribs / maxlength = %d\n", 
                total, maxlength);

    for (i = 0; i < total; i++) {
        int name_len = -1, size = -1;
        GLenum type = GL_ZERO;
        GLint location;

        glGetActiveAttrib(sp->program, i, maxlength,
                          &name_len, &size, &type, name);
        name[name_len] = 0;
        location = glGetAttribLocation(sp->program, name);
        if (location >= 0) {
            ainfo = (ATTRIB_INFO *)calloc(1, sizeof(ATTRIB_INFO));
            ainfo->name = strdup(name);
            ainfo->size = size;
            ainfo->type = type;
            ainfo->location = location;
            entryPtr = Tcl_CreateHashEntry(&sp->attribTable, ainfo->name, &newentry);
            Tcl_SetHashValue(entryPtr, ainfo);
            if (verbose)
                fprintf(stdout, "%s: %s [%d@%d]\n", 
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
        new_ainfo = (ATTRIB_INFO *)calloc(1, sizeof(ATTRIB_INFO));
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
        ainfo = (ATTRIB_INFO *)Tcl_GetHashValue(entryPtr);
        if (ainfo->name) free(ainfo->name);
        free(ainfo);
    }
    Tcl_DeleteHashTable(atable);

    return 0;
}

/********************************************************************/
/*                       UNIFORM UPDATING                           */
/********************************************************************/

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
                glUniform1iv(uinfo->location, uinfo->size, uinfo->val);
                break;
            case GL_BOOL_VEC2:
            case GL_INT_VEC2:
                glUniform2iv(uinfo->location, uinfo->size, uinfo->val);
                break;
            case GL_BOOL_VEC3:
            case GL_INT_VEC3:
                glUniform3iv(uinfo->location, uinfo->size, uinfo->val);
                break;
            case GL_BOOL_VEC4:
            case GL_INT_VEC4:
                glUniform4iv(uinfo->location, uinfo->size, uinfo->val);
                break;
            case GL_FLOAT:
                glUniform1fv(uinfo->location, uinfo->size, uinfo->val);
                break;
            case GL_FLOAT_VEC2:
                glUniform2fv(uinfo->location, uinfo->size, uinfo->val);
                break;
            case GL_FLOAT_VEC3:
                glUniform3fv(uinfo->location, uinfo->size, uinfo->val);
                break;
            case GL_FLOAT_VEC4:
                glUniform4fv(uinfo->location, uinfo->size, uinfo->val);
                break;
            case GL_FLOAT_MAT2:
                glUniformMatrix2fv(uinfo->location, uinfo->size, 0, uinfo->val);
                break;
            case GL_FLOAT_MAT3:
                glUniformMatrix3fv(uinfo->location, uinfo->size, 0, uinfo->val);
                break;
            case GL_FLOAT_MAT4:
                glUniformMatrix4fv(uinfo->location, uinfo->size, 0, uinfo->val);
                break;
            default: 
                break;
            }
        }
        entryPtr = Tcl_NextHashEntry(&searchEntry);
    }
    return 0;
}

/********************************************************************/
/*                  UNIFORM VALUES FROM AND TO TCL                  */
/********************************************************************/

/*
 * shader_uniform_set - set a uniform in a table from a Tcl list of
 * numbers, by the uniform's own type (bool, int, float, vec2-4, mat2-4,
 * and arrays of them).  shader_uniform_get returns the value as a list.
 * Shared by the modules that keep uniform tables (shader, shaderpass).
 */
int shader_uniform_set(Tcl_Interp *interp, Tcl_HashTable *table,
                       const char *shader_name, const char *name,
                       const char *valstr)
{
  Tcl_HashEntry *entryPtr;
  UNIFORM_INFO *uinfo;
  Tcl_Obj *listObj;
  Tcl_Obj **elements;
  Tcl_Size elementCount;
  int totalNumbersRequired = 0;
  int i;

  Tcl_ResetResult(interp);
  
  if ((entryPtr = Tcl_FindHashEntry(table, name))) {
    uinfo = (UNIFORM_INFO *)Tcl_GetHashValue(entryPtr);
  }
  else {
    Tcl_AppendResult(interp, "uniform \"", name, "\" not found in shader \"",
                     shader_name, "\"", NULL);
    return TCL_ERROR;
  }

  listObj = Tcl_NewStringObj(valstr, -1);
  Tcl_IncrRefCount(listObj);

  // Determine the total number of individual numbers required based on the type
  switch (uinfo->type) {
  case GL_BOOL:
  case GL_INT:
  case GL_FLOAT:
    totalNumbersRequired = uinfo->size; // 1 number per element
    break;
  case GL_FLOAT_VEC2:
    totalNumbersRequired = uinfo->size * 2; // 2 numbers per element
    break;
  case GL_FLOAT_VEC3:
    totalNumbersRequired = uinfo->size * 3; // 3 numbers per element
    break;
  case GL_FLOAT_VEC4:
  case GL_FLOAT_MAT2:
    totalNumbersRequired = uinfo->size * 4; // 4 numbers per element
    break;
  case GL_FLOAT_MAT3:
    totalNumbersRequired = uinfo->size * 9; // 9 numbers per element
    break;
  case GL_FLOAT_MAT4:
    totalNumbersRequired = uinfo->size * 16; // 16 numbers per element
    break;
  default:
    Tcl_DecrRefCount(listObj);
    Tcl_AppendResult(interp,
		     "unsupported uniform type for \"", name, "\"", NULL);
    return TCL_ERROR;
  }

  // Split the input string into individual components
  if (Tcl_ListObjGetElements(interp, listObj,
			     &elementCount, &elements) != TCL_OK) {
    Tcl_DecrRefCount(listObj);
    Tcl_AppendResult(interp, "failed to parse uniform value: \"",
		     valstr, "\"", NULL);
    return TCL_ERROR;
  }
	
  // Check if the number of elements is a multiple of the total numbers required
  if (elementCount > totalNumbersRequired) {
      Tcl_DecrRefCount(listObj);
      /* (this passed the two counts to Tcl_AppendResult as if they were
         strings, and crashed) */
      Tcl_SetObjResult(interp,
		       Tcl_ObjPrintf("uniform \"%s\" expects no more than %d values but got %d",
				     name, totalNumbersRequired, (int) elementCount));
    return TCL_ERROR;
  }

  // Parse and store the values based on the uniform type
  if (uinfo->type == GL_BOOL || uinfo->type == GL_INT) {
    if (!uinfo->val) {
      uinfo->val = malloc(totalNumbersRequired * sizeof(int));
      if (!uinfo->val){
        Tcl_DecrRefCount(listObj);
        Tcl_AppendResult(interp,
			 "memory allocation failed for uniform \"",
			 name, "\"", NULL);
        return TCL_ERROR;
      }
    }
    int ival;
    for (i = 0; i < elementCount; i++) {
      if (Tcl_GetIntFromObj(interp, elements[i], &ival) != TCL_OK) {
        Tcl_DecrRefCount(listObj);
        return TCL_ERROR;
      }
      ((int *)uinfo->val)[i] = ival;
    }
  }
  else {
    for (i = 0; i < elementCount; i++) {
      if (!uinfo->val) {
        uinfo->val = malloc(totalNumbersRequired * sizeof(float));
        if (!uinfo->val)
        {
          Tcl_DecrRefCount(listObj);
          Tcl_AppendResult(interp,
			   "memory allocation failed for uniform \"",
			   name, "\"", NULL);
          return TCL_ERROR;
        }
      }
      double fval;
      if (Tcl_GetDoubleFromObj(interp, elements[i], &fval) != TCL_OK) {
        Tcl_DecrRefCount(listObj);
        return TCL_ERROR;
      }
      ((float *)uinfo->val)[i] = (float)fval;
    }
  }

  Tcl_DecrRefCount(listObj);
  return TCL_OK;
}

int shader_uniform_get(Tcl_Interp *interp, Tcl_HashTable *table,
                       const char *shader_name, const char *name)
{
  Tcl_HashEntry *entryPtr;
  UNIFORM_INFO *uinfo;
  Tcl_Obj *listObj;
  int *ival;
  float *fvals;
  int i, j, elementsPerUniform;

  if ((entryPtr = Tcl_FindHashEntry(table, name))) {
    uinfo = (UNIFORM_INFO *)Tcl_GetHashValue(entryPtr);
  } else {
    Tcl_AppendResult(interp, "uniform \"", name, "\" not found in shader \"",
                     shader_name, "\"", NULL);
    return TCL_ERROR;
  }

  if (!uinfo->val) {
    Tcl_AppendResult(interp, "uniform \"", name, "\" has no value set", NULL);
    return TCL_ERROR;
  }
 
  // Create a new Tcl list object to hold the uniform values
  listObj = Tcl_NewListObj(0, NULL);

  // Determine the number of elements per uniform based on the type
  switch (uinfo->type) {
  case GL_BOOL:
  case GL_INT:
  case GL_FLOAT:
    elementsPerUniform = 1;
    break;
  case GL_FLOAT_VEC2:
    elementsPerUniform = 2;
    break;
  case GL_FLOAT_VEC3:
    elementsPerUniform = 3;
    break;
  case GL_FLOAT_VEC4:
  case GL_FLOAT_MAT2:
    elementsPerUniform = 4;
    break;
  case GL_FLOAT_MAT3:
    elementsPerUniform = 9;
    break;
  case GL_FLOAT_MAT4:
    elementsPerUniform = 16;
    break;
  default:
    Tcl_AppendResult(interp, "unsupported uniform type for \"", name, "\"", NULL);
    return TCL_ERROR;
  }

  // Iterate over the uniform values and add them to the list
  if (uinfo->type == GL_BOOL || uinfo->type == GL_INT) {
    ival = (int *)uinfo->val;
    for (i = 0; i < uinfo->size; i++) {
      for (j = 0; j < elementsPerUniform; j++) {
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(ival[i * elementsPerUniform + j]));
      }
    }
  } else {
    fvals = (float *)uinfo->val;
    for (i = 0; i < uinfo->size; i++) {
      for (j = 0; j < elementsPerUniform; j++) {
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(fvals[i * elementsPerUniform + j]));
      }
    }
  }

  // Set the Tcl list as the result
  Tcl_SetObjResult(interp, listObj);
  return TCL_OK;
}

/********************************************************************/
/*                       SHADER COMPILATION                         */
/********************************************************************/

void printProgramInfoLog(GLuint program) {
    int max_length = 2048;
    int actual_length = 0;
    char log[2048];
    glGetProgramInfoLog(program, max_length, &actual_length, log);
    printf("program info log for GL index %u:\n%s", program, log);
}

void printShaderInfoLog(GLuint obj)
{
    int infologLength = 0;
    int charsWritten = 0;
    char *infoLog;

    glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &infologLength);

    if (infologLength > 0) {
        infoLog = (char *)malloc(infologLength);
        glGetShaderInfoLog(obj, infologLength, &charsWritten, infoLog);
        fprintf(stdout, "%s\n", infoLog);
        free(infoLog);
    }
}

GLenum CompileProgram(GLenum target, const GLchar *sourcecode, GLuint *shader,
                      int verbose)
{
    if (sourcecode != 0) {
        *shader = glCreateShader(target);
        glShaderSource(*shader, 1, (const GLchar **)&sourcecode, 0);
        glCompileShader(*shader);

        /* Check for compile errors */
        int params = -1;
        glGetShaderiv(*shader, GL_COMPILE_STATUS, &params);
        if (GL_TRUE != params) {
            fprintf(stderr, "ERROR: GL shader index %i did not compile\n", *shader);
            printShaderInfoLog(*shader);
            return -1;
        }
    }
    return GL_NO_ERROR;
}

GLenum LinkProgram(GLuint program, int verbose)
{
    glLinkProgram(program);

    /* Check if link was successful */
    int params = -1;
    glGetProgramiv(program, GL_LINK_STATUS, &params);
    if (GL_TRUE != params) {
        fprintf(stderr, "ERROR: could not link shader program GL index %u\n", program);
        printProgramInfoLog(program);
        return -1;
    }
    return GL_NO_ERROR;
}

int build_prog(SHADER_PROG *sp, const char *v, const char *f, int verbose)
{
    int err;

    err = CompileProgram(GL_VERTEX_SHADER, v, &sp->vertShader, verbose);
    if (GL_NO_ERROR != err) {
        glDeleteShader(sp->vertShader);
        return -1;
    }

    err = CompileProgram(GL_FRAGMENT_SHADER, f, &sp->fragShader, verbose);
    if (GL_NO_ERROR != err) {
        glDeleteShader(sp->fragShader);
        glDeleteShader(sp->vertShader);
        return -1;
    }

    sp->program = glCreateProgram();
    glAttachShader(sp->program, sp->vertShader);
    glAttachShader(sp->program, sp->fragShader);

    err = LinkProgram(sp->program, verbose);

    if (GL_NO_ERROR != err) {
        glDeleteShader(sp->fragShader);
        glDeleteShader(sp->vertShader);
        glDeleteProgram(sp->program);
        fprintf(stdout, "Program could not link\n");
        return -1;
    }

    return GL_NO_ERROR;
}

/*
 * The vertex shader of a quad placed in the scene, for a shader file (or
 * an inline build) that gives only a fragment shader: the position by the
 * projection and modelview matrices, the texture coordinate passed on as
 * `texcoord`.  It is what most files in shaders/ spell out themselves.
 */
const char *shaderDefaultVertexSource(void)
{
    return
#ifndef STIM2_USE_GLES
        "#version 330\n"
#else
        "#version 300 es\n"
#endif
        "in vec3 vertex_position;\n"
        "in vec2 vertex_texcoord;\n"
        "out vec2 texcoord;\n"
        "uniform mat4 projMat;\n"
        "uniform mat4 modelviewMat;\n"
        "void main(void)\n"
        "{\n"
        "  texcoord = vertex_texcoord;\n"
        "  gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);\n"
        "}\n";
}

int build_prog_from_file(SHADER_PROG *sp, char *shadername, int verbose)
{
    return build_prog_from_file_ex(sp, shadername, verbose, NULL);
}

/*
 * build_prog_from_file_ex - as build_prog_from_file, but a file with no
 * "-- Vertex" section is built with default_vertex (complete source, with
 * its #version) when one is given.  NULL: such a file is an error, as it
 * always was -- a module whose vertex shader needs other attributes (mesh)
 * has no default to offer.
 */
int build_prog_from_file_ex(SHADER_PROG *sp, char *shadername, int verbose,
                            const char *default_vertex)
{
    char sname[256];
    const char *f, *v;

    /* Find the shader in search path */
    if (!find_shader_file(shadername)) {
        fprintf(stderr, "ERROR: shader '%s%s' not found in search path:\n",
                shadername, shader_config.suffix);
        for (int i = 0; i < shader_config.count; i++) {
            fprintf(stderr, "  %s\n", shader_config.paths[i]);
        }
        if (shader_config.system_path[0])
            fprintf(stderr, "  %s\n", shader_config.system_path);
        return -1;
    }
    
    if (verbose) {
        fprintf(stdout, "Loading shader '%s' from %s\n", 
                shadername, shader_config.resolved_path);
    }
    
    ensure_glsw_initialized(shader_config.resolved_path);

    sprintf(sname, "%s.Fragment", shadername);
    f = glswGetShader(sname);
    if (!f) {
        fprintf(stderr, "ERROR: %s\n", glswGetError());
        return -1;
    }

    sprintf(sname, "%s.Vertex", shadername);
    v = glswGetShader(sname);
    if (!v) {
        if (!default_vertex) {
            fprintf(stderr, "ERROR: %s\n", glswGetError());
            return -1;
        }
        v = default_vertex;
    }

    return build_prog(sp, v, f, verbose);
}
