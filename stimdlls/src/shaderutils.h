/* shaderutils.h - Shader utilities for stim2 */

#ifndef SHADERUTILS_H
#define SHADERUTILS_H

#include <tcl.h>
#include <glad/glad.h>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

/*
 * Shader path management
 */
void shaderClearPaths(void);
int  shaderAddPath(const char *path);
int  shaderPrependPath(const char *path);
void shaderSetPath(const char *path);       /* legacy - sets single path */

const char *shaderGetPaths(void);           /* colon-separated list */
int         shaderGetPathCount(void);
const char *shaderGetPathN(int n);          /* get Nth path (0-indexed) */
const char *shaderGetPath(void);            /* last resolved path */

void        shaderSetSuffix(const char *suffix);
const char *shaderGetSuffix(void);

void shaderReset(void);
void shaderShutdown(void);

/* Legacy global - deprecated, use shaderSetPath() instead */
extern char shaderPath[MAX_PATH];

/*
 * Uniform info structure
 */
typedef struct {
    char *name;
    GLenum type;
    int size;
    GLint location;
    void *val;
} UNIFORM_INFO;

/*
 * Attribute info structure
 */
typedef struct {
    char *name;
    GLenum type;
    int size;
    GLint location;
} ATTRIB_INFO;

/*
 * Shader program structure
 * 
 * Contains compiled shader program plus metadata tables.
 */
typedef struct {
    GLuint program;
    GLuint vertShader;
    GLuint fragShader;
    char name[64];                  /* shader name (e.g., "shader0") */
    Tcl_HashTable uniformTable;     /* active uniforms from program */
    Tcl_HashTable attribTable;      /* active attributes from program */
    Tcl_HashTable defaultsTable;    /* default values from .glsl file */
} SHADER_PROG;

/*
 * Utility functions
 */
char *GL_type_to_string(GLenum type);

/*
 * Uniform table management
 */
int add_uniforms_to_table(Tcl_HashTable *utable, SHADER_PROG *sp);
int copy_uniform_table(Tcl_HashTable *source, Tcl_HashTable *dest);
int delete_uniform_table(Tcl_HashTable *utable);
int update_uniforms(Tcl_HashTable *utable);

/*
 * Defaults table management (for -- Uniforms section in .glsl files)
 */
int add_defaults_to_table(Tcl_Interp *interp, Tcl_HashTable *dtable, char *shadername);
int delete_defaults_table(Tcl_HashTable *dtable);

/*
 * Attribute table management
 */
int add_attribs_to_table(Tcl_HashTable *atable, SHADER_PROG *sp);
int copy_attrib_table(Tcl_HashTable *source, Tcl_HashTable *dest);
int delete_attrib_table(Tcl_HashTable *atable);

/*
 * Shader compilation
 */
GLenum CompileProgram(GLenum target, const GLchar *sourcecode, GLuint *shader, int verbose);
GLenum LinkProgram(GLuint program, int verbose);
int build_prog(SHADER_PROG *sp, const char *v, const char *f, int verbose);
int build_prog_from_file(SHADER_PROG *sp, char *shadername, int verbose);

/*
 * Debug output
 */
void printProgramInfoLog(GLuint program);
void printShaderInfoLog(GLuint obj);

#endif /* SHADERUTILS_H */