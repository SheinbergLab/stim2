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
 * Paths are searched in order until a shader file is found.
 * Use shaderAddPath() to append paths, shaderPrependPath() to prepend.
 */

#define MAX_SHADER_PATHS 16

static struct {
    char paths[MAX_SHADER_PATHS][MAX_PATH];
    int count;
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

/*
 * shaderClearPaths - Remove all search paths
 */
void shaderClearPaths(void)
{
    shader_config.count = 0;
    shader_config.resolved_path[0] = '\0';
    
    if (shader_config.glsw_initialized) {
        glswShutdown();
        shader_config.glsw_initialized = 0;
    }
}

/*
 * shaderAddPath - Append a path to the search list
 *
 * Path should include trailing slash (e.g., "/usr/share/stim/shaders/")
 * Returns: 1 on success, 0 if path list is full
 */
int shaderAddPath(const char *path)
{
    if (!path || !path[0]) return 0;
    if (shader_config.count >= MAX_SHADER_PATHS) return 0;
    
    strncpy(shader_config.paths[shader_config.count], path, MAX_PATH - 1);
    shader_config.paths[shader_config.count][MAX_PATH - 1] = '\0';
    shader_config.count++;
    
    /* Reset glsw so it picks up new path on next use */
    if (shader_config.glsw_initialized) {
        glswShutdown();
        shader_config.glsw_initialized = 0;
    }
    
    return 1;
}

/*
 * shaderPrependPath - Add a path to the front of the search list
 *
 * Useful for letting user paths override system paths.
 * Returns: 1 on success, 0 if path list is full
 */
int shaderPrependPath(const char *path)
{
    int i;
    
    if (!path || !path[0]) return 0;
    if (shader_config.count >= MAX_SHADER_PATHS) return 0;
    
    /* Shift existing paths down */
    for (i = shader_config.count; i > 0; i--) {
        strcpy(shader_config.paths[i], shader_config.paths[i-1]);
    }
    
    strncpy(shader_config.paths[0], path, MAX_PATH - 1);
    shader_config.paths[0][MAX_PATH - 1] = '\0';
    shader_config.count++;
    
    if (shader_config.glsw_initialized) {
        glswShutdown();
        shader_config.glsw_initialized = 0;
    }
    
    return 1;
}

/*
 * shaderSetPath - Set a single path (replaces all existing paths)
 *
 * For backward compatibility with old code.
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
    shaderAddPath("/usr/local/share/stim/shaders/");
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

/*
 * find_shader_file - Search for a shader file in the path list
 *
 * Tries each path in order until the file is found.
 * On success, stores the full directory path in shader_config.resolved_path
 * and returns 1. Returns 0 if not found.
 */
static int find_shader_file(const char *shadername)
{
    char fullpath[MAX_PATH * 2];
    int i;
    
    sync_legacy_path();
    
    /* If no paths configured, add defaults */
    if (shader_config.count == 0) {
        add_default_paths();
    }
    
    /* Try each path in order */
    for (i = 0; i < shader_config.count; i++) {
        snprintf(fullpath, sizeof(fullpath), "%s%s%s",
                 shader_config.paths[i], shadername, shader_config.suffix);
        
        if (access(fullpath, F_OK) == 0) {
            /* Found it - save the successful path */
            strncpy(shader_config.resolved_path, shader_config.paths[i], MAX_PATH - 1);
            shader_config.resolved_path[MAX_PATH - 1] = '\0';
            return 1;
        }
    }
    
    /* Not found in any path */
    return 0;
}

/*
 * ensure_glsw_initialized - Initialize glsw with current resolved path
 */
static int ensure_glsw_initialized(const char *path)
{
    if (shader_config.glsw_initialized) {
        /* Check if we need to reinit for a different path */
        if (strcmp(path, shader_config.resolved_path) != 0) {
            glswShutdown();
            shader_config.glsw_initialized = 0;
        } else {
            return 1;
        }
    }
    
    glswInit();
    glswSetPath(path, shader_config.suffix);
    
#ifndef STIM2_USE_GLES
    glswAddDirectiveToken("", "#version 330");
#else
    glswAddDirectiveToken("", "#version 300 es");
#endif
    
    strncpy(shader_config.resolved_path, path, MAX_PATH - 1);
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
                    entryPtr = Tcl_CreateHashEntry(dtable, argv[0], &newentry);
                    Tcl_SetHashValue(entryPtr, strdup(argv[1]));
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

int build_prog_from_file(SHADER_PROG *sp, char *shadername, int verbose)
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
        return -1;
    }
    
    if (verbose) {
        fprintf(stdout, "Loading shader '%s' from %s\n", 
                shadername, shader_config.resolved_path);
    }
    
    ensure_glsw_initialized(shader_config.resolved_path);

    sprintf(sname, "%s.Vertex", shadername);
    v = glswGetShader(sname);
    if (!v) {
        fprintf(stderr, "ERROR: %s\n", glswGetError());
        return -1;
    }

    sprintf(sname, "%s.Fragment", shadername);
    f = glswGetShader(sname);
    if (!f) {
        fprintf(stderr, "ERROR: %s\n", glswGetError());
        return -1;
    }

    return build_prog(sp, v, f, verbose);
}