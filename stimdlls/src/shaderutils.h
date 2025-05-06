

#ifndef MAX_PATH
#define MAX_PATH 512
#endif

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
  GLint size;
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


#ifdef __cplusplus
extern "C" {
#endif
  extern char shaderPath[];
  
  char *GL_type_to_string (GLenum type);
  int add_uniforms_to_table(Tcl_HashTable *utable, SHADER_PROG *sp);
  int copy_uniform_table(Tcl_HashTable *source, Tcl_HashTable *dest);
  int delete_uniform_table(Tcl_HashTable *utable);
  int add_defaults_to_table(Tcl_Interp *interp, Tcl_HashTable *dtable,
			    char *shadername);
  int delete_defaults_table(Tcl_HashTable *utable);
  int add_attribs_to_table(Tcl_HashTable *atable, SHADER_PROG *sp);
  int copy_attrib_table(Tcl_HashTable *source, Tcl_HashTable *dest);
  int delete_attrib_table(Tcl_HashTable *atable);
  int update_uniforms(Tcl_HashTable *utable);
  int build_prog(SHADER_PROG *sp, const char *v, const char *f, int verbose);
  int build_prog_from_file(SHADER_PROG *sp, char *shadername, int verbose);
  void printProgramInfoLog (GLuint program);
  GLenum LinkProgram(GLuint program, int verbose);
  void printShaderInfoLog(GLuint obj);
  GLenum CompileProgram(GLenum target, const GLchar* sourcecode, GLuint *shader,
			int verbose);
#ifdef __cplusplus
}
#endif
