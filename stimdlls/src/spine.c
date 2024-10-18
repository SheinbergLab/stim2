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
    
#include <spine/spine.h>
#include <spine/extension.h>
#include <tcl.h>
#include <stim2.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "shaderutils.h"

#ifndef SPINE_MESH_VERTEX_COUNT_MAX
#define SPINE_MESH_VERTEX_COUNT_MAX 2048
#endif

#define MAX_VERTICES_PER_ATTACHMENT 2048
#define MAX_VERTICES_PER_ATTACHMENT 2048

typedef struct _SPINE_INFO {
  SHADER_PROG *SpineShaderProg;
  GLuint vao;
  GLuint pos_vbo;
  GLuint col_vbo;
  GLuint tex_vbo;
  float worldVerticesPositions[MAX_VERTICES_PER_ATTACHMENT];
  GLfloat Vertices_xy[MAX_VERTICES_PER_ATTACHMENT];
  GLfloat Vertices_uv[MAX_VERTICES_PER_ATTACHMENT];
  GLfloat Vertices_rgba[MAX_VERTICES_PER_ATTACHMENT];
} SPINE_INFO;

typedef struct _SpineTexture {
  GLuint textureID;
} SpineTexture;
  
typedef struct _SpineObject {
  spSkeleton* skeleton;
  spSkeletonClipping *clipper;
  float scale;			/* to rescale to "1 degree" */
  float timeScale;
  spSkeletonBounds* bounds;
  spFloatArray *worldVertices;
  float last_update;	/* track time of last update */
  int do_reset;		/* reset animation on update? */

  int ownsAnimationStateData;
  spAnimationStateData* stateData;
  spAnimationState* state;
  int ownsSkeletonData;
  spSkeletonData *skeletonData;
  int ownsAtlas;
  spAtlas* atlas;

  SPINE_INFO *spineInfo;
  SHADER_PROG *program;
  UNIFORM_INFO *modelviewMat;   /* set if we have "modelviewMat" uniform */
  UNIFORM_INFO *projMat;        /* set if we have "projMat" uniform */
  UNIFORM_INFO *tex0;           /* set if we have "tex0" uniform */
  Tcl_HashTable uniformTable;	/* local unique version */
  Tcl_HashTable attribTable;	/* local unique version */
} SpineObject;


static int SpineID = -1;	/* unique spine object id */
static SPINE_INFO SpineInfo;	/* track global variables */

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

void _spAtlasPage_createTexture (spAtlasPage* self, const char* path)
{
  int width, height, components;
  stbi_uc *imageData = stbi_load(path, &width, &height, &components, 4);
  if (!imageData) return;
	
  GLuint texture;

  //  if (!LoadPNGFile(path, &pixels, &w, &h, &d)) {
  //    fprintf(getConsoleFP(), "error loading atlas source: %s\n", path);
  //    return;
  //  }

  glGenTextures(1, &texture);
  
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, width);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 
	       GL_RGBA, GL_UNSIGNED_BYTE, imageData);

  stbi_image_free(imageData);

  SpineTexture *spine_texture = (SpineTexture *) calloc(1, sizeof(SpineTexture));
  spine_texture->textureID = texture;
  self->rendererObject = spine_texture;
  self->width = width;
  self->height = height;

  //  printf("loaded texture [%d]: %dx%d\n",
  //	 ((SpineTexture *) (self->rendererObject))->textureID, width, height);
}

void _spAtlasPage_disposeTexture (spAtlasPage* self) {
  SpineTexture *spine_texture = (SpineTexture *) self->rendererObject;
  glDeleteTextures(1, &spine_texture->textureID);
  free(spine_texture);
}

char* _spUtil_readFile (const char* path, int* length) {
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
		     int texture, BlendMode blendmode)
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
  static unsigned short quadIndices[] = {0, 1, 2, 2, 3, 0};
    
  
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

    // Early out if the slot color is 0 or the bone is not active
    if (slot->color.a == 0 || !slot->bone->active) {
      spSkeletonClipping_clipEnd(s->clipper, slot);
      continue;
    }
    
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

    spFloatArray *vertices = s->worldVertices;
    int verticesCount = 0;
    float *uvs = NULL;
    unsigned short *indices;
    int indicesCount = 0;
    spColor *attachmentColor = NULL;

    if (attachment->type == SP_ATTACHMENT_REGION) {
      // Cast to an spRegionAttachment so we can get the rendererObject
      // and compute the world vertices
      spRegionAttachment* region = (spRegionAttachment *) attachment;
      attachmentColor = &region->color;

      // Early out if the slot color is 0
      if (attachmentColor->a == 0) {
	spSkeletonClipping_clipEnd(s->clipper, slot);
	continue;
      }
      
      spFloatArray_setSize(vertices, 8);
      spRegionAttachment_computeWorldVertices(region, slot, vertices->items, 0, 2);
      verticesCount = 4;
      uvs = region->uvs;
      indices = quadIndices;
      indicesCount = 6;

      // Might add this?
#if 0      
      if (spSkeletonClipping_isClipping(s->clipper)) {
	spSkeletonClipping_clipTriangles(s->clipper, vertices->items,
					 verticesCount << 1, indices, indicesCount, uvs, 2);
	vertices = s->clipper->clippedVertices;
	verticesCount = s->clipper->clippedVertices->size >> 1;
	uvs = s->clipper->clippedUVs->items;
	indices = s->clipper->clippedTriangles->items;
	indicesCount = s->clipper->clippedTriangles->size;
      }
#endif 
      addVertex(s, vertices->items[0], vertices->items[1],
                region->uvs[0], region->uvs[1],
                tintR, tintG, tintB, tintA, &vertexIndex);
      
      addVertex(s, vertices->items[4], vertices->items[5],
                region->uvs[4], region->uvs[5],
                tintR, tintG, tintB, tintA, &vertexIndex);
      
      addVertex(s, vertices->items[2], vertices->items[3],
                region->uvs[2], region->uvs[3],
                tintR, tintG, tintB, tintA, &vertexIndex);
      
      addVertex(s, vertices->items[4], vertices->items[5],
                region->uvs[4], region->uvs[5],
                tintR, tintG, tintB, tintA, &vertexIndex);

      addVertex(s, vertices->items[0], vertices->items[1],
                region->uvs[0], region->uvs[1],
                tintR, tintG, tintB, tintA, &vertexIndex);      

      addVertex(s, vertices->items[6], vertices->items[7],
                region->uvs[6], region->uvs[7],
                tintR, tintG, tintB, tintA, &vertexIndex);
      
      //      SpineTexture *spine_texture = (SpineTexture *)
      //	((spAtlasRegion*) region->rendererObject)->page->rendererObject;
      SpineTexture *spine_texture = (SpineTexture *)
	((spAtlasRegion * ) region->rendererObject)->page->rendererObject;

      texture = spine_texture->textureID;
      
    } else if (attachment->type == SP_ATTACHMENT_MESH) {
      // Cast to an spMeshAttachment so we can get the rendererObject
      // and compute the world vertices
      spMeshAttachment* mesh = (spMeshAttachment*) attachment;

      attachmentColor = &mesh->color;
      // Early out if the slot color is 0
      if (attachmentColor->a == 0) {
	spSkeletonClipping_clipEnd(s->clipper, slot);
	continue;
      }


      spFloatArray_setSize(vertices, mesh->super.worldVerticesLength);
      spVertexAttachment_computeWorldVertices(SUPER(mesh), slot, 0,
					      mesh->super.worldVerticesLength, vertices->items, 0, 2);
      verticesCount = mesh->super.worldVerticesLength >> 1;
      uvs = mesh->uvs;
      indices = mesh->triangles;
      indicesCount = mesh->trianglesCount;
      
      // Our engine specific Texture is stored in the spAtlasRegion which was
      // assigned to the attachment on load. It represents the texture atlas
      // page that contains the image the mesh attachment is mapped to
      SpineTexture *spine_texture = (SpineTexture *)
	((spAtlasRegion*) mesh->rendererObject)->page->rendererObject;
      texture = spine_texture->textureID;
      
      if (spSkeletonClipping_isClipping(s->clipper)) {
	spSkeletonClipping_clipTriangles(s->clipper, vertices->items,
					 verticesCount << 1, indices, indicesCount, uvs, 2);
	vertices = s->clipper->clippedVertices;
	verticesCount = s->clipper->clippedVertices->size >> 1;
	uvs = s->clipper->clippedUVs->items;
	indices = s->clipper->clippedTriangles->items;
	indicesCount = s->clipper->clippedTriangles->size;
      }

      int index;
      for (int i = 0; i < indicesCount; i+=3) {
	index = mesh->triangles[i] << 1;
	addVertex(s, vertices->items[index],
		  vertices->items[index + 1],
		  uvs[index], uvs[index + 1], 
		  tintR, tintG, tintB, tintA, &vertexIndex);

	index = mesh->triangles[i+2] << 1;
	addVertex(s, vertices->items[index],
		  vertices->items[index + 1],
		  uvs[index], uvs[index + 1], 
		  tintR, tintG, tintB, tintA, &vertexIndex);

	index = mesh->triangles[i+1] << 1;
	addVertex(s, vertices->items[index],
		  vertices->items[index + 1],
		  uvs[index], uvs[index + 1], 
		  tintR, tintG, tintB, tintA, &vertexIndex);
      }

    } else if (attachment->type == SP_ATTACHMENT_CLIPPING) {
      spClippingAttachment *clip = (spClippingAttachment *) slot->attachment;
      spSkeletonClipping_clipStart(s->clipper, slot, clip);
      continue;
    } else
      continue;


    // Draw the mesh we created for the attachment
    if (vertexIndex) {
      engine_drawMesh(s, 0, vertexIndex, texture, engineBlendMode);
    }
    spSkeletonClipping_clipEnd(s->clipper, slot);

  }
  spSkeletonClipping_clipEnd2(s->clipper);
  
}

void spineDelete(GR_OBJ *gobj) 
{
  SpineObject *s = (SpineObject *) GR_CLIENTDATA(gobj);

  if (s->ownsSkeletonData) {
    spSkeletonData_dispose(s->skeletonData);
  }
  spSkeletonBounds_dispose(s->bounds);
  if (s->ownsAtlas)
    spAtlas_dispose(s->atlas);
  spFloatArray_dispose(s->worldVertices);
  if (s->ownsAnimationStateData) 
    spAnimationStateData_dispose(s->state->data);
  spAnimationState_dispose(s->state);
  spSkeleton_dispose(s->skeleton);
  spSkeletonClipping_dispose(s->clipper);

  delete_uniform_table(&s->uniformTable);
  delete_attrib_table(&s->attribTable);
  
  free((void *) s);
}


static void spineUpdate(GR_OBJ *m) 
{
  SpineObject *s = (SpineObject *) GR_CLIENTDATA(m);
  float delta;
  float StimClock = getStimTime()/1000.;

  if (s->do_reset) {
    delta = 0.01;
    s->do_reset = 0;
    /*
    AnimationState_addAnimationByName(s->state, 0, "run", 1, 3.0);
    */
  }
  else {
    delta = StimClock-(s->last_update);
  }
  s->last_update = StimClock;

  spSkeletonBounds_update(s->bounds, s->skeleton, 1);

  spAnimationState_update(s->state, delta * s->timeScale);
  spAnimationState_apply(s->state, s->skeleton);
  spSkeleton_updateWorldTransform(s->skeleton, SP_PHYSICS_UPDATE);
}


static void spineReset(GR_OBJ *m) 
{
  SpineObject *s = (SpineObject *) GR_CLIENTDATA(m);
  s->do_reset = 1;
  //  spSkeleton_setToSetupPose(s->skeleton);
  //  spSkeleton_updateWorldTransform(s->skeleton, SP_PHYSICS_UPDATE);
}

static void spineOn(GR_OBJ *m) 
{
  SpineObject *s = (SpineObject *) GR_CLIENTDATA(m);
  float StimClock = getStimTime()/1000.;
  s->last_update = StimClock;
}

int spineCopy(OBJ_LIST *objlist, SpineObject *source)
{
  const char *name = "SpineCopy";
  GR_OBJ *obj;
  SpineObject *copy;
  Tcl_HashEntry *entryPtr;
  
  copy = (SpineObject *) calloc(1, sizeof(SpineObject));
  memcpy(copy, source, sizeof(SpineObject));

  copy->skeleton = spSkeleton_create(source->skeletonData);
  copy->clipper = spSkeletonClipping_create();
  copy->ownsAnimationStateData = 0;
  copy->ownsAtlas = 0;
  copy->ownsSkeletonData = 0;
  copy->state = spAnimationState_create(copy->stateData);
  copy->worldVertices = spFloatArray_create(12);
  copy->bounds = spSkeletonBounds_create();

  spSkeletonBounds_update(copy->bounds, copy->skeleton, 1);

  // spSkeleton_update(copy->skeleton, 0.0);
  spAnimationState_update(copy->state, 0.0);
  spAnimationState_apply(copy->state, copy->skeleton);

  spSkeleton_updateWorldTransform(copy->skeleton, SP_PHYSICS_UPDATE);
  
  copy_uniform_table(&source->program->uniformTable, &copy->uniformTable);
  copy_attrib_table(&source->program->attribTable, &copy->attribTable);

  if ((entryPtr = Tcl_FindHashEntry(&copy->uniformTable, "modelviewMat"))) {
    copy->modelviewMat = Tcl_GetHashValue(entryPtr);
    copy->modelviewMat->val = malloc(sizeof(float)*16);
  }
  
  if ((entryPtr = Tcl_FindHashEntry(&copy->uniformTable, "projMat"))) {
    copy->projMat = Tcl_GetHashValue(entryPtr);
    copy->projMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&copy->uniformTable, "tex0"))) {
    copy->tex0 = Tcl_GetHashValue(entryPtr);
    copy->tex0->val = malloc(sizeof(int));
    *((int *)(copy->tex0->val)) = 0;
  }

  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = SpineID;

  GR_UPDATEFUNCP(obj) = spineUpdate;
  GR_DELETEFUNCP(obj) = spineDelete;
  GR_RESETFUNCP(obj) = spineReset;
  GR_ACTIONFUNCP(obj) = spineDraw;
  
  GR_CLIENTDATA(obj) = copy;

  return(gobjAddObj(objlist, obj));
}

int spineCreate(OBJ_LIST *objlist, char *skelfile, char *atlasfile)
{
  const char *name = "Spine";
  GR_OBJ *obj;
  SpineObject* spineobj;
  Tcl_HashEntry *entryPtr;
  
  spAtlas* atlas;
  spSkeletonJson* json;
  spSkeletonBounds* bounds;
  spSkeletonData *skeletonData;
  spSkeleton* skeleton;

  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = SpineID;

  GR_UPDATEFUNCP(obj) = spineUpdate;
  GR_DELETEFUNCP(obj) = spineDelete;
  GR_RESETFUNCP(obj) = spineReset;
  GR_ACTIONFUNCP(obj) = spineDraw;

  //  printf("loading atlasfile %s\n", atlasfile);
  
  atlas = spAtlas_createFromFile(atlasfile, 0);

  if (!atlas) {
    fprintf(getConsoleFP(), "error loading atlas file %s\n", atlasfile);
    return -1;
  }
  

  json = spSkeletonJson_create(atlas);

  json->scale = 0.6f;
  
  //  printf("reading skeleton data from %s\n", skelfile);
  skeletonData = spSkeletonJson_readSkeletonDataFile(json, skelfile);
  //  printf("read skeleton data\n");

  if (!skeletonData) {
    fprintf(getConsoleFP(), "%s\n", json->error);
    return -1;
  }

  spSkeletonJson_dispose(json);

  
  // Configure mixing.
  /*
    stateData = spAnimationStateData_create(skeletonData);
    spAnimationStateData_setMixByName(stateData, "idle", "walk", 0.6f);
    spAnimationStateData_setMixByName(stateData, "jump", "run", 0.2f);
  */

  spineobj = (SpineObject *) calloc(1, sizeof(SpineObject));
  /*
  vertexArray(new VertexArray(Triangles, skeletonData->bonesCount * 4))
  */
  
  spineobj->timeScale = 1.0;
  spineobj->worldVertices = spFloatArray_create(12);
  spineobj->skeleton = spSkeleton_create(skeletonData);
  spineobj->clipper = spSkeletonClipping_create();
  
  spineobj->ownsAnimationStateData = 1;
  if (spineobj->ownsAnimationStateData) {
    spineobj->stateData = spAnimationStateData_create(skeletonData);
  }
  spineobj->state = spAnimationState_create(spineobj->stateData);

  spineobj->timeScale = 1;

  bounds = spSkeletonBounds_create();
  spineobj->bounds = bounds;

  spineobj->ownsAtlas = 1;
  spineobj->atlas = atlas;

  spineobj->do_reset = 1;
  spineobj->ownsSkeletonData = 1;
  spineobj->skeletonData = skeletonData;

  skeleton = spineobj->skeleton;
  spSkeleton_setToSetupPose(skeleton);
  
  skeleton->x = 0;
  skeleton->y = 0;

  spSkeletonBounds_update(spineobj->bounds, spineobj->skeleton, 1);

  // spSkeleton_update(spineobj->skeleton, 0.0);
  spAnimationState_update(spineobj->state, 0.0);
  spAnimationState_apply(spineobj->state, spineobj->skeleton);

  spSkeleton_updateWorldTransform(skeleton, SP_PHYSICS_UPDATE);
  spSkeleton_setSkin(skeleton, 0);

  spineobj->scale = 0.01;	/* fix this */
  spineobj->program = SpineInfo.SpineShaderProg;
  spineobj->spineInfo = &SpineInfo;

  copy_uniform_table(&spineobj->program->uniformTable, &spineobj->uniformTable);
  copy_attrib_table(&spineobj->program->attribTable, &spineobj->attribTable);

  if ((entryPtr = Tcl_FindHashEntry(&spineobj->uniformTable, "modelviewMat"))) {
    spineobj->modelviewMat = Tcl_GetHashValue(entryPtr);
    spineobj->modelviewMat->val = malloc(sizeof(float)*16);
  }
  
  if ((entryPtr = Tcl_FindHashEntry(&spineobj->uniformTable, "projMat"))) {
    spineobj->projMat = Tcl_GetHashValue(entryPtr);
    spineobj->projMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&spineobj->uniformTable, "tex0"))) {
    spineobj->tex0 = Tcl_GetHashValue(entryPtr);
    spineobj->tex0->val = malloc(sizeof(int));
    *((int *)(spineobj->tex0->val)) = 0;
  }

  
  GR_CLIENTDATA(obj) = spineobj;
  
  /*
    spineobj->state->listener = callback;
    AnimationState_setAnimationByName(spineobj->state, 0, "walk", 1);
    AnimationState_addAnimationByName(spineobj->state, 0, "jump", 0, 3);
    AnimationState_addAnimationByName(spineobj->state, 0, "run", 1, 0);
  */
  return(gobjAddObj(objlist, obj));
}



static int spCreateCmd(ClientData clientData, Tcl_Interp *interp,
		       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " skeleton_file atlas_file", NULL);
    return TCL_ERROR;
  }

  if ((id = spineCreate(olist, argv[1], argv[2])) < 0) {
    Tcl_SetResult(interp, "error loading spine animation", TCL_STATIC);
    return(TCL_ERROR);
  }

  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
  return(TCL_OK);
}


static int spCopyCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  SpineObject *s;
  int id;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " spine_obj", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a spine object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != SpineID) {
    Tcl_AppendResult(interp, argv[0], ": object not a spine object", NULL);
    return TCL_ERROR;
  }
  s = (SpineObject *) GR_CLIENTDATA(OL_OBJ(olist,id));

  if ((id = spineCopy(olist, s)) < 0) {
    Tcl_SetResult(interp, "error copying spine object", TCL_STATIC);
    return(TCL_ERROR);
  }
  
  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
  return(TCL_OK);
}

static int spGetBoundsCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  SpineObject *s;
  int id;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " spine_obj", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a spine object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != SpineID) {
    Tcl_AppendResult(interp, argv[0], ": object not a spine object", NULL);
    return TCL_ERROR;
  }
  s = (SpineObject *) GR_CLIENTDATA(OL_OBJ(olist,id));

  Tcl_Obj *listPtr = Tcl_NewListObj(0, NULL);
  Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewDoubleObj(s->bounds->minX));
  Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewDoubleObj(s->bounds->minY));
  Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewDoubleObj(s->bounds->maxX));
  Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewDoubleObj(s->bounds->maxY));
  Tcl_SetObjResult(interp, listPtr);
  return(TCL_OK);
}

static int spSetAddAnimationByNameCmd(ClientData clientData, Tcl_Interp *interp,
				      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  SpineObject *s;
  spTrackEntry *result;
  int id;
  int track = 0;
  int loop = 0;
  double delay = 0.0;
  int setAnimation = 0;

  if (!strncmp(argv[0], "sp::set", 7)) setAnimation = 1;

  if (setAnimation) {
    if (argc < 3) {
      Tcl_AppendResult(interp, "usage: ", argv[0],
		       " spine_obj anim track ?loop?", NULL);
      return TCL_ERROR;
    }
    
    if (argc > 3) {
      if (Tcl_GetInt(interp, argv[3], &track) != TCL_OK)
	return TCL_ERROR;
    }
    
    if (argc > 4) {
      if (Tcl_GetInt(interp, argv[4], &loop) != TCL_OK)
	return TCL_ERROR;
    }
  } 
  else {
    if (argc < 3) {
      Tcl_AppendResult(interp, "usage: ", argv[0],
		       " spine_obj anim ?track? ?loop? ?delay?", NULL);
      return TCL_ERROR;
    }
    
    if (argc > 3) {
      if (Tcl_GetInt(interp, argv[3], &track) != TCL_OK)
	return TCL_ERROR;
    }
    
    if (argc > 4) {
      if (Tcl_GetInt(interp, argv[4], &loop) != TCL_OK)
	return TCL_ERROR;
    }
    
    if (argc > 5) {
      if (Tcl_GetDouble(interp, argv[5], &delay) != TCL_OK)
	return TCL_ERROR;
    }
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a spine object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != SpineID) {
    Tcl_AppendResult(interp, argv[0], ": object not a spine object", NULL);
    return TCL_ERROR;
  }
  s = (SpineObject *) GR_CLIENTDATA(OL_OBJ(olist,id));
  
  if (!spSkeletonData_findAnimation(s->skeletonData, argv[2])) {
    Tcl_AppendResult(interp, argv[0], ": animation \"",
		     argv[2], "\" not found", NULL);
    return TCL_ERROR;
  }

  if (setAnimation) {
    result =  spAnimationState_setAnimationByName(s->state, 
						  track, argv[2], loop);
  }
  else {
    result =  spAnimationState_addAnimationByName(s->state, 
						  track, argv[2], loop, delay);
  }
  
  /* Update to in preparation for first draw */
  // spSkeleton_update(s->skeleton, 0);
  spAnimationState_update(s->state, 0);
  spAnimationState_apply(s->state, s->skeleton);
  spSkeleton_updateWorldTransform(s->skeleton, SP_PHYSICS_UPDATE);
  
  return(TCL_OK);
}



int spineShaderCreate(Tcl_Interp *interp, SPINE_INFO *spineInfo)
{
  Tcl_HashEntry *entryPtr;
  spineInfo->SpineShaderProg = (SHADER_PROG *) calloc(1, sizeof(SHADER_PROG));

#ifndef DEBUG
  const char* vertex_shader =
  #ifndef STIM2_USE_GLES
    "# version 330\n"
  #else
    "# version 310 es\n"  
  #endif
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
  #ifndef STIM2_USE_GLES
    "# version 330\n"
  #else
    "# version 310 es\n"  
  #endif
  
    "#ifdef GL_ES\n"
    "precision mediump float;"
    "precision mediump int;\n"
    "#endif\n"

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
  #ifndef STIM2_USE_GLES
    "# version 330\n"
  #else
    "# version 310 es\n"  
  #endif
    "in vec2 vertex_position;"
    "uniform mat4 projMat;"
    "uniform mat4 modelviewMat;"
    "void main () {"
    " gl_Position = projMat * modelviewMat * vec4(vertex_position, 0.0, 1.0);"
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

    "out vec4 frag_color;"
    "void main () {"
    " frag_color = vec4(1.,1.,1.,1.);"
    "}";

#endif
  if (build_prog(spineInfo->SpineShaderProg,
		 vertex_shader, fragment_shader, 0) == -1) {
    free(spineInfo->SpineShaderProg);
    Tcl_AppendResult(interp, "spine : error building spine shader", NULL);
    return TCL_ERROR;
  }
  
  /* Now add uniforms into master table */
  Tcl_InitHashTable(&spineInfo->SpineShaderProg->uniformTable, TCL_STRING_KEYS);
  add_uniforms_to_table(&spineInfo->SpineShaderProg->uniformTable,
			spineInfo->SpineShaderProg);

  /* Now add attribs into master table */
  Tcl_InitHashTable(&spineInfo->SpineShaderProg->attribTable, TCL_STRING_KEYS);
  add_attribs_to_table(&spineInfo->SpineShaderProg->attribTable,
		       spineInfo->SpineShaderProg);

  glGenBuffers(1, &spineInfo->pos_vbo); 
  glGenBuffers(1, &spineInfo->tex_vbo); 
  glGenBuffers(1, &spineInfo->col_vbo); 

  glGenVertexArrays(1, &spineInfo->vao); /* Create a VAO to hold VBOs */
  glBindVertexArray(spineInfo->vao);
  
  if ((entryPtr =
       Tcl_FindHashEntry(&spineInfo->SpineShaderProg->attribTable,
			 "vertex_position"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    
    static GLfloat points[] = { -.5, -.5,
				-.5, .5,
				.5, -.5,
				.5, .5 };
    
    glBindBuffer(GL_ARRAY_BUFFER, spineInfo->pos_vbo);
    glBufferData(GL_ARRAY_BUFFER, 8*sizeof(GLfloat), points, GL_STATIC_DRAW);
    glEnableVertexAttribArray(ainfo->location);
    glVertexAttribPointer(ainfo->location, 2, GL_FLOAT, GL_FALSE, 0, NULL);
  }
  
  if ((entryPtr =
       Tcl_FindHashEntry(&spineInfo->SpineShaderProg->attribTable,
			 "vertex_texcoord"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    glBindBuffer(GL_ARRAY_BUFFER, spineInfo->tex_vbo);
    glVertexAttribPointer(ainfo->location, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
  }
  
  if ((entryPtr =
       Tcl_FindHashEntry(&spineInfo->SpineShaderProg->attribTable,
			 "vertex_color"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    glBindBuffer(GL_ARRAY_BUFFER, spineInfo->col_vbo);
    glVertexAttribPointer(ainfo->location, 4, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
  }
  
  return TCL_OK;
}



#ifdef _WIN32
EXPORT(int,Spine_Init) _ANSI_ARGS_((Tcl_Interp *interp))
#else
int Spine_Init(Tcl_Interp * interp)
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
  
  if (SpineID < 0) {
    SpineID = gobjRegisterType();
  }
  else return TCL_OK;

  gladLoadGL();
  
  spineShaderCreate(interp, &SpineInfo);

  Tcl_Eval(interp, "namespace eval sp {}");

  Tcl_CreateCommand(interp, "sp::create", 
		    (Tcl_CmdProc *) spCreateCmd, 
		    (ClientData) OBJList, 
		    (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "sp::copy", 
		    (Tcl_CmdProc *) spCopyCmd, 
		    (ClientData) OBJList, 
		    (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "sp::setAnimationByName", 
		    (Tcl_CmdProc *) spSetAddAnimationByNameCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "sp::addAnimationByName", 
		    (Tcl_CmdProc *) spSetAddAnimationByNameCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "sp::getBounds", 
		    (Tcl_CmdProc *) spGetBoundsCmd, 
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
