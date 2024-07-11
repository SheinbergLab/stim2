#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h> 
#include <assert.h>
#include <stdlib.h>
#include <math.h>

#include <tcl.h>
#include <stim2.h>

#include "shaderutils.h"

#define GLM_FORCE_RADIANS	// verify what this does!
#include "MeshBone.h"
#include "CreatureModule.h"

#define MAX_VERTICES_PER_ATTACHMENT 2048

typedef struct _CREATURE_INFO {
  SHADER_PROG *CreatureShaderProg;
  GLuint vao;
  GLuint pos_vbo;
  GLuint col_vbo;
  GLuint tex_vbo;
  float worldVerticesPositions[MAX_VERTICES_PER_ATTACHMENT];
  GLfloat Vertices_xy[MAX_VERTICES_PER_ATTACHMENT];
  GLfloat Vertices_uv[MAX_VERTICES_PER_ATTACHMENT];
  GLfloat Vertices_rgba[MAX_VERTICES_PER_ATTACHMENT];
} CREATURE_INFO;
  
typedef struct _CreatureObject {
  CreatureModule::Creature *creature;
  CreatureModule::CreatureManager *manager;

  float scale;		/* to rescale to "1 degree" */
  float timeScale;
  
  float last_update;	/* track time of last update */
  int do_reset;		/* reset animation on update? */
  
  CREATURE_INFO *creatureInfo;
  SHADER_PROG *program;
  UNIFORM_INFO *modelviewMat;   /* set if we have "modelviewMat" uniform */
  UNIFORM_INFO *projMat;        /* set if we have "projMat" uniform */
  UNIFORM_INFO *tex0;           /* set if we have "tex0" uniform */
  Tcl_HashTable uniformTable;	/* local unique version */
  Tcl_HashTable attribTable;	/* local unique version */
} CreatureObject;


static int CreatureID = -1;	/* unique spine object id */
static CREATURE_INFO CreatureInfo;	/* track global variables */

/*
  read the information from the header and store it in the LodePNG_Info. 
  return value is error
*/
static int PNG_GetInfo(const unsigned char* in, size_t inlength, 
		       int *w, int *h, int *bitDepth, int *colorType)
{
  extern unsigned LodePNG_read32bitInt(const unsigned char* buffer);
  
  if(inlength == 0 || in == 0) return -1;
  if(inlength < 29) return -1;
  
  if(in[0] != 137 || in[1] != 80 || in[2] != 78 || in[3] != 71
     || in[4] != 13 || in[5] != 10 || in[6] != 26 || in[7] != 10)
  {
    return -1;
  }
  if(in[12] != 'I' || in[13] != 'H' || in[14] != 'D' || in[15] != 'R')
  {
    return -1;
  }

  /*read the values given in the header*/
  if (w) *w = LodePNG_read32bitInt(&in[16]);
  if (h) *h = LodePNG_read32bitInt(&in[20]);
  if (bitDepth) *bitDepth = in[24];
  if (colorType) *colorType = in[25];
  return 0;
}

static int LoadPNGFile(const char *filename, unsigned char **pixels, 
		       int *width, int *height, int *depth)
{    
  extern unsigned LodePNG_loadFile(unsigned char** out,
				   size_t* outsize, const char* filename);
  extern unsigned LodePNG_decode(unsigned char** out, 
				 unsigned* w, unsigned* h,
				 const unsigned char* in,
				 size_t insize, unsigned colorType,
				 unsigned bitDepth);
  unsigned char *pixeldata;
  unsigned int w, h;
  int d;
  unsigned char* buffer;
  size_t buffersize;
  unsigned error;
  int bitDepth;
  int colorType;

  error = LodePNG_loadFile(&buffer, &buffersize, filename);
  if (error) return 0;

  error = PNG_GetInfo(buffer, buffersize, NULL, NULL, &bitDepth, &colorType); 
  if (error) return 0;
  
  if (colorType != 0 && colorType != 2 && colorType != 6) 
    return 0; /* only handle these three (RGBA, RGB, GRAYSCALE) */
  
  error = LodePNG_decode(&pixeldata, &w, &h,
			 buffer, buffersize, colorType, bitDepth);
  free(buffer);
  if (error) return 0;

  if (colorType == 6) {
    d = 4;			
  } else if (colorType == 2) {
    d = 3;			
  } else if (colorType == 0) {
    d = 1;			
  }

  if (pixels) *pixels = pixeldata;
  if (width) *width = w;
  if (height) *height = h;
  if (depth) *depth = d;

  return 1;
}

#if 0
void _spAtlasPage_createTexture (spAtlasPage* self, const char* path)
{
  GLuint texture;
  int w, h, d;
  unsigned char *pixels;

  if (!LoadPNGFile(path, &pixels, &w, &h, &d)) {
    fprintf(getConsoleFP(), "error loading atlas source: %s\n", path);
    return;
  }
  
  glGenTextures(1, &texture);
  
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, w);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, 
	       GL_RGBA, GL_UNSIGNED_BYTE, pixels);

  free(pixels);
  
  self->rendererObject = (void *) (size_t) texture;
  self->width = w;
  self->height = h;
}

void _spAtlasPage_disposeTexture (spAtlasPage* self){
  glDeleteTextures(1, (GLuint *) &(self->rendererObject));
}

char* _spUtil_readFile (const char* path, int* length){
  return _spReadFile(path, length);
}

void callback (spAnimationState* state, int trackIndex,
	       spEventType type, spEvent* event, int loopCount) {
  spTrackEntry* entry = spAnimationState_getCurrent(state, trackIndex);
  const char* animationName = 
    (entry && entry->animation) ? entry->animation->name : 0;
  
  switch (type) {
  case SP_ANIMATION_START:
    fprintf(getConsoleFP(), "%d start: %s\n", trackIndex, animationName);
    break;
  case SP_ANIMATION_END:
    fprintf(getConsoleFP(), "%d end: %s\n", trackIndex, animationName);
    break;
  case SP_ANIMATION_COMPLETE:
    fprintf(getConsoleFP(), "%d complete: %s, %d\n", trackIndex,
	    animationName, loopCount);
    break;
  case SP_ANIMATION_INTERRUPT:
    fprintf(getConsoleFP(), "%d interrupt: %s\n", trackIndex, animationName);
    break;
    
  case SP_ANIMATION_DISPOSE:
    fprintf(getConsoleFP(), "%d dispose: %s\n", trackIndex, animationName);
    break;
    
  case SP_ANIMATION_EVENT:
    fprintf(getConsoleFP(), "%d event: %s, %s: %d, %f, %s\n", 
	    trackIndex, animationName, event->data->name, 
	    event->intValue, event->floatValue,
	    event->stringValue);
    break;
  }
}

typedef enum {
  // See https://github.com/EsotericSoftware/spine-runtimes/blob/master/spine-libgdx/spine-libgdx/src/com/esotericsoftware/spine/BlendMode.java#L37
  // for how these translate to OpenGL source/destination blend modes.
  BLEND_NORMAL,
  BLEND_ADDITIVE,
  BLEND_MULTIPLY,
  BLEND_SCREEN,
} BlendMode;

// Draw the given mesh.
// - vertices are pointers to xy, uv, and rgba values for each vertex
// - start defines from which vertex in the vertices array to start
// - count defines how many vertices to use for rendering
// (should be divisible by 3, as we render triangles, each with 3 vertices)
// - texture the texture to use
// - blendMode the blend mode to use
void engine_drawMesh(SpineObject *s, 
		     int start, int count,
		     GLuint texture, BlendMode blendmode)
{
  SPINE_INFO *spineInfo = s->spineInfo;

  GLfloat *xy = s->spineInfo->Vertices_xy;
  GLfloat *uv = s->spineInfo->Vertices_uv;
  GLfloat *rgba = s->spineInfo->Vertices_rgba;
  
  glEnable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);

  switch(blendmode) {
  case BLEND_SCREEN:
  case BLEND_NORMAL:
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;
  case BLEND_ADDITIVE:
    glBlendFunc(GL_DST_ALPHA, GL_ONE);
    break;
  case BLEND_MULTIPLY:
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    break;
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  
  glBindVertexArray(spineInfo->vao);
  glBindBuffer(GL_ARRAY_BUFFER, spineInfo->pos_vbo);
  glBufferData(GL_ARRAY_BUFFER, 2*count*sizeof(GLfloat), xy, GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, spineInfo->tex_vbo);
  glBufferData(GL_ARRAY_BUFFER, 2*count*sizeof(GLfloat), uv, GL_DYNAMIC_DRAW);

#ifdef DEBUG
    {
    int i;
    static GLfloat v[1024];
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 8*sizeof(GLfloat), v);
    for (i = 0; i < 8; i++) { printf("%.2f ", v[i]); }
    printf("\n");
  }
#endif  
  
  glBindBuffer(GL_ARRAY_BUFFER, spineInfo->col_vbo);
  glBufferData(GL_ARRAY_BUFFER, 4*count*sizeof(GLfloat),
	       rgba, GL_DYNAMIC_DRAW);

  glDrawArrays(GL_TRIANGLES, 0, count);

}

// Little helper function to add a vertex to the scratch buffer.
// Index will be increased
// by one after a call to this function.
void addVertex(SpineObject *s,
	       float x, float y,
	       float u, float v,
	       float r, float g, float b, float a,
	       int* index) {
  SPINE_INFO *spineInfo = s->spineInfo;
  int idx = (*index)*2;
  GLfloat* verts = &(spineInfo->Vertices_xy)[idx];
  verts[0] = x*s->scale;
  verts[1] = y*s->scale;
  verts = &(spineInfo->Vertices_uv)[idx];
  verts[0] = u;
  verts[1] = v;
  idx = (*index)*4;
  verts = &(spineInfo->Vertices_rgba)[idx];
  verts[0] = r;
  verts[1] = g;
  verts[2] = b;
  verts[3] = a;
  *index += 1;
}

void spineDraw(GR_OBJ *gobj)
{
  BlendMode engineBlendMode;
  
  SpineObject *s = (SpineObject *) GR_CLIENTDATA(gobj);
  SPINE_INFO *spineInfo = s->spineInfo;
  
  int i;
  spSlot *slot;
  spAttachment *attachment;
  GLuint texture;
  float tintR;
  float tintG;
  float tintB;
  float tintA;
  int vertexIndex;
  float *v;
  float *worldVerticesPositions = spineInfo->worldVerticesPositions;

  /* Update uniform table */
  if (s->modelviewMat) {
    v = (float *) s->modelviewMat->val;
    stimGetMatrix(STIM_MODELVIEW_MATRIX, v);
  }
  if (s->projMat) {
    v = (float *) s->projMat->val;
    stimGetMatrix(STIM_PROJECTION_MATRIX, v);
  }

  glUseProgram(s->program->program);
  update_uniforms(&s->uniformTable);
  
  for (i = 0; i < s->skeleton->slotsCount; ++i) {
    slot = s->skeleton->drawOrder[i];
    attachment = slot->attachment;
    if (!attachment) continue;
    
    // Fetch the blend mode from the slot and
    // translate it to the engine blend mode
    switch (slot->data->blendMode) {
    case SP_BLEND_MODE_NORMAL:
      engineBlendMode = BLEND_NORMAL;
      break;
    case SP_BLEND_MODE_ADDITIVE:
      engineBlendMode = BLEND_ADDITIVE;
      break;
    case SP_BLEND_MODE_MULTIPLY:
      engineBlendMode = BLEND_MULTIPLY;
      break;
    case SP_BLEND_MODE_SCREEN:
      engineBlendMode = BLEND_SCREEN;
      break;
    default:
      // unknown Spine blend mode, fall back to
      // normal blend mode
      engineBlendMode = BLEND_NORMAL;
    }
    
    // Calculate the tinting color based on the skeleton's color
    // and the slot's color. Each color channel is given in the
    // range [0-1], you may have to multiply by 255 and cast to
    // and int if your engine uses integer ranges for color channels.
    tintR = s->skeleton->color.r * slot->color.r;
    tintG = s->skeleton->color.g * slot->color.g;
    tintB = s->skeleton->color.b * slot->color.b;
    tintA = s->skeleton->color.a * slot->color.a;
    
    // Fill the vertices array depending on the type of attachment
    texture = 0;
    vertexIndex = 0;
    if (attachment->type == SP_ATTACHMENT_REGION) {
      // Cast to an spRegionAttachment so we can get the rendererObject
      // and compute the world vertices
      spRegionAttachment* regionAttachment = (spRegionAttachment*)attachment;
      
      // Our engine specific Texture is stored in the spAtlasRegion which was
      // assigned to the attachment on load. It represents the texture atlas
      // page that contains the image the region attachment is mapped to
      texture = (GLuint) (size_t)
	((spAtlasRegion*)regionAttachment->rendererObject)->page->rendererObject;

      // Computed the world vertices positions for the 4 vertices that make up
      // the rectangular region attachment. This assumes the world transform of the
      // bone to which the slot (hence attachment) is attached has been calculated
      // before rendering via spSkeleton_updateWorldTransform
      spRegionAttachment_computeWorldVertices(regionAttachment,
					      slot->bone,
					      worldVerticesPositions, 0, 2);

      // Create 2 triangles, with 3 vertices each from the region's
      // world vertex positions and its UV coordinates (in the range [0-1]).
      addVertex(s,worldVerticesPositions[0], worldVerticesPositions[1],
                regionAttachment->uvs[0], regionAttachment->uvs[1],
                tintR, tintG, tintB, tintA, &vertexIndex);
      
      addVertex(s,worldVerticesPositions[2], worldVerticesPositions[3],
                regionAttachment->uvs[2], regionAttachment->uvs[3],
                tintR, tintG, tintB, tintA, &vertexIndex);
      
      addVertex(s,worldVerticesPositions[4], worldVerticesPositions[5],
                regionAttachment->uvs[4], regionAttachment->uvs[5],
                tintR, tintG, tintB, tintA, &vertexIndex);
      
      addVertex(s,worldVerticesPositions[4], worldVerticesPositions[5],
                regionAttachment->uvs[4], regionAttachment->uvs[5],
                tintR, tintG, tintB, tintA, &vertexIndex);
      
      addVertex(s,worldVerticesPositions[6], worldVerticesPositions[7],
                regionAttachment->uvs[6], regionAttachment->uvs[7],
                tintR, tintG, tintB, tintA, &vertexIndex);
      
      addVertex(s,worldVerticesPositions[0], worldVerticesPositions[1],
                regionAttachment->uvs[0], regionAttachment->uvs[1],
                tintR, tintG, tintB, tintA, &vertexIndex);
    } else if (attachment->type == SP_ATTACHMENT_MESH) {
      // Cast to an spMeshAttachment so we can get the rendererObject
      // and compute the world vertices
      spMeshAttachment* mesh = (spMeshAttachment*)attachment;
      
      // Check the number of vertices in the mesh attachment. If it is bigger
      // than our scratch buffer, we don't render the mesh. We do this here
      // for simplicity, in production you want to reallocate the scratch buffer
      // to fit the mesh.
      if (mesh->super.worldVerticesLength > MAX_VERTICES_PER_ATTACHMENT) continue;
      
      // Our engine specific Texture is stored in the spAtlasRegion which was
      // assigned to the attachment on load. It represents the texture atlas
      // page that contains the image the mesh attachment is mapped to
      texture = (GLuint) (size_t)
	((spAtlasRegion*)mesh->rendererObject)->page->rendererObject;
      
      // Computed the world vertices positions for the vertices that make up
      // the mesh attachment. This assumes the world transform of the
      // bone to which the slot (and hence attachment) is
      // attached has been calculated
      // before rendering via spSkeleton_updateWorldTransform
      spVertexAttachment_computeWorldVertices(SUPER(mesh),
					      slot, 0,
					      mesh->super.worldVerticesLength,
					      worldVerticesPositions, 0, 2);
      
      // Mesh attachments use an array of vertices,
      //  and an array of indices to define which
      // 3 vertices make up each triangle. We loop through all triangle indices
      // and simply emit a vertex for each triangle's vertex.
      for (int i = 0; i < mesh->trianglesCount; ++i) {
	int index = mesh->triangles[i] << 1;
	addVertex(s,worldVerticesPositions[index],
		  worldVerticesPositions[index + 1],
		  mesh->uvs[index], mesh->uvs[index + 1], 
		  tintR, tintG, tintB, tintA, &vertexIndex);
      }
    }
    
    // Draw the mesh we created for the attachment
    engine_drawMesh(s, 0, vertexIndex, texture, engineBlendMode);
  }
}


#endif


void creatureDelete(GR_OBJ *gobj) 
{
  CreatureObject *s = (CreatureObject *) GR_CLIENTDATA(gobj);

  delete s->manager;
  
  delete_uniform_table(&s->uniformTable);
  delete_attrib_table(&s->attribTable);
  
  free((void *) s);
}


static void creatureUpdate(GR_OBJ *m) 
{
  CreatureObject *s = (CreatureObject *) GR_CLIENTDATA(m);
  float delta;
  float StimClock = getStimTime()/1000.;

  if (s->do_reset) {
    delta = 0.01;
    s->do_reset = 0;
    /*
    AnimationState_setAnimationByName(s->state, 0, "jump",  0);
    AnimationState_addAnimationByName(s->state, 0, "run", 1, 3.0);
    */
  }
  else {
    delta = StimClock-(s->last_update);
  }
  s->last_update = StimClock;
  s->manager->Update(delta);  
}


static void creatureReset(GR_OBJ *m) 
{
  CreatureObject *s = (CreatureObject *) GR_CLIENTDATA(m);
  s->do_reset = 1;
}

static void creatureOn(GR_OBJ *m) 
{
  CreatureObject *s = (CreatureObject *) GR_CLIENTDATA(m);
  float StimClock = getStimTime()/1000.;
  s->last_update = StimClock;
}

int creatureCreate(OBJ_LIST *objlist, char *datafile, char *texturefile)
{
  const char *name = "Creature";
  GR_OBJ *obj;
  CreatureObject* creatureobj;
  Tcl_HashEntry *entryPtr;

  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = CreatureID;

  GR_UPDATEFUNCP(obj) = creatureUpdate;
  GR_DELETEFUNCP(obj) = creatureDelete;
  GR_RESETFUNCP(obj) = creatureReset;
  //  GR_ACTIONFUNCP(obj) = spineDraw;


  CreatureModule::CreatureLoadDataPacket json_data;
  try {
    CreatureModule::LoadCreatureJSONData(datafile, json_data);
  }
  catch (...) {
    fprintf(getConsoleFP(), "error loading json file %s\n", datafile);
    return -1;
  }

  auto cur_creature =
    std::shared_ptr<CreatureModule::Creature>(new CreatureModule::Creature(json_data));
  
  CreatureModule::CreatureManager *creature_manager =
    new CreatureModule::CreatureManager(cur_creature);

  creatureobj = (CreatureObject *) calloc(1, sizeof(CreatureObject));

  creatureobj->timeScale = 1.0;
  creatureobj->do_reset = 1;
  creatureobj->scale = 1.0;
  creatureobj->program = CreatureInfo.CreatureShaderProg;
  creatureobj->creatureInfo = &CreatureInfo;

  copy_uniform_table(&creatureobj->program->uniformTable, &creatureobj->uniformTable);
  copy_attrib_table(&creatureobj->program->attribTable, &creatureobj->attribTable);

  if ((entryPtr = Tcl_FindHashEntry(&creatureobj->uniformTable, "modelviewMat"))) {
    creatureobj->modelviewMat = (UNIFORM_INFO *) Tcl_GetHashValue(entryPtr);
    creatureobj->modelviewMat->val = malloc(sizeof(float)*16);
  }
  
  if ((entryPtr = Tcl_FindHashEntry(&creatureobj->uniformTable, "projMat"))) {
    creatureobj->projMat = (UNIFORM_INFO *) Tcl_GetHashValue(entryPtr);
    creatureobj->projMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&creatureobj->uniformTable, "tex0"))) {
    creatureobj->tex0 = (UNIFORM_INFO *) Tcl_GetHashValue(entryPtr);
    creatureobj->tex0->val = malloc(sizeof(int));
    *((int *)(creatureobj->tex0->val)) = 0;
  }

  
  GR_CLIENTDATA(obj) = creatureobj;
  return(gobjAddObj(objlist, obj));
}



static int creatureCreateCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;
  char idstr[64];
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: creature::create json_file atlas_file", NULL);
    return TCL_ERROR;
  }

  if ((id = creatureCreate(olist, argv[1], argv[2])) < 0) {
    Tcl_AppendResult(interp, argv[0], ": error loading creature", NULL);
    return(TCL_ERROR);
  }
  
  sprintf(idstr ,"%d", id);
  Tcl_SetResult(interp, idstr, NULL);
  
  return(TCL_OK);
}


int creatureShaderCreate(Tcl_Interp *interp, CREATURE_INFO *creatureInfo)
{
  Tcl_HashEntry *entryPtr;
  creatureInfo->CreatureShaderProg = (SHADER_PROG *) calloc(1, sizeof(SHADER_PROG));

#ifndef DEBUG
  const char* vertex_shader =
    "# version 330\n"
    "in vec2 vertex_position;"
    "in vec2 vertex_texcoord;"
    "in vec4 vertex_color;"
    "uniform mat4 projMat;"
    "uniform mat4 modelviewMat;"
    "out vec2 texcoord;"
    "out vec4 color;"

    "void main () {"
    " texcoord = vertex_texcoord;"
    " color = vertex_color;"
    " gl_Position = projMat * modelviewMat * vec4(vertex_position, 0.0, 1.0);"
    "}";

  const char* fragment_shader =
    "# version 330\n"
    "uniform sampler2D tex0;"
    "in vec2 texcoord;"
    "in vec4 color;"
    "out vec4 frag_color;"
    "void main () {"
    " vec4 texColor = texture(tex0, vec2(texcoord.s, texcoord.t));"
    " frag_color = texColor*color;"
    "}";
#else
  const char* vertex_shader =
    "# version 330\n"
    "in vec2 vertex_position;"
    "uniform mat4 projMat;"
    "uniform mat4 modelviewMat;"
    "void main () {"
    " gl_Position = projMat * modelviewMat * vec4(vertex_position, 0.0, 1.0);"
    "}";

  const char* fragment_shader =
    "# version 330\n"
    "out vec4 frag_color;"
    "void main () {"
    " frag_color = vec4(1.,1.,1.,1.);"
    "}";

#endif
  if (build_prog(creatureInfo->CreatureShaderProg,
		 vertex_shader, fragment_shader, 0) == -1) {
    free(creatureInfo->CreatureShaderProg);
    Tcl_AppendResult(interp, "creature : error building creature shader", NULL);
    return TCL_ERROR;
  }
  
  /* Now add uniforms into master table */
  Tcl_InitHashTable(&creatureInfo->CreatureShaderProg->uniformTable,
		    TCL_STRING_KEYS);
  add_uniforms_to_table(&creatureInfo->CreatureShaderProg->uniformTable,
			creatureInfo->CreatureShaderProg);

  /* Now add attribs into master table */
  Tcl_InitHashTable(&creatureInfo->CreatureShaderProg->attribTable, TCL_STRING_KEYS);
  add_attribs_to_table(&creatureInfo->CreatureShaderProg->attribTable,
		       creatureInfo->CreatureShaderProg);

  glGenBuffers(1, &creatureInfo->pos_vbo); 
  glGenBuffers(1, &creatureInfo->tex_vbo); 
  glGenBuffers(1, &creatureInfo->col_vbo); 

  glGenVertexArrays(1, &creatureInfo->vao); /* Create a VAO to hold VBOs */
  glBindVertexArray(creatureInfo->vao);
  
  if ((entryPtr =
       Tcl_FindHashEntry(&creatureInfo->CreatureShaderProg->attribTable,
			 "vertex_position"))) {
    ATTRIB_INFO *ainfo = (ATTRIB_INFO *) Tcl_GetHashValue(entryPtr);
    
    static GLfloat points[] = { -.5, -.5,
				-.5, .5,
				.5, -.5,
				.5, .5 };
    
    glBindBuffer(GL_ARRAY_BUFFER, creatureInfo->pos_vbo);
    glBufferData(GL_ARRAY_BUFFER, 8*sizeof(GLfloat), points, GL_STATIC_DRAW);
    glEnableVertexAttribArray(ainfo->location);
    glVertexAttribPointer(ainfo->location, 2, GL_FLOAT, GL_FALSE, 0, NULL);
  }
  
  if ((entryPtr =
       Tcl_FindHashEntry(&creatureInfo->CreatureShaderProg->attribTable,
			 "vertex_texcoord"))) {
    ATTRIB_INFO *ainfo = (ATTRIB_INFO *) Tcl_GetHashValue(entryPtr);
    glBindBuffer(GL_ARRAY_BUFFER, creatureInfo->tex_vbo);
    glVertexAttribPointer(ainfo->location, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
  }
  
  if ((entryPtr =
       Tcl_FindHashEntry(&creatureInfo->CreatureShaderProg->attribTable,
			 "vertex_color"))) {
    ATTRIB_INFO *ainfo = (ATTRIB_INFO *) Tcl_GetHashValue(entryPtr);
    glBindBuffer(GL_ARRAY_BUFFER, creatureInfo->col_vbo);
    glVertexAttribPointer(ainfo->location, 4, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
  }
  
  return TCL_OK;
}


#ifdef _WIN32
EXPORT(int,Creature_Init) _ANSI_ARGS_((Tcl_Interp *interp))
#else
#ifdef __cplusplus
extern "C" {
#endif
  extern int Creature_Init(Tcl_Interp *interp);
#ifdef __cplusplus
}
#endif
int Creature_Init(Tcl_Interp *interp)
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
  
  if (CreatureID < 0) {
    CreatureID = gobjRegisterType();
  }
  else return TCL_OK;

  gladLoadGL();
  
  creatureShaderCreate(interp, &CreatureInfo);

  Tcl_Eval(interp, "namespace eval creature {}");

  Tcl_CreateCommand(interp, "creature::create", 
		    (Tcl_CmdProc *) creatureCreateCmd, 
		    (ClientData) OBJList, 
		    (Tcl_CmdDeleteProc *) NULL);
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
