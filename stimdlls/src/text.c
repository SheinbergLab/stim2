/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tcl.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>

#ifdef __QNX__
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cglm/cglm.h>
#else
#include <glad/glad.h>
#include <GLFW/glfw3.h> 
#endif

#include <prmutil.h>
#include <stim2.h>
#include "shaderutils.h"

typedef struct _vao_info {
  GLuint vao;
  int narrays;
  int nindices;
  GLuint points_vbo;
  GLuint texcoords_vbo;
} VAO_INFO;


typedef struct text {
  char *string;
  int angle;			/* rotation angle   */
  int type;

  GLfloat *verts;
  int nverts;
  GLfloat *texcoords;
  int ntexcoords;
  float color[4];

  UNIFORM_INFO *modelviewMat;
  UNIFORM_INFO *projMat;
  UNIFORM_INFO *uColor;
  SHADER_PROG *program;
  VAO_INFO *vao_info;		/* to track vertex attributes */
  Tcl_HashTable uniformTable;	/* local unique version */
  Tcl_HashTable attribTable;	/* local unique version */

} TEXT;

static int TextID = -1;	/* unique polygon object id */
SHADER_PROG *TextShaderProg = NULL;


enum { TEXT_JUSTIFIED_LEFT, TEXT_JUSTIFIED_CENTERED, TEXT_JUSTIFIED_RIGHT };
enum { TEXT_VERTS_VBO, TEXT_TEXCOORDS_VBO };

static void delete_vao_info(VAO_INFO *vinfo)
{
  glDeleteBuffers(1, &vinfo->points_vbo);
  glDeleteBuffers(1, &vinfo->texcoords_vbo);
  glDeleteVertexArrays(1, &vinfo->vao);
}

static void update_vbo(TEXT *t, int type)
{
  float *vals;
  int n, d;
  int vbo;
  switch(type) {
  case TEXT_VERTS_VBO:
    {
      d = 3;			/* 3D */
      vals = t->verts;
      n = t->nverts;
      vbo = t->vao_info->points_vbo;
    }
    break;
  case TEXT_TEXCOORDS_VBO:
    {
      d = 2;			/* 2D */
      vals = t->texcoords;
      n = t->ntexcoords;
      vbo = t->vao_info->texcoords_vbo;
    }
    break;
  }

  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, d*n*sizeof(GLfloat), vals, GL_STATIC_DRAW);
  t->vao_info->nindices = n;
}

struct font_t {
	unsigned int font_texture;
	float pt;
	float advance[128];
	float width[128];
	float height[128];
	float tex_x1[128];
	float tex_x2[128];
	float tex_y1[128];
	float tex_y2[128];
	float offset_x[128];
	float offset_y[128];
	int initialized;
};

typedef struct font_t font_t;

static int initialized = 0;
static font_t* font;

/* Utility function to calculate the dpi based on the display size */
static int calculate_dpi() {
#if 0
  int screen_phys_size[2] = { 0, 0 };

  screen_get_display_property_iv(screen_disp, SCREEN_PROPERTY_PHYSICAL_SIZE, screen_phys_size);

  /* If using a simulator, {0,0} is returned for physical size of the screen,
     so use 170 as the default dpi when this is the case. */
  if ((screen_phys_size[0] == 0) && (screen_phys_size[1] == 0)) {
    return 170;
  } else{
    int screen_resolution[2] = { 0, 0 };
    screen_get_display_property_iv(screen_disp, SCREEN_PROPERTY_SIZE, screen_resolution);

    int diagonal_pixels = sqrt(screen_resolution[0] * screen_resolution[0]
			       + screen_resolution[1] * screen_resolution[1]);
    int diagonal_inches = 0.0393700787 * sqrt(screen_phys_size[0] * screen_phys_size[0]
					      + screen_phys_size[1] * screen_phys_size[1]);
    return (int)(diagonal_pixels / diagonal_inches);
  }
#else
  return 96;
#endif
}

static inline int
nextp2(int x)
{
  int val = 1;
  while(val < x) val <<= 1;
  return val;
}


/* Utility function to load and ready the font for use by OpenGL */
static font_t* load_font(const char* path, int point_size, int dpi) {
  FT_Library library;
  FT_Face face;
  int c;
  int i, j;
  font_t* font;

  if (!path){
    fprintf(stderr, "Invalid path to font file\n");
    return NULL;
  }

  if(FT_Init_FreeType(&library)) {
    fprintf(stderr, "Error loading Freetype library\n");
    return NULL;
  }
  if (FT_New_Face(library, path,0,&face)) {
    fprintf(stderr, "Error loading font %s\n", path);
    return NULL;
  }

  if(FT_Set_Char_Size ( face, point_size * 64, point_size * 64, dpi, dpi)) {
    fprintf(stderr, "Error initializing character parameters\n");
    return NULL;
  }

  font = (font_t*) malloc(sizeof(font_t));
  font->initialized = 0;

  glGenTextures(1, &(font->font_texture));

  /*Let each glyph reside in 32x32 section of the font texture */
  int segment_size_x = 0, segment_size_y = 0;
  int num_segments_x = 16;
  int num_segments_y = 8;

  FT_GlyphSlot slot;
  FT_Bitmap bmp;
  int glyph_width, glyph_height;

  /*First calculate the max width and height of a character in a passed font*/
  for(c = 0; c < 128; c++) {
    if(FT_Load_Char(face, c, FT_LOAD_RENDER)) {
      fprintf(stderr, "FT_Load_Char failed\n");
      free(font);
      return NULL;
    }

    slot = face->glyph;
    bmp = slot->bitmap;

    glyph_width = bmp.width;
    glyph_height = bmp.rows;

    if (glyph_width > segment_size_x) {
      segment_size_x = glyph_width;
    }

    if (glyph_height > segment_size_y) {
      segment_size_y = glyph_height;
    }
  }

  int font_tex_width = nextp2(num_segments_x * segment_size_x);
  int font_tex_height = nextp2(num_segments_y * segment_size_y);

  int bitmap_offset_x = 0, bitmap_offset_y = 0;

  GLubyte* font_texture_data = (GLubyte*) malloc(sizeof(GLubyte) * 2 * font_tex_width * font_tex_height);
  memset((void*)font_texture_data, 0, sizeof(GLubyte) * 2 * font_tex_width * font_tex_height);

  if (!font_texture_data) {
    fprintf(stderr, "Failed to allocate memory for font texture\n");
    free(font);
    return NULL;
  }

  /* Fill font texture bitmap with individual bmp data and record appropriate size,
     texture coordinates and offsets for every glyph */
  for(c = 0; c < 128; c++) {
    if(FT_Load_Char(face, c, FT_LOAD_RENDER)) {
      fprintf(stderr, "FT_Load_Char failed\n");
      free(font);
      return NULL;
    }

    slot = face->glyph;
    bmp = slot->bitmap;

    glyph_width = nextp2(bmp.width);
    glyph_height = nextp2(bmp.rows);

    div_t temp = div(c, num_segments_x);

    bitmap_offset_x = segment_size_x * temp.rem;
    bitmap_offset_y = segment_size_y * temp.quot;

    for (j = 0; j < glyph_height; j++) {
      for (i = 0; i < glyph_width; i++) {
	font_texture_data[2 * ((bitmap_offset_x + i) + (j + bitmap_offset_y) * font_tex_width) + 0] =
	  font_texture_data[2 * ((bitmap_offset_x + i) + (j + bitmap_offset_y) * font_tex_width) + 1] =
	  (i >= bmp.width || j >= bmp.rows)? 0 : bmp.buffer[i + bmp.width * j];
      }
    }

    font->advance[c] = (float)(slot->advance.x >> 6);
    font->tex_x1[c] = (float)bitmap_offset_x / (float) font_tex_width;
    font->tex_x2[c] = (float)(bitmap_offset_x + bmp.width) / (float)font_tex_width;
    font->tex_y1[c] = (float)bitmap_offset_y / (float) font_tex_height;
    font->tex_y2[c] = (float)(bitmap_offset_y + bmp.rows) / (float)font_tex_height;
    font->width[c] = bmp.width;
    font->height[c] = bmp.rows;
    font->offset_x[c] = (float)slot->bitmap_left;
    font->offset_y[c] =  (float)((slot->metrics.horiBearingY-face->glyph->metrics.height) >> 6);
  }

  glBindTexture(GL_TEXTURE_2D, font->font_texture);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG, font_tex_width,
	       font_tex_height,
	       0, GL_RG , GL_UNSIGNED_BYTE, font_texture_data);

  int err = glGetError();

  free(font_texture_data);

  FT_Done_Face(face);
  FT_Done_FreeType(library);

  if (err != 0) {
    fprintf(stderr, "GL Error 0x%x", err);
    free(font);
    return NULL;
  }

  font->initialized = 1;
  return font;
}

static void measure_text(font_t* font, const char* msg, float* width, float* height) {
  int i, c;

  if (!msg) {
    return;
  }

  if (width) {
    /* Width of a text rectangle is a sum advances for every glyph in a string */
    *width = 0.0f;

    for(i = 0; i < strlen(msg); ++i) {
      c = msg[i];
      *width += font->advance[c];
    }
  }

  if (height) {
    /* Height of a text rectangle is a high of a tallest glyph in a string */
    *height = 0.0f;

    for(i = 0; i < strlen(msg); ++i) {
      c = msg[i];

      if (*height < font->height[c]) {
	*height = font->height[c];
      }
    }
  }
}



void textDraw(GR_OBJ *g) 
{
  TEXT *t = (TEXT *) GR_CLIENTDATA(g);
  SHADER_PROG *sp = (SHADER_PROG *) t->program;
  float *v;

  /* Update uniform table */
  if (t->modelviewMat) {
    v = (float *) t->modelviewMat->val;
    stimGetMatrix(STIM_MODELVIEW_MATRIX, v);
  }
  if (t->projMat) {
    v = (float *) t->projMat->val;
    stimGetMatrix(STIM_PROJECTION_MATRIX, v);
  }
  if (t->uColor) {
    v = (float *) t->uColor->val;
    v[0] = t->color[0];
    v[1] = t->color[1];
    v[2] = t->color[2];
    v[3] = t->color[3];
  }
  
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  glUseProgram(sp->program);
  update_uniforms(&t->uniformTable);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, font->font_texture);

  if (t->vao_info->narrays) {
    glBindVertexArray(t->vao_info->vao);
    glDrawArrays(t->type, 0, t->vao_info->nindices);
  }
  glUseProgram(0);

}


void textDelete(GR_OBJ *g) 
{
  TEXT *t = (TEXT *) GR_CLIENTDATA(g);
  if (t->verts) free(t->verts);
  if (t->texcoords) free(t->texcoords);

  delete_uniform_table(&t->uniformTable);
  delete_attrib_table(&t->attribTable);
  delete_vao_info(t->vao_info);

  free((void *) t);
}


int textSet(GR_OBJ *g, font_t* font, const char* msg, float x, float y, int just) {
  TEXT *t = (TEXT *) GR_CLIENTDATA(g);
  float pen_x = 0.0f;
  GLfloat *vptr, *tptr;
  int nchars = strlen(msg);
  int i, c;
  
  if (!nchars) return -1;
      
  if (t->verts) free(t->verts);
  vptr = t->verts = (GLfloat *) malloc(sizeof(float)*18*nchars);
  t->nverts = 6*nchars;
  if (t->texcoords) free(t->texcoords);
  tptr = t->texcoords = (GLfloat *) malloc(sizeof(float)*12*nchars);

  float x0, y0, xoff = 0, yoff = 0;

  float width, height;
  measure_text(font, msg, &width, &height);

  switch (just) {
  case TEXT_JUSTIFIED_CENTERED:
    xoff=-width/2;
    yoff=-height/2;
    break;
  case TEXT_JUSTIFIED_RIGHT:
    xoff=-width;
    yoff=-height/2;
    break;
  case TEXT_JUSTIFIED_LEFT:
    xoff=0.0;
    yoff=-height/2;
    break;
  }

  for(i = 0; i < strlen(msg); ++i) {
    c = msg[i];

    x0 = (x + pen_x + font->offset_x[c]) + xoff;
    y0 = (y + font->offset_y[c]) + yoff;

    //v0
    *vptr++ = x0;
    *vptr++ = y0;
    *vptr++ = 0.0;
    //v1
    *vptr++ = x0 + font->width[c];
    *vptr++ = y0;
    *vptr++ = 0.0;
    //v2
    *vptr++ = x0;
    *vptr++ = y0 + font->height[c];
    *vptr++ = 0.0;
    //v3 (v2)
    *vptr++ = x0;
    *vptr++ = y0 + font->height[c];
    *vptr++ = 0.0;
    //v4 (v1)
    *vptr++ = x0 + font->width[c];
    *vptr++ = y0;
    *vptr++ = 0.0;
    //v5
    *vptr++ = x0 + font->width[c];
    *vptr++ = y0 + font->height[c];
    *vptr++ = 0.0;

    //t0
    *tptr++ = font->tex_x1[c];
    *tptr++ = font->tex_y2[c];
    //t1
    *tptr++ = font->tex_x2[c];
    *tptr++ = font->tex_y2[c];
    //t2
    *tptr++ = font->tex_x1[c];
    *tptr++ = font->tex_y1[c];
    //t3 (t2)
    *tptr++ = font->tex_x1[c];
    *tptr++ = font->tex_y1[c];
    //t4 (t1)
    *tptr++ = font->tex_x2[c];
    *tptr++ = font->tex_y2[c];
    //t5
    *tptr++ = font->tex_x2[c];
    *tptr++ = font->tex_y1[c];
    
    /* Assume we are only working with typewriter fonts */
    pen_x += font->advance[c];
  }

  t->nverts = 6*nchars;
  t->ntexcoords = 6*nchars;
  t->type = GL_TRIANGLES;
  update_vbo(t, TEXT_VERTS_VBO);
  update_vbo(t, TEXT_TEXCOORDS_VBO);

  return 0;
}





static int textcolorCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  TEXT *t;
  double r, g, b, a;
  int id;

  if (argc < 5) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " text r g b ?a?", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a polygon object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != TextID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type text", NULL);
    return TCL_ERROR;
  }
  t = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &r) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &g) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &b) != TCL_OK) return TCL_ERROR;
  if (argc > 5) {
    if (Tcl_GetDouble(interp, argv[5], &a) != TCL_OK) return TCL_ERROR;
  }
  else {
    a = 1.0;
  }

  t->color[0] = r;
  t->color[1] = g;
  t->color[2] = b;
  t->color[3] = a;

  return(TCL_OK);
}


int textCreate(OBJ_LIST *objlist, SHADER_PROG *sp, char *string)
{
  const char *name = "Text";
  GR_OBJ *obj;
  TEXT *t;
  Tcl_HashEntry *entryPtr;

  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = TextID;

  GR_ACTIONFUNCP(obj) = textDraw;
  GR_DELETEFUNCP(obj) = textDelete;

  t = (TEXT *) calloc(1, sizeof(TEXT));
  GR_CLIENTDATA(obj) = t;
  
  /* Default to white */
  t->color[0] = 1.0;
  t->color[1] = 1.0;
  t->color[2] = 1.0;
  t->color[3] = 1.0;

  t->program = sp;
  copy_uniform_table(&sp->uniformTable, &t->uniformTable);
  copy_attrib_table(&sp->attribTable, &t->attribTable);

  t->vao_info = (VAO_INFO *) calloc(1, sizeof(VAO_INFO));
  t->vao_info->narrays = 0;
  glGenVertexArrays(1, &t->vao_info->vao);
  glBindVertexArray(t->vao_info->vao);

  if ((entryPtr = Tcl_FindHashEntry(&t->attribTable, "vertex_position"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    glGenBuffers(1, &t->vao_info->points_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, t->vao_info->points_vbo);
    glVertexAttribPointer(ainfo->location, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
    t->vao_info->narrays++;
  }

  if ((entryPtr = Tcl_FindHashEntry(&t->attribTable, "vertex_texcoord"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    glGenBuffers(1, &t->vao_info->texcoords_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, t->vao_info->texcoords_vbo);
    glVertexAttribPointer(ainfo->location, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
    t->vao_info->narrays++;
  }

  if ((entryPtr = Tcl_FindHashEntry(&t->uniformTable, "modelviewMat"))) {
    t->modelviewMat = Tcl_GetHashValue(entryPtr);
    t->modelviewMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&t->uniformTable, "projMat"))) {
    t->projMat = Tcl_GetHashValue(entryPtr);
    t->projMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&t->uniformTable, "uColor"))) {
    t->uColor = Tcl_GetHashValue(entryPtr);
    t->uColor->val = malloc(sizeof(float)*4);
  }


  /* setup the initial string */
  textSet(obj, font, string, 0.0, 0.0, TEXT_JUSTIFIED_CENTERED);

  return(gobjAddObj(objlist, obj));
}



static int textCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;
  char *string = "text";
  
  if (argc > 1) string = argv[1];
  
  if ((id = textCreate(olist, TextShaderProg, string)) < 0) {
    Tcl_SetResult(interp, "error creating text", TCL_STATIC);
    return(TCL_ERROR);
  }
  
  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
  return(TCL_OK);
}


int textShaderCreate(Tcl_Interp *interp)
{
  TextShaderProg = (SHADER_PROG *) calloc(1, sizeof(SHADER_PROG));

  const char* vertex_shader =
  #ifndef STIM2_USE_GLES
    "# version 330\n"
  #else
    "# version 310 es\n"  
  #endif
    "in vec3 vertex_position;"
    "in vec2 vertex_texcoord;"
    "out vec2 texcoord;"
    "uniform mat4 projMat;"
    "uniform mat4 modelviewMat;"
    
    "void main () {"
    " texcoord = vertex_texcoord;"
    " gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);"
    "}";

  const char* fragment_shader =
  #ifndef STIM2_USE_GLES
    "# version 330\n"
  #else
    "# version 310 es\n"  
  #endif
  
    "#ifdef GL_ES\n"
    "precision mediump float;"
    "precision mediump int;\n"
    "#endif\n"

    "uniform highp vec4 uColor;"
    "uniform sampler2D tex0;"
    "in highp vec2 texcoord;"
    "out highp vec4 frag_color;"
    "void main () {"
    //"  frag_color = uColor;"
    "  highp float alpha = texture(tex0, vec2(texcoord.s, texcoord.t)).g;"
    "  frag_color = vec4(uColor.rgb, alpha);"
    "}";
  
  
  if (build_prog(TextShaderProg, vertex_shader, fragment_shader, 0) == -1) {
    Tcl_AppendResult(interp, "text : error building text shader", NULL);
    return TCL_ERROR;
  }

  /* Now add uniforms into master table */
  Tcl_InitHashTable(&TextShaderProg->uniformTable, TCL_STRING_KEYS);
  add_uniforms_to_table(&TextShaderProg->uniformTable, TextShaderProg);

  /* Now add attribs into master table */
  Tcl_InitHashTable(&TextShaderProg->attribTable, TCL_STRING_KEYS);
  add_attribs_to_table(&TextShaderProg->attribTable, TextShaderProg);

  return TCL_OK;
}

#ifdef _WIN32
EXPORT(int, Text_Init) _ANSI_ARGS_((Tcl_Interp *interp))
#else
int Text_Init(Tcl_Interp *interp)
#endif
{

  int dpi;
  
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
  
  if (TextID < 0) TextID = gobjRegisterType();

  dpi = calculate_dpi();
  
  /*font = load_font(
    "/usr/fonts/font_repository/adobe/MyriadPro-Bold.otf", 15, dpi); */
  #ifndef APPLE_APPLICATION_FOLDER
  font = load_font("/usr/local/stim2/fonts/NotoSans-Regular.ttf", 24, dpi);
  #else
  font = load_font("/Applications/stim2.app/Contents/Resources/fonts/NotoSans-Regular.ttf", 24, dpi);
  
  #endif

  if (!font) {
    return 0;
  }

  gladLoadGL();

  textShaderCreate(interp);

  Tcl_CreateCommand(interp, "text", (Tcl_CmdProc *) textCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "textcolor", (Tcl_CmdProc *) textcolorCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  return TCL_OK;
}
