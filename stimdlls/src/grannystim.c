/*
 * grannystim.c
 *  Use the granny library (v2) to render animations
 */


#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
    
#include <granny.h>

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif


#define GLFW_INCLUDE_GLCOREARB // Needed for OpenGL 3.3 on OS X
#include <GLFW/glfw3.h>

//#include <gl/gl.h>
//#include <gl/glu.h>
#include <prmutil.h>

#include <tcl.h>
#include <df.h>
#include <dfana.h>
#include <tcl_dl.h>

#include <stim.h>

typedef granny_real64x granny_real64;

typedef struct texture_struct
{
  char Name[256];
  GLuint TextureHandle;
  int Allocated;
} texture;

granny_data_type_definition MaterialParametersType[] =
{
  {GrannyReal32Member, "Diffuse Color", 0, 3},
  {GrannyReal32Member, "Specular Color", 0, 3},
  {GrannyReal32Member, "Opacity"},
  {GrannyReal32Member, "Transparency"},
  {GrannyEndMember}
};

typedef struct _material_parameters
{
  granny_real32 DiffuseColor[3];
  granny_real32 SpecularColor[3];
  granny_real32 Opacity;
  granny_real32 Transparency;
} material_parameters;

typedef struct _quad {
  float r;
  float g;
  float b;
  float a;
} quad;

typedef struct mesh_struct
{
  char *Name;
  char Visible;
  char *TriGroupsVisible;
  quad *TriGroupsColors;
  granny_mesh *GrannyMesh;
  granny_mesh_binding *GrannyBinding;
  granny_mesh_deformer *GrannyDeformer;
  
  int TextureCount;
  texture **TextureReferences;
  int *TextureHandled;

  int MorphIndex;

} mesh;

typedef struct model_struct
{
  char *Name;
  granny_model_instance *GrannyInstance;
  granny_world_pose *WorldPose;
  float Matrix[4][4];
  int MeshCount;
  mesh *Meshes;
} model;

/*For point-light displays*/
typedef struct dotfield_struct
{
/*drawingmode: 0 normal, 1 point-light*/
  int drawingmode;
  int framepersistence;
  int frameind;
  int dotsperupdate;
  int **trianglefordot;
  int **groupfordot;
  int **meshfordot;
  int **modelfordot;
  float **winX;
  float **winY;
  GLfloat **bary0;
  GLfloat **bary1;
  GLfloat **bary2;
  float extentX;
  float extentY;
  float dX;
  float dY;
  GLfloat *vertices;
  GLuint *drawlist;
  GLubyte *screenbuffer;
  int drawcount;
  GLfloat color[4];
  GLfloat dotsize;
} dotfield;

typedef struct scene_struct
{
    granny_file *LoadedFile;
    int TextureCount;
    texture *Textures;
    int ModelCount;
    model *Models;
    int MaxBoneCount;
    granny_local_pose *SharedLocalPose;
    int MaxMutableVertexBufferSize;
    granny_pnt332_vertex *MutableVertexBuffer;
} scene;

static int lastGrannyTime = 0;
#define MAX_ANIMATIONS 64

typedef struct _granny_anim {
  int been_played;
  int easeout_happening;
  model *model;
  float clock_override;		/* usually -1, meaning real time */
  granny_file *loaded_file;
  granny_control *control;
  granny_animation *animation;
  granny_real64 begin;
  granny_real64 end;
  granny_real64 easein;
  int easein_from_current;
  granny_real64 easeout;
  int loopcount;
  float speed;
} GRANNY_ANIMATION;

typedef struct _granny_model {
  scene scene;
  int nanimations;
  float clock_override;		/* usually -1, meaning real time */
  GRANNY_ANIMATION animations[MAX_ANIMATIONS];
  granny_real64 ontime;
  granny_real64 start;
  float color[4];
  int color_material;
  granny_system_clock LastSeconds;
  int ResetClock;
  int initialized;
  int free_grannyfile;
  int free_textures;
  Tcl_HashTable meshTable;	/* access pointers to mesh objects */
  Tcl_HashTable boneTable;	/* access pointers to bone objects */

  float bend;			/* for testing */
  int bone_to_bend;

  dotfield *dots;
  dotfield *backdots;
} GRANNY_MODEL;


enum EASE_MODE  { GRANNY_EASE_IN, GRANNY_EASE_OUT };

static Tcl_Interp *OurInterp;
static int GrannyID = -1;	/* unique granny object id */

static void GrannyError(granny_log_message_type Type,
			granny_log_message_origin Origin,
			char const* SourceFile, granny_int32x SourceLine,
			char const *Message,
			void *UserData);
static void CreateTexture(texture *Texture,
                          granny_texture *GrannyTexture, char *name);
static void CreateModel(GRANNY_MODEL *m, model *Model, scene *InScene,
                        granny_model *GrannyModel);
static void CreateMesh(mesh *Mesh, granny_mesh *GrannyMesh,
                       granny_model_instance *InModel, scene *InScene);
static texture *FindTexture(scene *Scene, granny_material *GrannyMaterial, int *handled);
static void RenderingSetup(void);
static void RenderModel(scene *InScene, model *Model);
static void RenderSceneDots(scene *InScene, dotfield *dots,
			    dotfield *backdots, GR_OBJ *gobj);
void RenderMesh(mesh const *Mesh, granny_pnt332_vertex const *Vertices);
void RenderMeshDots(mesh const *Mesh, granny_pnt332_vertex const *Vertices,
		    dotfield *dots, int ModelIndex, int MeshIndex,
		    GLint *viewport);
void BaryToEuc(GLfloat x1, GLfloat y1, GLfloat z1, GLfloat x2,
	       GLfloat y2, GLfloat z2, GLfloat x3, GLfloat y3, GLfloat z3,
	       GLfloat b1, GLfloat b2, GLfloat b3, GLfloat *Euc);
void EucToBary(GLfloat x1, GLfloat y1, GLfloat z1, GLfloat x2,
	       GLfloat y2, GLfloat z2, GLfloat x3, GLfloat y3,
	       GLfloat z3, GLfloat p1, GLfloat p2, GLfloat *b1,
	       GLfloat *b2, GLfloat *b3, int solveInverse);
void dotfieldDelete(dotfield *df);
dotfield *allocDotfield(int framepersistence, int dotsperupdate);
void RenderMeshIndexed(mesh const *Mesh, granny_pnt332_vertex const *Vertices,
		       GLuint TC, float HALF_SCREEN_DEG_X, float SX);
void CreateNewForegroundDots(dotfield *dots);
void ReadDotsForModelMesh(int modelInd, int meshInd, mesh const *Mesh,
			  granny_pnt332_vertex const *Vertices,
			  dotfield *dots, float HALF_SCREEN_DEG_X,
			  float HALF_SCREEN_DEG_Y, GLint *viewport, float SX,
			  float SY, float TX, float TY);
void ReadDotsForGround(dotfield *dots, float HALF_SCREEN_DEG_X,
		       float HALF_SCREEN_DEG_Y, GLint *viewport, float TX,
		       float TY, float SX, float SY);
void GetModelMeshGroupTri(dotfield *dots, int frameind, int dotind,
			  GLint *viewport, int *MMGT);

void grannyUpdateAnimations(GRANNY_MODEL *g, float clock)
{
  int i;
  for (i = 0; i < g->nanimations; i++) {
    if (g->animations[i].control) {
      if (g->animations[i].clock_override >= 0.) {
	clock = g->animations[i].clock_override;
      }
      GrannySetControlClock(g->animations[i].control,
			    clock - g->animations[i].begin);

	/* Verify that easeout start time is correct DLS/JS 11/09 */ 

      if (clock > g->animations[i].begin && clock <= g->animations[i].end) {
	g->animations[i].easeout_happening = 0;
        GrannySetControlWeight(g->animations[i].control, 1.0);
      } else if (clock > g->animations[i].end &&
		 clock <= g->animations[i].end + g->animations[i].easeout) {
	if (!g->animations[i].easeout_happening) {
	  g->animations[i].easeout_happening = 1;
	  GrannyEaseControlOut(g->animations[i].control,
			       g->animations[i].easeout);
	  /*
	    GrannySetControlEaseOutCurve(g->animations[i].control,
	    g->animations[i].end - clock, g->animations[i].easeout,
	    1.0f, 1.0f, 0.0f, 0.0f);
	  */
	}
      } else {
	g->animations[i].easeout_happening = 0;
	GrannySetControlWeight(g->animations[i].control, 0.0);
      }
    }
  }
}

void grannyUpdate(GR_OBJ *gobj) 
{
  int ModelIndex, BoneCount;
  GRANNY_MODEL *g = (GRANNY_MODEL *) GR_CLIENTDATA(gobj);
  granny_system_clock Seconds;
  float SecondsElapsed;
  float StimClock;
  
  GrannyGetSystemSeconds(&Seconds);

  if (g->ResetClock) {
    StimClock = 0;
    SecondsElapsed = 0;
    g->ResetClock = 0;
  }
  else if (g->clock_override >= 0.0) {
    StimClock = g->clock_override;
    SecondsElapsed = 0;
  }
  else {
    StimClock = getStimTime()/1000.;
    SecondsElapsed = GrannyGetSecondsElapsed(&(g->LastSeconds), &Seconds);
  }

  /* StimClock is my absolute time.  I add the seconds elapsed for
     each frame into it as an accumulator.  (Note that if you're
     worried about floating point precision, Granny allows you to
     call GrannyRecenterControlClock on any controls you have to
     reset the game clock when it gets to high). */

  g->LastSeconds = Seconds;
  grannyUpdateAnimations(g, StimClock);

  {for(ModelIndex = 0;
       ModelIndex < g->scene.ModelCount;
       ++ModelIndex)
      {
	model *Model = &g->scene.Models[ModelIndex];
	
	/* All of the state-updating calls take a bone range to
	   update, so I grab the bone count of the model into a local
	   for convenience. */
	BoneCount =
	  GrannyGetSourceSkeleton(Model->GrannyInstance)->BoneCount;
	
	/* Each animation in Granny keeps its own separate clock.
	   This is different from Granny 1.x, which had a single
	   global clock.  It's much more convenient this way, since it
	   gives you much more fine-grained control over what gets
	   updated and when (and you can also sync stuff over the
	   internet easier).  GrannySetModelClock() loops through all
	   the animations on a particular model and updates them to
	   the time given.  You can alternatively call from the
	   control side with GrannySetControlClock if you want to
	   advance time for specific animations instead of specific
	   models. */
	
	//      GrannySetModelClock(Model->GrannyInstance, StimClock);
	
        /* Granny (optionally) gives me the updated version of
           my model's placement matrix.  Allowing Granny to update this
           matrix means that movement will be animator-controlled, so
           that feet will stay planted and so on.  Note that I don't
           have to accept the matrix that Granny gives me - I could
           instead decide that it was colliding, and only allow a smaller
           movement, or even re-call GrannySetModelClock() for an earlier
           to time to set to a non-colliding position. */

      if (SecondsElapsed) {
	GrannyUpdateModelMatrix(Model->GrannyInstance, SecondsElapsed,
				(float *)Model->Matrix,
				(float *)Model->Matrix, 0);
      }
      
      /* Now that the clocks are set, I ask Granny for the current
	 state of the model based on the animations playing on it.
	 I do this into the shared local pose buffer I allocated
	 in OSStartUp, since I only need the results during this
	 update, and don't need to save them per model. */
      
      GrannySampleModelAnimations(Model->GrannyInstance, 0, BoneCount,
				  g->scene.SharedLocalPose);
      
      /* At this point, the SharedLocalPose buffer has the parent-
	 relative transforms of all the bones in this model (hence
	 the "local").  If I wanted to do some tweaking here,
	 I could directly access and manipulate the transforms
	 with GrannyGetLocalPoseTransform.  But, since I don't
	 want to do anything special, I just pass the results
	 directly to the world-space matrix builder. */

      GrannyBuildWorldPose(GrannyGetSourceSkeleton(Model->GrannyInstance),
			   0, BoneCount, g->scene.SharedLocalPose, 
			   (float *) Model->Matrix,
			   Model->WorldPose);

      /* Now Model->WorldPose contains the world-space pose of the
	 model, meaning that all of the local transforms have been
	 composited into per-bone world-space matrices.  I'm
	 saving this per-model, because I imagine most games will
	 want to (since you will want to access it for scripting,
	 collision detection, etc.), but in an extremely memory-
	 constrained scenario, I could have written it so the
	 world-space pose is also buffered, and I just render
	 right here to save space. */
      
      /* At the end of each update, I tell Granny it's OK to free
	 any controls that have completed on this model.  That is,
	 any control on which I said GrannyCompleteControlAt() to
	 kill it at a specified time.  I haven't done that in this
	 sample app, but it's best to always keep it in the loop
	 in case you decide to use GrannyCompleteControlAt() later
	 (which you almost certainly will in practice). */

      GrannyFreeCompletedModelControls(Model->GrannyInstance);
      
      /* Note also that the reason GrannyFreeCompletedModelControls
	 is a separate call, and not automatically handled by
	 GrannySetModelClock and so forth, is because some
	 applications might set the clock mulitple times per frame,
	 sometimes going backwards, in order to do prediction or
	 collision detection and so on.  Obviously, in that case you
	 wouldn't want GrannySetModelClock to delete some of your
	 completed controls, since you may be about to set time back
	 a few tenths of a second where the controls would've still
	 existed, and hence get wrong results. */
    }
  }
}

void grannyDraw(GR_OBJ *gobj) 
{
  GRANNY_MODEL *g = (GRANNY_MODEL *) GR_CLIENTDATA(gobj);
  int ModelIndex;
  
  if (!g->initialized) {
    g->ResetClock = 1;
    grannyUpdate(g);
    g->initialized = 1;
  }
  
  glPushAttrib(GL_ENABLE_BIT);
  RenderingSetup();

  glColor4fv(g->color);

  if (g->color_material) {
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT, GL_DIFFUSE);
    glShadeModel(GL_SMOOTH);
  }
  
  if (g->dots && g->backdots && (g->dots->drawingmode==1)) {
    RenderSceneDots(&g->scene, g->dots, g->backdots, gobj);
  } else {
    for (ModelIndex = 0; ModelIndex < g->scene.ModelCount; ++ModelIndex) {
      RenderModel(&g->scene, &g->scene.Models[ModelIndex]);
    }
  }


  glPopAttrib();
  glDisable(GL_LIGHTING);
  
  /*
    if (g->ontime) {
    GrannyGetCurrentTime(Granny, &now);
    if (g->ontime+g->start > now) return;
    }
  */
  
}

void dotfieldDelete(dotfield *df)
{
  int i;
  if (df->trianglefordot) {
    for (i = 0; i < df->framepersistence; i++) {free(df->trianglefordot[i]);}
    free(df->trianglefordot);
  }
  if (df->modelfordot) {
    for (i = 0; i < df->framepersistence; i++) {free(df->modelfordot[i]);}
    free(df->modelfordot);
  }
  if (df->meshfordot) {
    for (i = 0; i < df->framepersistence; i++) {free(df->meshfordot[i]);}
    free(df->meshfordot);
  }
  if (df->groupfordot) {
    for (i = 0; i < df->framepersistence; i++) {free(df->groupfordot[i]);}
    free(df->groupfordot);
  }
  if (df->winX) {
    for (i = 0; i < df->framepersistence; i++) {free(df->winX[i]);}
    free(df->winX);
  }
  if (df->winY) {
    for (i = 0; i < df->framepersistence; i++) {free(df->winY[i]);}
    free(df->winY);
  }
  if (df->bary0) {
    for (i = 0; i < df->framepersistence; i++) {free(df->bary0[i]);}
    free(df->bary0);
  }
  if (df->bary1) {
    for (i = 0; i < df->framepersistence; i++) {free(df->bary1[i]);}
    free(df->bary1);
  }
  if (df->bary2) {
    for (i = 0; i < df->framepersistence; i++) {free(df->bary2[i]);}
    free(df->bary2);
  }
  if (df->vertices) {
    free(df->vertices);
  }
  if (df->drawlist) {
    free(df->drawlist);
  }
  if (df->screenbuffer) {
    free(df->screenbuffer);
  }
  free(df);
}

void grannyDelete(GR_OBJ *gobj)
{
  int i;
  int TextureIndex, ModelIndex, MeshIndex;
  GRANNY_MODEL *g = (GRANNY_MODEL *) GR_CLIENTDATA(gobj);
  
  if (g->dots) {
    dotfieldDelete(g->dots);
  }
  if (g->backdots) {
    dotfieldDelete(g->backdots);
  }
  if (g->free_textures) {
    for(TextureIndex = 0;
	TextureIndex < g->scene.TextureCount;
	++TextureIndex) {
      texture *Texture = &g->scene.Textures[TextureIndex];
      if (Texture->Allocated)
	glDeleteTextures(1, &Texture->TextureHandle);
    }
    if (g->scene.Textures) free(g->scene.Textures);
  }
  
  for(ModelIndex = 0;
      ModelIndex < g->scene.ModelCount;
      ++ModelIndex) {
    model *Model = &g->scene.Models[ModelIndex];
    if (Model->Name) free(Model->Name);
    for(MeshIndex = 0;
	MeshIndex < Model->MeshCount;
	++MeshIndex) {
      mesh *Mesh = &Model->Meshes[MeshIndex];
      
      if (Mesh->GrannyBinding) GrannyFreeMeshBinding(Mesh->GrannyBinding);
      if (Mesh->GrannyDeformer) GrannyFreeMeshDeformer(Mesh->GrannyDeformer);
      
      if (Mesh->TextureHandled) free(Mesh->TextureHandled);
      if (Mesh->TextureReferences) free(Mesh->TextureReferences);
      if (Mesh->TriGroupsVisible) free(Mesh->TriGroupsVisible);
      if (Mesh->TriGroupsColors) free(Mesh->TriGroupsColors);
    }

    GrannyFreeModelInstance(Model->GrannyInstance);
    GrannyFreeWorldPose(Model->WorldPose);
    free(Model->Meshes);
  }
  free(g->scene.Models);
  
  GrannyFreeLocalPose(g->scene.SharedLocalPose);

  if (g->scene.MutableVertexBuffer) 
    free(g->scene.MutableVertexBuffer);
  
  if (g->free_grannyfile) GrannyFreeFile(g->scene.LoadedFile);

  /* To reset the bone binding cache -- had been causing problems... */
  GrannyFlushAllUnusedAnimationBindings();

  /* free up associated animations */
  for (i = 0; i < g->nanimations; i++) {
    GrannyFreeFile(g->animations[i].loaded_file);    
  }

  /* Free up hash table of mesh pointers */
  Tcl_DeleteHashTable(&g->meshTable);
  Tcl_DeleteHashTable(&g->boneTable);

  free((void *) g);
}

void grannyReset(GR_OBJ *gobj) 
{
  GRANNY_MODEL *g = (GRANNY_MODEL *) GR_CLIENTDATA(gobj);
  int i;

  g->ResetClock = 1;
  grannyUpdate(gobj);
  g->initialized = 1;
  for (i = 0; i < g->nanimations; i++) 
    g->animations[i].been_played = 0;
}

int grannyCopy(OBJ_LIST *objlist, GRANNY_MODEL *src)
{
  static const char *name = "GrannyCopy";
  GR_OBJ *obj;
  GRANNY_MODEL *g;
  int ModelIndex, VertexCount;
  int use_initial_placement = 1;
  granny_real64 loopcount = 0;  
  granny_file *file = src->scene.LoadedFile;
  
  
  g = (GRANNY_MODEL *) calloc(1, sizeof(GRANNY_MODEL));
  Tcl_InitHashTable(&g->meshTable, TCL_STRING_KEYS);
  Tcl_InitHashTable(&g->boneTable, TCL_STRING_KEYS);
  
  g->scene.ModelCount = 0;
  g->scene.MaxBoneCount = 0;
  g->scene.TextureCount = 0;
  g->scene.MaxMutableVertexBufferSize = 0;
  
  g->scene.LoadedFile = file;
  
  if (g->scene.LoadedFile)
    {
      granny_file_info *FileInfo =
	GrannyGetFileInfo(g->scene.LoadedFile);
      g->scene.TextureCount = FileInfo->TextureCount;
      if (g->scene.TextureCount) {
	g->scene.Textures = src->scene.Textures;
      }            

      g->scene.ModelCount = FileInfo->ModelCount;
      if (g->scene.ModelCount) {
	g->scene.Models = 
	  (model *) calloc (g->scene.ModelCount, sizeof(model));
      }
      for(ModelIndex = 0;
	  ModelIndex < g->scene.ModelCount;
	  ++ModelIndex)
	{
	  // Create the model         .
	  granny_model *GrannyModel = FileInfo->Models[ModelIndex];
	  model *Model = &g->scene.Models[ModelIndex];
	  CreateModel(g, Model, &g->scene, GrannyModel);
	  
	  GrannyGetModelInitialPlacement4x4(GrannyModel, 
					    (float *) Model->Matrix);
	  if (!use_initial_placement) {
	    Model->Matrix[3][0] = 0.;
	    Model->Matrix[3][1] = 0;
	    Model->Matrix[3][2] = 0.;
	  }
	}
      g->scene.SharedLocalPose =
	GrannyNewLocalPose(g->scene.MaxBoneCount);
      VertexCount = g->scene.MaxMutableVertexBufferSize;
      if (!VertexCount) VertexCount = 1;
      
      g->scene.MutableVertexBuffer = (granny_pnt332_vertex *) 
	calloc(VertexCount, sizeof(granny_pnt332_vertex));
    }
  else
    {
      free(g);
      return -2;
    }
  
  obj = gobjCreateObj();
  if (!obj) return -1;
  
  GR_OBJTYPE(obj) = GrannyID;
  strcpy(GR_NAME(obj), name);

  GR_ACTIONFUNC(obj) = grannyDraw;
  GR_DELETEFUNC(obj) = grannyDelete;
  GR_RESETFUNC(obj) = grannyReset;
  GR_UPDATEFUNC(obj) = grannyUpdate;
  
  GR_CLIENTDATA(obj) = g;
  
  g->nanimations = 0;
  g->ontime = 0;
  g->color_material = 0;
  g->color[0] = g->color[1] = g->color[2] = g->color[3] = 1.0;

  g->clock_override = -1;		/* use real clock */
  g->ResetClock = 1;
  grannyUpdate(obj);
  g->initialized = 1;
  g->free_grannyfile = 0;
  g->free_textures = 0;
  
  return(gobjAddObj(objlist, obj));
}

int grannyCreate(OBJ_LIST *objlist, char *filename,
		 int use_initial_placement, char *texture_filename)
{
  static const char *name = "Granny";
  GR_OBJ *obj;
  GRANNY_MODEL *g;
  granny_file *TextureFile = NULL, *ModelFile = NULL;
  granny_file_info *TextureFileInfo;
  char *tname;
  int TextureIndex, ModelIndex, VertexCount;
  granny_real32 Affine3[3];	/* for recomputing basis */
  granny_real32 Linear3x3[9];
  granny_real32 InverseLinear3x3[9];
  
  /* This is our desired coordinate system */
  granny_real32 Origin[] = {0, 0, 0};
  granny_real32 RightVector[] = {1, 0, 0};
  granny_real32 UpVector[] = {0, 1, 0};
  granny_real32 BackVector[] = {0, 0, 1};
  granny_real32 UnitsPerMeter = 1.0;
  
  if (texture_filename) {
    TextureFile = GrannyReadEntireFile(texture_filename);
    if (TextureFile) {
      TextureFileInfo = GrannyGetFileInfo(TextureFile);
    } 
    else {
      return -3;
    }
  }

  ModelFile = GrannyReadEntireFile(filename);
  if (!ModelFile) return -2;

  g = (GRANNY_MODEL *) calloc(1, sizeof(GRANNY_MODEL));
  Tcl_InitHashTable(&g->meshTable, TCL_STRING_KEYS);
  Tcl_InitHashTable(&g->boneTable, TCL_STRING_KEYS);
  
  g->dots = 0;
  g->backdots = 0;
  
  g->scene.ModelCount = 0;
  g->scene.MaxBoneCount = 0;
  g->scene.TextureCount = 0;
  g->scene.MaxMutableVertexBufferSize = 0;

    /* Now I read in the file.  I call GrannyReadEntireFile, and that
       basically does all the work.  You can get fancier and override
       Granny's file callbacks, or even block load the file yourself
       and hand it to Granny, but those are overkill for this sample. */
  g->scene.LoadedFile = ModelFile;
  if (g->scene.LoadedFile) {
    /* Once I've got the file, I ask Granny for a pointer into the
       data.  If the file is from a newer or older version of
       Granny, this call will allocate a memory buffer and do a
       conversion of any parts of the file that have changed
       formats so that it will still be directly usable as an
       in-memory datastructure.*/
    granny_file_info *FileInfo = 
      GrannyGetFileInfo(g->scene.LoadedFile);
    granny_art_tool_info *ArtToolInfo =
      FileInfo ->ArtToolInfo;
    
    if(FileInfo)
      {
	UnitsPerMeter = ArtToolInfo->UnitsPerMeter;
	
	/* Special case: this is the default conversion factor out of Max */
	if (UnitsPerMeter > 39.3 && UnitsPerMeter < 39.4) 
	  UnitsPerMeter = 1.0; /* System Units were set to inches */
	else if (UnitsPerMeter = 100.)
	  UnitsPerMeter = 1.0; /* System Units were set to centimeters */
	
	
	/* Tell Granny to construct the transform from the file's */
	/* coordinate system to our coordinate system */
	GrannyComputeBasisConversion(FileInfo, UnitsPerMeter, 
				     Origin, RightVector, UpVector, 
				     BackVector,
				     Affine3, Linear3x3, InverseLinear3x3);
	
	/* Tell Granny to transform the file into our coordinate system */
	GrannyTransformFile(FileInfo, Affine3, Linear3x3, 
			    InverseLinear3x3,  1e-5f, 1e-5f,
			    GrannyRenormalizeNormals | 
			    GrannyReorderTriangleIndices);

	/* 
	 * Now load the textures -- note that this must occur before the
	 * CreateModel() model call (which in turn calls CreateMesh()) because
	 * the meshes expect the texture id's to be set when they are called
	 */

	if (texture_filename == NULL) {
	  TextureFileInfo = FileInfo;
	}
	
	g->scene.TextureCount = TextureFileInfo->TextureCount;
	if (g->scene.TextureCount) {
	  g->scene.Textures = 
	    (texture *) calloc(g->scene.TextureCount, sizeof(texture));
	}
	
	/*
	 * Current swap names so the materials from the old object with use
	 * textures from the new one 
	 */
	for(TextureIndex = 0;
	    TextureIndex < g->scene.TextureCount;
	    ++TextureIndex) {
	  if (!texture_filename) {
	    tname =
	      (char *) TextureFileInfo->Textures[TextureIndex]->FromFileName;
	  }
	  else {
	    if (TextureIndex < FileInfo->TextureCount) {
	      tname = (char *) FileInfo->Textures[TextureIndex]->FromFileName;
	    }
	    else {
	      tname = (char *)
		TextureFileInfo->Textures[TextureIndex]->FromFileName;
	    }
	  }
	  CreateTexture(&g->scene.Textures[TextureIndex],
			TextureFileInfo->Textures[TextureIndex],
			tname);
	}            
		
	/* Second, I need to instantiate all the models that are
	   in the file.  Unlike a lot of systems, Granny doesn't
	   mandate that you have only one model in a file - your
	   artists could opt to model hundreds of different models
	   in a single art file and they'd all come out as distinct
	   models in the Granny file. */
	g->scene.ModelCount = FileInfo->ModelCount;
	
	if (g->scene.ModelCount) {
	  g->scene.Models = 
	    (model *) calloc (g->scene.ModelCount, sizeof(model));
	}
	
	for(ModelIndex = 0;
	    ModelIndex < g->scene.ModelCount;
	    ++ModelIndex)
	  {
	    // Create the model         .
	    granny_model *GrannyModel = FileInfo->Models[ModelIndex];
	    model *Model = &g->scene.Models[ModelIndex];
	    CreateModel(g, Model, &g->scene, GrannyModel);
	    
	    /* Often times models are not made with their root bone
	       at the origin.  Granny always pre-transforms the
	       models on export to make sure that they do, so that
	       they will always be consistent.  However, sometimes
	       where the model actually was is very important, so you
	       can get that information as well, and use it to
	       seed your initial placement of the model in the
	       world (or any other purpose you may deem appropriate). */
	    
	    GrannyGetModelInitialPlacement4x4(GrannyModel, 
					      (float *) Model->Matrix);
	    
	    if (!use_initial_placement) {
	      Model->Matrix[3][0] = 0.;
	      Model->Matrix[3][1] = 0;
	      Model->Matrix[3][2] = 0.;
	    }
	  }
	
	/* When Granny updates models, she needs an intermediate
	   buffer that holds the local transform for each bone in
	   a model.  But, I really don't want to keep around a
	   separate buffer for each model because I don't care
	   about the results outside of the update loop.  So I
	   allocate a single local pose buffer, and use it for all
	   the models.  The CreateModel() call automatically kept
	   track of the maximum number of bones in the models, so
	   I just allocate one that big and know that it will work
	   for all the models in the scene. */
	
	g->scene.SharedLocalPose =
	  GrannyNewLocalPose(g->scene.MaxBoneCount);
	
	/* On a similar note, when Granny does weighted vertex
	   blending, she needs somewhere to put the results for
	   OpenGL to send to the video card.  So, the CreateMesh
	   routine (like the CreateModel routine) kept track of
	   the maximum number of deformable vertices in an mesh.
	   I allocate buffers of that size to use as output buffers. */
	VertexCount = g->scene.MaxMutableVertexBufferSize;
	if (!VertexCount) VertexCount = 1;
	
	g->scene.MutableVertexBuffer = (granny_pnt332_vertex *) 
	  calloc(VertexCount, sizeof(granny_pnt332_vertex));
      }        
    else
      {
	free(g);
	return -2;
      }
    
    /* Now that I've loaded and processed everything, I don't need
       to keep around parts of the file that I've copied elsewhere -
       namely the rigid vertices, rigid indices, and the textures.
       So I tell Granny to get rid of them, but not the rest of the
       file, since that has all the other data that I still need
       (the model and animation data, and the deformable vertices
       and indices). */
  }
  else {
    return -2;
  }
    
  obj = gobjCreateObj();
  if (!obj) return -1;
  
  GR_OBJTYPE(obj) = GrannyID;
  strcpy(GR_NAME(obj), name);
  
  GR_ACTIONFUNC(obj) = grannyDraw;
  GR_DELETEFUNC(obj) = grannyDelete;
  GR_RESETFUNC(obj) = grannyReset;
  GR_UPDATEFUNC(obj) = grannyUpdate;
  
  GR_CLIENTDATA(obj) = g;
  
  g->nanimations = 0;
  g->ontime = 0;
  g->color[0] = g->color[1] = g->color[2] = g->color[3] = 1.0;
  
  g->clock_override = -1;		/* use real clock */
  g->ResetClock = 1;
  grannyUpdate(obj);
  g->initialized = 1;
  g->free_grannyfile = 1;
  g->free_textures = 1;

  if (texture_filename) {
    GrannyFreeFile(TextureFile);
  }
  
  return(gobjAddObj(objlist, obj));
}

static int grannyGetVertices(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id;
  int ModelIndex, MeshIndex;
  int VertexCount, VertexBufferSize;
  model *Model;
  mesh *Mesh;
  float *VertexBufferPointer;
  DYN_LIST *dl, *curdl;


  if (argc < 2) {
    interp->result = 
      "usage: granny_getVertices granny_object";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));
  
  dl = dfuCreateDynList(DF_LIST,  g->scene.ModelCount);
  for (ModelIndex = 0; ModelIndex < g->scene.ModelCount; ++ModelIndex) {
    Model = &g->scene.Models[ModelIndex];
  
    for(MeshIndex = 0;
	MeshIndex < Model->MeshCount;
	++MeshIndex) {
      Mesh = &Model->Meshes[MeshIndex];
      VertexCount = GrannyGetMeshVertexCount(Mesh->GrannyMesh);
      VertexBufferSize = VertexCount * sizeof(granny_p3_vertex);
      VertexBufferPointer = (float *) malloc(VertexBufferSize);
      GrannyCopyMeshVertices(Mesh->GrannyMesh,
			     GrannyP3VertexType, 
			     VertexBufferPointer);
      curdl = dfuCreateDynListWithVals(DF_FLOAT, VertexCount*3, 
				       VertexBufferPointer);
      dfuMoveDynListList(dl, curdl);
    }
  }
  
  return tclPutList(interp, dl);
}


static int grannyGetIndices(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id;
  int ModelIndex, MeshIndex;
  model *Model;
  mesh *Mesh;
  int *IndexBufferPointer;
  DYN_LIST *dl, *curdl;
  
  if (argc < 2) {
    interp->result = 
      "usage: granny_getIndices granny_object";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));
  
  dl = dfuCreateDynList(DF_LIST,  g->scene.ModelCount);
  for (ModelIndex = 0; ModelIndex < g->scene.ModelCount; ++ModelIndex) {
    Model = &g->scene.Models[ModelIndex];
    
    for(MeshIndex = 0;
	MeshIndex < Model->MeshCount;
	++MeshIndex) {
      Mesh = &Model->Meshes[MeshIndex];

      IndexBufferPointer =
	(unsigned int *) malloc(4*GrannyGetMeshIndexCount(Mesh->GrannyMesh));
      GrannyCopyMeshIndices(Mesh->GrannyMesh, 4, IndexBufferPointer);
      curdl =
	dfuCreateDynListWithVals(DF_LONG, 
				 GrannyGetMeshIndexCount(Mesh->GrannyMesh), 
				 IndexBufferPointer);
      dfuMoveDynListList(dl, curdl);
    }
  }
  
  return tclPutList(interp, dl);
}


static int grannyGetMeshNames(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id;
  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch searchEntry;
  Tcl_HashTable *tablePtr;
  
  static char model_mesh_string[256];
  
  Tcl_DString meshList;
  
  if (argc < 2) {
    interp->result = 
      "usage: granny_getMeshNames granny_object";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));

  Tcl_DStringInit(&meshList);
  
  tablePtr = &g->meshTable;
  entryPtr = Tcl_FirstHashEntry(tablePtr, &searchEntry);
  if (entryPtr) {
    Tcl_DStringAppendElement(&meshList, Tcl_GetHashKey(tablePtr, entryPtr));
    while ((entryPtr = Tcl_NextHashEntry(&searchEntry))) {
      Tcl_DStringAppendElement(&meshList, Tcl_GetHashKey(tablePtr, entryPtr));
    }
  }

  Tcl_DStringResult(interp, &meshList);
  return TCL_OK;
}


 static int grannyGetMeshMorphCounts(ClientData clientData, Tcl_Interp *interp,
				     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id;
  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch searchEntry;
  Tcl_HashTable *tablePtr;
  Tcl_DString meshList;
  mesh *meshPtr;
  char countstring[16];
  
  if (argc < 2) {
    interp->result = 
      "usage: granny_getMeshMorphCounts granny_object";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));
  
  Tcl_DStringInit(&meshList);
  
  tablePtr = &g->meshTable;
  entryPtr = Tcl_FirstHashEntry(tablePtr, &searchEntry);
  if (entryPtr) {
    meshPtr = (mesh *) Tcl_GetHashValue(entryPtr);
    sprintf(countstring, "%d", 
	    GrannyGetMeshMorphTargetCount(meshPtr->GrannyMesh));
    Tcl_DStringAppendElement(&meshList, countstring);
    while ((entryPtr = Tcl_NextHashEntry(&searchEntry))) {
      Tcl_DStringAppendElement(&meshList, countstring);
    }
  }
  
  Tcl_DStringResult(interp, &meshList);
  return TCL_OK;
}

 static int grannyGetBoneNames(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
 {
   OBJ_LIST *olist = (OBJ_LIST *) clientData;
   GRANNY_MODEL *g;
   int id;
   Tcl_HashEntry *entryPtr;
   Tcl_HashSearch searchEntry;
   Tcl_HashTable *tablePtr;

  static char model_bone_string[256];
 
  Tcl_DString boneList;

  if (argc < 2) {
    interp->result = 
      "usage: granny_getBoneNames granny_object";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));

  Tcl_DStringInit(&boneList);
  
  tablePtr = &g->boneTable;
  entryPtr = Tcl_FirstHashEntry(tablePtr, &searchEntry);
  if (entryPtr) {
    Tcl_DStringAppendElement(&boneList, Tcl_GetHashKey(tablePtr, entryPtr));
    while ((entryPtr = Tcl_NextHashEntry(&searchEntry))) {
      Tcl_DStringAppendElement(&boneList, Tcl_GetHashKey(tablePtr, entryPtr));
    }
  }

  Tcl_DStringResult(interp, &boneList);
  return TCL_OK;
}

static int grannyAllocatedCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  granny_allocation_header *Header;
  granny_allocation_information Info;
  for(Header = GrannyAllocationsBegin();
      Header != GrannyAllocationsEnd();
      Header = GrannyNextAllocation(Header)) {
    GrannyGetAllocationInformation(Header, &Info);
    
    fprintf(getConsoleFP(), "%s(%d): %d bytes allocated at address 0x%x\n",
	    Info.SourceFileName, Info.SourceLineNumber,
	    Info.RequestedSize, (unsigned int) Info.Memory);
  }
  return TCL_OK;
}

static int grannyModelCmd(ClientData clientData, Tcl_Interp *interp,
			  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  char *texturefile;
  int id, use_initial_placement = 1;
  granny_real64 loopcount = 0;  

  if (argc < 2) {
    interp->result = "usage: granny grannyfile [texturefile]";
    return TCL_ERROR;
  }
  if (argc > 2) texturefile = argv[2];
  else texturefile = NULL;

  if (argc > 3) {
    if (Tcl_GetInt(interp, argv[3], &use_initial_placement) != TCL_OK) 
      return TCL_ERROR;
  }

  if ((id = grannyCreate(olist, argv[1], use_initial_placement, texturefile)) < 0) {
    if (id == -2) {  /* Granny Error */ 
      Tcl_AppendResult(interp,
		       "error loading granny object \"", argv[1], "\"",  NULL);
   }
    else if (id == -3) {  /* Granny Texture File Error */
      Tcl_SetResult(interp, "error reading textures", TCL_STATIC);
    }
    else {
      Tcl_SetResult(interp, "error creating granny object", TCL_STATIC);
    }
    return(TCL_ERROR);
  }
  
  sprintf(interp->result,"%d", id);
  return(TCL_OK);
}



static int grannyCopyModelCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id;

  if (argc < 2) {
    interp->result = "usage: granny_copyModel objid";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  if ((id = grannyCopy(olist, g)) < 0) {
    if (id == -2) {  /* Granny Error */
      Tcl_AppendResult(interp, "error loading granny object \"", argv[1],
		       "\"",  NULL);
    }
    else if (id == -3) {  /* Granny Texture File Error */
      Tcl_SetResult(interp, "error reading textures", TCL_STATIC);
    }
    else {
      Tcl_SetResult(interp, "error creating granny object", TCL_STATIC);
    }
    return(TCL_ERROR);
  }
  
  sprintf(interp->result,"%d", id);
  return(TCL_OK);
}

static void
CreateTexture(texture *Texture,
              granny_texture *GrannyTexture,
	      char *tname)
{
  bool HasAlpha;
  unsigned char *PixelBuffer;
  
  /* The name of the texture is just the file name that
     the texture came from.  I'll use this later (in FindTexture())
     to match texture references to the textures I create here. */
  strcpy(Texture->Name, tname);
  
  // Loop over all the MIP levels and fill them.
  if((GrannyTexture->TextureType == GrannyColorMapTextureType) &&
     (GrannyTexture->ImageCount == 1))
    {
      // Grab the single image
      granny_texture_image *GrannyImage = &GrannyTexture->Images[0];
      
      int Width = GrannyTexture->Width;
      int Height = GrannyTexture->Height;
      if(GrannyImage->MIPLevelCount > 0)
        {
	  granny_texture_mip_level *GrannyMIPLevel =
	    &GrannyImage->MIPLevels[0];
	  
	  glGenTextures(1, &Texture->TextureHandle);
	  Texture->Allocated = 1;
	  
	  if(Texture->TextureHandle)
            {
	      glBindTexture(GL_TEXTURE_2D, Texture->TextureHandle);
	      
	      HasAlpha = GrannyTextureHasAlpha(GrannyTexture);
	      PixelBuffer = (unsigned char *) calloc(Width*Height, 4);
	      
	      /* GrannyGetImageInFormat takes any arbitrarily formatted
		 source texture, including Bink-compressed textures, and
		 spits them out in the RGBA format of your choice.  In this
		 case we pick either RGBA8888 (if the texture has alpha) or
		 BGR888, since that's how we submit the OpenGL texture. */
	      
	      GrannyCopyTextureImage(GrannyTexture, 0, 0,
				     HasAlpha ? GrannyRGBA8888PixelFormat :
				     GrannyRGB888PixelFormat,
				     Width, Height,
				     Width * (HasAlpha ? 4 : 3),
				     PixelBuffer);
	      
	      
	      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	      glPixelStorei(GL_UNPACK_ROW_LENGTH, Width);

	      glTexImage2D(GL_TEXTURE_2D, 0, HasAlpha ? 4 : 3, Width, Height, 0,
			   HasAlpha ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE,
			   PixelBuffer);
	      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	      
	      free(PixelBuffer);
            }
        }
    }    
  else
    {
      //        ErrorMessage("This sample only works with single-image "
      //             "ColorMapTextureType textures");
    }
}

/* DEFTUTORIAL EXPGROUP(BasicLoading) (BasicLoading_CreateModel, CreateModel) */
/* CreateModel() is what I call in OSStartup for each model in the
   file.  It goes through and creates a buffer for storing the state
   of the model and initializes the index and vertex buffers for all
   the meshes in the model. */
static void
CreateModel(GRANNY_MODEL *m, model *Model, 
	    scene *InScene, granny_model *GrannyModel)
{
  mesh *meshPtr;
  Tcl_HashEntry *entryPtr;
  int newentry;
  int MeshIndex;
  /* First, I grab the bone count for the model and check to see
     if it's the biggest I've seen so far.  If it is, I record it.
     This is used back in OSStartup() to determine how big to make
     the shared local pose buffer. */
  int BoneCount = GrannyModel->Skeleton->BoneCount;
  if(InScene->MaxBoneCount < BoneCount)
    {
      InScene->MaxBoneCount = BoneCount;
    }
  
  /* Now I ask Granny for two objects I'm going to use to animate
     the model.  The first is the granny_model_instance, which keeps
     track of what animations are playing on the model... */
  Model->GrannyInstance = GrannyInstantiateModel(GrannyModel);
  
  /* ... the second is the granny_world_pose, which is a buffer that
     stores the world-space matrices for all the bones of the model
     so that I can use them for rendering (or collision detection,
     etc.) at any time. */
  Model->WorldPose = GrannyNewWorldPose(BoneCount);
  
  /* Now, I loop through all the meshes in the model and process
     them. */
  Model->MeshCount = GrannyModel->MeshBindingCount;
  
  Model->Name = _strdup(GrannyModel->Name);
  Model->Meshes = (mesh *) calloc (Model->MeshCount, sizeof(mesh));
  {
    static char model_mesh_string[256];
    for(MeshIndex = 0;
	MeshIndex < Model->MeshCount;
	++MeshIndex) {
      meshPtr = &Model->Meshes[MeshIndex];
      CreateMesh(meshPtr,
		 GrannyModel->MeshBindings[MeshIndex].Mesh,
		 Model->GrannyInstance, InScene);
      
      /* Update hash table to add pointer to created mesh */
      sprintf(model_mesh_string, "%s::%s", Model->Name, meshPtr->Name); 
      entryPtr = Tcl_CreateHashEntry(&m->meshTable, model_mesh_string, 
				     &newentry);
      Tcl_SetHashValue(entryPtr, &Model->Meshes[MeshIndex]);
    }
  }
  
  /* Now fill up our bone table */
  {
    int BoneIndex;
    static char model_bone_string[256];
    granny_bone *bonePtr;
    granny_skeleton *skeleton = GrannyGetSourceSkeleton(Model->GrannyInstance);
    for(BoneIndex = 0;
	BoneIndex < skeleton->BoneCount;
	++BoneIndex) {
      bonePtr = &skeleton->Bones[BoneIndex];

      /* Update hash table to add pointer to created mesh */
      sprintf(model_bone_string, "%s", bonePtr->Name); 
      entryPtr = Tcl_CreateHashEntry(&m->boneTable, model_bone_string, 
				     &newentry);
      Tcl_SetHashValue(entryPtr, bonePtr);
    }
  }
}

dotfield *allocDotfield(int framepersistence, int dotsperupdate)
{
  int i;
  dotfield *df = (dotfield *) calloc(1, sizeof(dotfield));
  df->frameind = -1;
  df->framepersistence = framepersistence;
  df->dotsperupdate = dotsperupdate;
  df->trianglefordot = (int **) calloc(df->framepersistence, sizeof(int *));
  df->modelfordot = (int **) calloc(df->framepersistence, sizeof(int *));
  df->meshfordot = (int **) calloc(df->framepersistence, sizeof(int *));
  df->groupfordot = (int **) calloc(df->framepersistence, sizeof(int *));
  df->winX = (float **) calloc(df->framepersistence, sizeof(float *));
  df->winY = (float **) calloc(df->framepersistence, sizeof(float *));
  df->bary0 = (GLfloat **) calloc(df->framepersistence, sizeof(float *));
  df->bary1 = (GLfloat **) calloc(df->framepersistence, sizeof(float *));
  df->bary2 = (GLfloat **) calloc(df->framepersistence, sizeof(float *));
  df->screenbuffer = 0;
  for (i = 0; i < df->framepersistence; i++) {
    df->trianglefordot[i] = (int *) calloc(df->dotsperupdate, sizeof(int));
    df->modelfordot[i] = (int *) calloc(df->dotsperupdate, sizeof(int));
    df->meshfordot[i] = (int *) calloc(df->dotsperupdate, sizeof(int));
    df->groupfordot[i] = (int *) calloc(df->dotsperupdate, sizeof(int));
    df->winX[i] = (float *) calloc(df->dotsperupdate, sizeof(float));
    df->winY[i] = (float *) calloc(df->dotsperupdate, sizeof(float));
    df->bary0[i] = (GLfloat *) calloc(df->dotsperupdate, sizeof(float));
    df->bary1[i] = (GLfloat *) calloc(df->dotsperupdate, sizeof(float));
    df->bary2[i] = (GLfloat *) calloc(df->dotsperupdate, sizeof(float));
  }
  df->vertices = (GLfloat *)
    calloc(df->dotsperupdate*3*df->framepersistence, sizeof(GLfloat));
  df->drawlist = (GLuint *)
    calloc(df->dotsperupdate*df->framepersistence, sizeof(GLuint));
  df->drawcount = 0;
  return(df);
}

static void get_material_parameters(granny_material *material,
			     material_parameters *MaterialParameters)
{
  GrannyMergeSingleObject(material->ExtendedData.Type,
			  material->ExtendedData.Object,
			  MaterialParametersType, MaterialParameters, NULL);
}

/* DEFTUTORIAL EXPGROUP(BasicLoading) (BasicLoading_CreateMesh, CreateMesh) */
/* CreateMesh() is what I call in CreateModel() for each mesh in the
   model.  It creates Granny mesh->model bindings for each mesh. */
static void
CreateMesh(mesh *Mesh, granny_mesh *GrannyMesh,
           granny_model_instance *InModel, scene *InScene)
{
  int VertexCount, TextureIndex, i;
  int ntrigroups;
  material_parameters matparams;
  granny_tri_material_group *Group;

  granny_skeleton *Skeleton;
  Mesh->GrannyMesh = GrannyMesh;
  
  /* First I create the Granny binding for this mesh.  The binding
     is basically a table that says what bone slots of the mesh go
     with which bones of the model.  It's used during rendering to
     pull the correct matrices from the skeleton to use for
     deformation (or just to load the correct single transform in
     the case of rigid meshes). */
  Skeleton = GrannyGetSourceSkeleton(InModel);
  Mesh->GrannyBinding = GrannyNewMeshBinding(GrannyMesh, Skeleton, Skeleton);

  // just fix up the pointer, don't realloc
  Mesh->Name = (char *) GrannyMesh->Name; 
  Mesh->Visible = 1;
  
  Mesh->TextureCount = GrannyMesh->MaterialBindingCount;
  Mesh->TextureReferences = 
    (texture **) calloc(Mesh->TextureCount, sizeof(texture *));
  Mesh->TextureHandled =
    (int *) calloc(Mesh->TextureCount, sizeof(int));

  ntrigroups = GrannyGetMeshTriangleGroupCount(GrannyMesh);
  Mesh->TriGroupsVisible = (char *) calloc(ntrigroups, sizeof(char));
  Mesh->TriGroupsColors = (quad *) calloc(ntrigroups, sizeof(quad));

  Group = GrannyGetMeshTriangleGroups(GrannyMesh);
  for (i = 0; i < ntrigroups; i++) { 
    Mesh->TriGroupsVisible[i] = 1; 

    matparams.DiffuseColor[0] = 1.;
    matparams.DiffuseColor[1] = 1.;
    matparams.DiffuseColor[2] = 1.;
    matparams.SpecularColor[0] = 1.;
    matparams.SpecularColor[1] = 1.;
    matparams.SpecularColor[2] = 1.;
    matparams.Opacity = 1.;
    matparams.Transparency = 0.;

    if (GrannyMesh->MaterialBindings) {
      get_material_parameters(GrannyMesh->MaterialBindings[Group[i].MaterialIndex].Material, &matparams);
    }

    /*
     *   THINGS TO ADD IN THE FUTURE... ************
     *      1) Handle transparency
     *      2) Handle opacity maps
     *      3) Handle bump maps
     */

    /* Turn off transparent groups for now */
    if (matparams.Transparency == 1) Mesh->TriGroupsVisible[i] = 0;

    Mesh->TriGroupsColors[i].r = matparams.DiffuseColor[0];
    Mesh->TriGroupsColors[i].g = matparams.DiffuseColor[1];
    Mesh->TriGroupsColors[i].b = matparams.DiffuseColor[2];
    Mesh->TriGroupsColors[i].a = matparams.Opacity;

  }
  for(TextureIndex = 0;
      TextureIndex < Mesh->TextureCount;
      ++TextureIndex)
    {
      int handled;
      Mesh->TextureReferences[TextureIndex] =
	FindTexture(InScene, 
		    GrannyMesh->MaterialBindings[TextureIndex].Material, &handled);
      Mesh->TextureHandled[TextureIndex] = handled;
    }
  
  VertexCount = GrannyGetMeshVertexCount(GrannyMesh);
  {
    int VertexBufferSize = VertexCount * sizeof(granny_pnt332_vertex);
    
    if(InScene->MaxMutableVertexBufferSize < VertexBufferSize)
      {
	InScene->MaxMutableVertexBufferSize = VertexBufferSize;
      }
    
//    if(GrannyMeshIsRigid(GrannyMesh)) {
//      Mesh->GrannyDeformer = 0;
//    }
//    else {
      /* ... and then I create a Granny deformer for this mesh.  I
	 ask for deformations for the position and normal in this
	 case, since that's all I've got, but I could also ask for
	 deformations of the tangent space if I was doing bump
	 mapping. */
      Mesh->GrannyDeformer = 
	GrannyNewMeshDeformer(
			      GrannyGetMeshVertexType(GrannyMesh), 
			      GrannyPNT332VertexType,
			      GrannyDeformPositionNormal,
			      GrannyAllowUncopiedTail);
      if(!Mesh->GrannyDeformer) {
	fprintf(getConsoleFP(), "Granny didn't find a matching deformer for "
		"the vertex format used by mesh \"%s\".  This "
		"mesh won't be rendered properly.",
		GrannyMesh->Name);
      }
//    }
  }
}

/* DEFTUTORIAL EXPGROUP(BasicLoading) (BasicLoading_FindTexture, FindTexture) */
/* In a real application setting, you would have a texture manager or
   something similar where you get all your textures.  In this
   situation, I'm just loading textures out of the same file that the
   model data is in, so this routine linearly scans the textures in
   the file (which I've already submitted to OpenGL) for a matching name.
   A real app would use a hash table or something similar. */
static texture *
FindTexture(scene *Scene, granny_material *GrannyMaterial, int *handled)
{
  int TextureIndex;
  texture *Texture;
    /* I ask Granny for the diffuse color texture of this material,
       if there is one.  For a more complicated shader system,
       you would probably ask for more maps (like bump maps) and
       maybe query extra properties like shininess and so on. */
  granny_texture *GrannyTexture =
    GrannyGetMaterialTextureByType(GrannyMaterial,
				   GrannyDiffuseColorTexture);
  granny_texture *OpacityTexture =
    GrannyGetMaterialTextureByType(GrannyMaterial,
				   GrannyOpacityTexture);
  *handled = 1;
  if (OpacityTexture && GrannyTexture) {
    *handled = 0;
    return NULL;
  }
  
  if(GrannyTexture) {
    // Look through all the textures in the file for a match-by-name.
    {
      for(TextureIndex = 0;
	  TextureIndex < Scene->TextureCount;
	  ++TextureIndex) {
	Texture = &Scene->Textures[TextureIndex];

	if(strcmp(Texture->Name, GrannyTexture->FromFileName) == 0) {
	  return(Texture);
	}
      }
    }
  }
  return(NULL);
}
  
  void
RenderingSetup(void)
{    
  glEnable(GL_NORMALIZE);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);
  /* Should this be an optional parameter? */
  glDisable(GL_CULL_FACE);
}

void
RenderSceneDots(scene *InScene, dotfield *dots, dotfield *backdots, GR_OBJ *o)
{
  GLuint ModelIndex, MeshIndex;
  GLuint TriCounter;
  granny_pnt332_vertex *MeshVertices;
  granny_matrix_4x4 *CompositeBuffer;
  model *Model;
  granny_pnt332_vertex *Vertices;
  mesh *Mesh;
  int *ToBoneIndices;
  int VertexCount;
  int frameind = dots->frameind;
  GLint viewport[4];
  GLfloat modelviewmat[16];
  GLfloat projectionmat[16];
  float HALF_SCREEN_DEG_X;
  float HALF_SCREEN_DEG_Y;
  char *valstr;
  float SX = GR_SX(o);
  float SY = GR_SY(o);
  float TX = GR_TX(o);
  float TY = GR_TY(o);

  valstr = puGetParamEntry(getParamTable(), "HalfScreenDegreeX");
  HALF_SCREEN_DEG_X = (float) atof(valstr);
  valstr = puGetParamEntry(getParamTable(), "HalfScreenDegreeY");
  HALF_SCREEN_DEG_Y = (float) atof(valstr);
  glGetIntegerv(GL_VIEWPORT, viewport);

  glGetFloatv(GL_MODELVIEW_MATRIX, modelviewmat);
  glGetFloatv(GL_PROJECTION_MATRIX, projectionmat);
  draw_to_offscreen_buffer(1);
  glEnable(GL_DEPTH_TEST);
  glClearColor(1.0, 1.0, 1.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMultMatrixf(projectionmat);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glMultMatrixf(modelviewmat);

  glDisable(GL_BLEND);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  for (ModelIndex = 0; ModelIndex < (unsigned) InScene->ModelCount; ++ModelIndex)
  {
    Model = &InScene->Models[ModelIndex];
    CompositeBuffer = GrannyGetWorldPoseComposite4x4Array(Model->WorldPose);
    glEnableClientState(GL_VERTEX_ARRAY);
    
    for(MeshIndex = 0; MeshIndex < (unsigned) Model->MeshCount; ++MeshIndex)
      {
	TriCounter = (ModelIndex<<22) | (MeshIndex<<19);
	Vertices = InScene->MutableVertexBuffer;
	Mesh = &Model->Meshes[MeshIndex];
	ToBoneIndices = (int *) GrannyGetMeshBindingToBoneIndices(Mesh->GrannyBinding);
	VertexCount = GrannyGetMeshVertexCount(Mesh->GrannyMesh);
	if (!Mesh->Visible) continue;
	if(Mesh->GrannyDeformer) {
	  if (Mesh->MorphIndex) {
	    MeshVertices = GrannyGetMeshMorphVertices(Mesh->GrannyMesh,
						      Mesh->MorphIndex);
	  } else {
	    MeshVertices = GrannyGetMeshVertices(Mesh->GrannyMesh);
	  }
	  
	  GrannyDeformVertices(Mesh->GrannyDeformer,
			       ToBoneIndices, (float *)CompositeBuffer,
			       VertexCount,
			       MeshVertices,
			       Vertices);
	  RenderMeshIndexed(Mesh, Vertices, TriCounter, HALF_SCREEN_DEG_X, SX);
	}
      }
    glDisableClientState(GL_VERTEX_ARRAY);
  }
  
  glReadBuffer(GL_FRONT);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  
  //JED
  glReadPixels((int) ((viewport[2] - viewport[0]) * (1-dots->extentX+2*dots->dX)/2),
	       (int) ((viewport[3] - viewport[1]) * (1-dots->extentY+2*dots->dY)/2),
	       (int) ((viewport[2] - viewport[0]) * dots->extentX),
	       (int) ((viewport[3] - viewport[1]) * dots->extentY),
	       GL_RGBA, GL_UNSIGNED_BYTE, dots->screenbuffer);
  
/*for (i = 0; i < 32768; i+=4) {
	if ((dots->screenbuffer[i]+dots->screenbuffer[i+1]+dots->screenbuffer[i+2]+dots->screenbuffer[i+3])%255) {
fprintf(getConsoleFP(), "%2X%2X%2X%2X ", dots->screenbuffer[i], dots->screenbuffer[i+1], dots->screenbuffer[i+2], dots->screenbuffer[i+3]);
	}
}*/

  backdots->screenbuffer = dots->screenbuffer;

  CreateNewForegroundDots(dots);
  //The following assumes nothing else messes with ModelIndex and MeshIndex between where they get set and here.
  //Bad programming practice. Kids, don't try this at home.
  if (ModelIndex + MeshIndex > 0)
    {
      for (ModelIndex = 0; ModelIndex < (unsigned) InScene->ModelCount; ++ModelIndex)
	{
	  Model = &InScene->Models[ModelIndex];
	  CompositeBuffer = GrannyGetWorldPoseComposite4x4Array(Model->WorldPose);
	  for(MeshIndex = 0; MeshIndex < (unsigned) Model->MeshCount; ++MeshIndex)
	    {
	      TriCounter = (ModelIndex<<22) | (MeshIndex<<19);
	      Vertices = InScene->MutableVertexBuffer;
	      Mesh = &Model->Meshes[MeshIndex];
	      ToBoneIndices = (int *) GrannyGetMeshBindingToBoneIndices(Mesh->GrannyBinding);
	      VertexCount = GrannyGetMeshVertexCount(Mesh->GrannyMesh);
	      if (!Mesh->Visible) continue;
	      if(Mesh->GrannyDeformer) {
		if (Mesh->MorphIndex) {
		  MeshVertices = GrannyGetMeshMorphVertices(Mesh->GrannyMesh,
							    Mesh->MorphIndex);
		} else {
	  	  MeshVertices = GrannyGetMeshVertices(Mesh->GrannyMesh);
		}
		
		GrannyDeformVertices(Mesh->GrannyDeformer,
				     ToBoneIndices, (float *)CompositeBuffer,
				     VertexCount,
				     MeshVertices,
				     Vertices);
		ReadDotsForModelMesh(ModelIndex, MeshIndex, Mesh, Vertices, dots, HALF_SCREEN_DEG_X, HALF_SCREEN_DEG_Y, viewport, SX, SY, TX, TY);
	      }
	    }
	}
    } else
    {
      ReadDotsForModelMesh(0, 0, Mesh, Vertices, dots, HALF_SCREEN_DEG_X, HALF_SCREEN_DEG_Y, viewport, SX, SY, TX, TY);
    }
  
  ReadDotsForGround(backdots, HALF_SCREEN_DEG_X, HALF_SCREEN_DEG_Y, viewport, SX, SY, TX, TY);
  dots->frameind = (dots->frameind+1)%(dots->framepersistence);
  backdots->frameind = (backdots->frameind+1)%(backdots->framepersistence);
  draw_to_offscreen_buffer(0);
  glReadBuffer(GL_BACK);

  glDisable(GL_BLEND);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glEnableClientState(GL_VERTEX_ARRAY);
  
  /*fprintf(getConsoleFP(), "Going to draw %d foreground dots, with drawlist indices and X-vertices at:\n", dots->drawcount);
    for (i = 0; i < dots->drawcount; i++) {
    fprintf(getConsoleFP(), "(%d, %f) ", dots->drawlist[i], dots->vertices[dots->drawlist[i]*3]);
    }
    fprintf(getConsoleFP(), "\nDone listing all %d foreground dots.\n", dots->drawcount);*/
  
  glVertexPointer(3, GL_FLOAT, 0, dots->vertices);
  glPointSize(dots->dotsize);
  glColor4fv(dots->color);
  //JED
  glDrawElements(GL_POINTS, dots->drawcount, GL_UNSIGNED_INT, dots->drawlist);
  
  glVertexPointer(3, GL_FLOAT, 0, backdots->vertices);
  glPointSize(backdots->dotsize);
  glColor4fv(backdots->color);
  //JED
  glDrawElements(GL_POINTS, backdots->drawcount, GL_UNSIGNED_INT, backdots->drawlist);
  
  glDisableClientState(GL_VERTEX_ARRAY);
  glEnable(GL_LIGHTING);
  glEnable(GL_BLEND);
  backdots->screenbuffer = 0;
}


void
CreateNewForegroundDots(dotfield *dots)
{
  int dotind, frameind;
  
  dots->drawcount = 0;
  for (frameind = 0; frameind < dots->framepersistence; frameind++)
    {
      for (dotind = 0; dotind < dots->dotsperupdate; dotind++)
	{
	  if ((frameind == dots->frameind) || (dots->frameind == -1)) //we must pick new spots for the dots in this frame
	    {
	      //RAND_MAX+1 so that we don't get a value of 1.0, which when floored would be in a nonexistant row/column
	      dots->winX[frameind][dotind] = (float)rand()/(RAND_MAX+1);
	      dots->winY[frameind][dotind] = (float)rand()/(RAND_MAX+1);
	    }
	}
    }
}


//This would be a good place to enable the calculations for per-mesh coloring
void
ReadDotsForModelMesh(int modelInd, int meshInd, mesh const *Mesh, granny_pnt332_vertex const *Vertices,
		     dotfield *dots, float HALF_SCREEN_DEG_X, float HALF_SCREEN_DEG_Y, GLint *viewport,
		     float SX, float SY, float TX, float TY)
{
  int dotind, frameind, bigdotind;
  int IndexSize = GrannyGetMeshBytesPerIndex(Mesh->GrannyMesh);
  unsigned char *Indices = (unsigned char *) GrannyGetMeshIndices(Mesh->GrannyMesh);
  GLuint *GLuintIndices = 0;
  granny_tri_material_group *Group = GrannyGetMeshTriangleGroups(Mesh->GrannyMesh);
  int GroupCount = GrannyGetMeshTriangleGroupCount(Mesh->GrannyMesh);
  int MMGT[4];

  if (IndexSize==4) GLuintIndices = Indices;
  bigdotind = 0;

  for (frameind = 0; frameind < dots->framepersistence; frameind++)
  {
  for (dotind = 0; dotind < dots->dotsperupdate; dotind++, bigdotind+=3)
  {
  	GetModelMeshGroupTri(dots, frameind, dotind, viewport, MMGT);
  	if ((frameind == dots->frameind) || (dots->frameind == -1)) //we must update the new dots in this frame
  	{
	  if (MMGT[0] == modelInd && MMGT[1] == meshInd) //we've hit the model with a new dot (if we missed, we don't care)
	  {
        dots->trianglefordot[frameind][dotind] = MMGT[3];
        dots->modelfordot[frameind][dotind] = MMGT[0];
        dots->meshfordot[frameind][dotind] = MMGT[1];
        dots->groupfordot[frameind][dotind] = MMGT[2];
		EucToBary(
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3])->Position[0],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3])->Position[1],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3])->Position[2],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+1])->Position[0],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+1])->Position[1],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+1])->Position[2],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+2])->Position[0],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+2])->Position[1],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+2])->Position[2],
			  (((dots->winX[frameind][dotind]-.5)*dots->extentX+dots->dX)*2*HALF_SCREEN_DEG_X-TX)/SX,
			  (((dots->winY[frameind][dotind]-.5)*dots->extentY+dots->dY)*2*HALF_SCREEN_DEG_Y-TY)/SY,
			  &dots->bary0[frameind][dotind], &dots->bary1[frameind][dotind], &dots->bary2[frameind][dotind], 1);
		BaryToEuc(
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3])->Position[0],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3])->Position[1],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3])->Position[2],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+1])->Position[0],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+1])->Position[1],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+1])->Position[2],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+2])->Position[0],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+2])->Position[1],
			  (Vertices+GLuintIndices[((Group+MMGT[2])->TriFirst+MMGT[3])*3+2])->Position[2],
			  dots->bary0[frameind][dotind], dots->bary1[frameind][dotind], dots->bary2[frameind][dotind],
			  &dots->vertices[bigdotind]);
		dots->drawlist[dots->drawcount++] = bigdotind/3;
		//fprintf(getConsoleFP(), "n");
	  }
	} else if (MMGT[0] == modelInd && MMGT[1] == meshInd) //an old dot that was on the model
	{
//calculate where this would hit; if it's still on the model and its tri is seen, add it and vertices to frontlist...
//if it's on the model but it's occluded, or if it's off the model, don't.
//for now, though, let's just assume it's still okay? Because due to aliasing,
//triangles are inconsistent frame-to-frame, so we lose a lot of dots otherwise.
	  BaryToEuc(
		    (Vertices+GLuintIndices[((Group+dots->groupfordot[frameind][dotind])->TriFirst+dots->trianglefordot[frameind][dotind])*3])->Position[0],
		    (Vertices+GLuintIndices[((Group+dots->groupfordot[frameind][dotind])->TriFirst+dots->trianglefordot[frameind][dotind])*3])->Position[1],
		    (Vertices+GLuintIndices[((Group+dots->groupfordot[frameind][dotind])->TriFirst+dots->trianglefordot[frameind][dotind])*3])->Position[2],
		    (Vertices+GLuintIndices[((Group+dots->groupfordot[frameind][dotind])->TriFirst+dots->trianglefordot[frameind][dotind])*3+1])->Position[0],
		    (Vertices+GLuintIndices[((Group+dots->groupfordot[frameind][dotind])->TriFirst+dots->trianglefordot[frameind][dotind])*3+1])->Position[1],
		    (Vertices+GLuintIndices[((Group+dots->groupfordot[frameind][dotind])->TriFirst+dots->trianglefordot[frameind][dotind])*3+1])->Position[2],
		    (Vertices+GLuintIndices[((Group+dots->groupfordot[frameind][dotind])->TriFirst+dots->trianglefordot[frameind][dotind])*3+2])->Position[0],
		    (Vertices+GLuintIndices[((Group+dots->groupfordot[frameind][dotind])->TriFirst+dots->trianglefordot[frameind][dotind])*3+2])->Position[1],
		    (Vertices+GLuintIndices[((Group+dots->groupfordot[frameind][dotind])->TriFirst+dots->trianglefordot[frameind][dotind])*3+2])->Position[2],
		    dots->bary0[frameind][dotind], dots->bary1[frameind][dotind], dots->bary2[frameind][dotind],
		    &dots->vertices[bigdotind]);
	  dots->drawlist[dots->drawcount++] = bigdotind/3;
	  //fprintf(getConsoleFP(), "o");
	}
  }
  //fprintf(getConsoleFP(), "Done setting barycentric coordinates for this frame\n");
  }
  //fprintf(getConsoleFP(), "Done setting barycentric coordinates for all frames\n");
}


void
ReadDotsForGround(dotfield *dots, float HALF_SCREEN_DEG_X, float HALF_SCREEN_DEG_Y, GLint *viewport,
  float SX, float SY, float TX, float TY)
{
  int bigdotind = 0;
  int dotind, frameind;
  int MMGT[4];

  dots->drawcount = 0;
  for (frameind = 0; frameind < dots->framepersistence; frameind++)
  {
  for (dotind = 0; dotind < dots->dotsperupdate; dotind++, bigdotind+=3)
  {
  	if ((frameind == dots->frameind) || (dots->frameind == -1))
  	{
//RAND_MAX+1 so that we don't get a value of 1.0, which when floored would be in a nonexistant row/column
  	  dots->winX[frameind][dotind] = (float)rand()/(RAND_MAX+1);
  	  dots->winY[frameind][dotind] = (float)rand()/(RAND_MAX+1);
      dots->vertices[bigdotind] = (((dots->winX[frameind][dotind]-.5)*dots->extentX+dots->dX)*2*HALF_SCREEN_DEG_X-TX)/SX;
      dots->vertices[bigdotind+1] = (((dots->winY[frameind][dotind]-.5)*dots->extentY+dots->dY)*2*HALF_SCREEN_DEG_Y-TY)/SY;
      dots->vertices[bigdotind+2] = 0;
	}
  	GetModelMeshGroupTri(dots, frameind, dotind, viewport, MMGT);
  	if (MMGT[0] == -1)
  	{
      dots->drawlist[dots->drawcount++] = bigdotind/3;
    }
  }
  }
}


void GetModelMeshGroupTri(dotfield *dots, int frameind, int dotind, GLint *viewport, int *MMGT)
{
  GLubyte *tempPix;

//MMGT needs room for four ints, indices for model, mesh, group, and triangle respectively.
//JED
/*  bufferind = ((int)(dots->winX[frameind][dotind] * ((int)((viewport[2] - viewport[0]) * dots->extentX))) +
    (int)(dots->winY[frameind][dotind] * ((int)((viewport[3] - viewport[1]) * dots->extentY))) *
    ((int)((viewport[2] - viewport[0]) * dots->extentX))) * 4;
if (bufferind > 145664) {
fprintf(getConsoleFP(), "bufferind is %d, because:\n", bufferind);
fprintf(getConsoleFP(), "dpX = %f, width is %d, int of product is %d\n", dots->winX[frameind][dotind], ((int)((viewport[2] - viewport[0]) * dots->extentX)), (int)(dots->winX[frameind][dotind] * ((int)((viewport[2] - viewport[0]) * dots->extentX))));
fprintf(getConsoleFP(), "dpY = %f, height is %d, int of product is %d\n", dots->winY[frameind][dotind], ((int)((viewport[3] - viewport[1]) * dots->extentY)), (int)(dots->winY[frameind][dotind] * ((int)((viewport[3] - viewport[1]) * dots->extentY))));
fprintf(getConsoleFP(), "product of Y-term and row width is %d\n\n", (int)(dots->winY[frameind][dotind] * ((int)((viewport[3] - viewport[1]) * dots->extentY))) *
    ((int)((viewport[2] - viewport[0]) * dots->extentX)));
}*/
  tempPix = &dots->screenbuffer[((int)(dots->winX[frameind][dotind] * ((int)((viewport[2] - viewport[0]) * dots->extentX))) +
    (int)(dots->winY[frameind][dotind] * ((int)((viewport[3] - viewport[1]) * dots->extentY))) *
    ((int)((viewport[2] - viewport[0]) * dots->extentX))) * 4];
  if (*((unsigned int *) tempPix) == 0xffffffff) {
	MMGT[0] = MMGT[1] = MMGT[2] = MMGT[3] = -1;
  } else
  {
    MMGT[3] = 256 * (int) tempPix[1] + (int) tempPix[0];
    MMGT[0] = (int) (tempPix[2]>>6);
    MMGT[1] = (int) ((tempPix[2] & 0x38) >> 3);
    MMGT[2] = (int) (tempPix[2] & 0x07);

//fprintf(getConsoleFP(), "%d %d %d %d\n", MMGT[0], MMGT[1], MMGT[2], MMGT[3]);

  }
}


void
RenderMeshIndexed(mesh const *Mesh, granny_pnt332_vertex const *Vertices, GLuint TC, float HALF_SCREEN_DEG_X, float SX)
{
  GLuint localTC, triTC, triind, i;
  int IndexSize = GrannyGetMeshBytesPerIndex(Mesh->GrannyMesh);
  unsigned char *Indices = (unsigned char *)
    GrannyGetMeshIndices(Mesh->GrannyMesh);
  GLuint GroupCount = GrannyGetMeshTriangleGroupCount(Mesh->GrannyMesh);
  granny_tri_material_group *Group =
    GrannyGetMeshTriangleGroups(Mesh->GrannyMesh);
  GLint viewport[4];

  glGetIntegerv(GL_VIEWPORT, viewport);
  glVertexPointer(3, GL_FLOAT, sizeof(*Vertices), Vertices->Position);
  
//glPushMatrix();
//glTranslatef(-HALF_SCREEN_DEG_X*2/SX/(viewport[2]-viewport[0]), -HALF_SCREEN_DEG_X/SX/(viewport[2]-viewport[0]), 0.0);
//this seems to be counterproductive now. Not sure what's different.
//OK, so we seem to need this with stimgui/classify/characters, but not with stimcmd/jeddotsphere (even with the same model). Weird.
//Now, we don't need it with stimgui/classify/characters. Even weirder.
  for (i = 0; i < GroupCount; i++, Group++)
  {
  	localTC = TC | (i<<16);
    if (Mesh->TriGroupsVisible[i]) 
	{
	  for (triind = 0; triind < (unsigned) Group->TriCount; triind++)
	  {
	  	triTC = localTC | (triind);
	  	glColor3ubv(&triTC);
	  	glDrawElements(GL_TRIANGLES, 3, (IndexSize == 4) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
		     &Indices[(Group->TriFirst+triind)*3*IndexSize]);
	  }
    }
  }
//glPopMatrix();
}


void BaryToEuc(GLfloat x1, GLfloat y1, GLfloat z1, GLfloat x2, GLfloat y2, GLfloat z2,
  GLfloat x3, GLfloat y3, GLfloat z3, GLfloat b1, GLfloat b2, GLfloat b3, GLfloat *Euc)
{
	/*Euc needs to have room for three GLfloats*/
	Euc[0] = b1*x1 + b2*x2 + b3*x3;
	Euc[1] = b1*y1 + b2*y2 + b3*y3;
	Euc[2] = b1*z1 + b2*z2 + b3*z3;
}

void EucToBary(GLfloat x1, GLfloat y1, GLfloat z1, GLfloat x2, GLfloat y2, GLfloat z2,
  GLfloat x3, GLfloat y3, GLfloat z3, GLfloat p1, GLfloat p2, GLfloat *b1, GLfloat *b2, GLfloat *b3, int solveInverse)
{
	GLfloat A, B, C, D, E, F, G, H, I, swapper;
	GLfloat bx, by, bz, cx, cy, cz, p3, nx, ny, nz;

	if (solveInverse)
	{
		bx = x1 - x2;
		cx = x3 - x2;
		by = y1 - y2;
		cy = y3 - y2;
		bz = z1 - z2;
		cz = z3 - z2;
		nx = by*cz - bz*cy;
		ny = bz*cx - bx*cz;
		nz = bx*cy - by*cx;
		p3 = (nx*(x1-p1) + ny*(y1-p2) + nz*z1)/nz;

		A = x1 - x3;
		B = x2 - x3;
		C = x3 - p1;
		D = y1 - y3;
		E = y2 - y3;
		F = y3 - p2;
		G = z1 - z3;
		H = z2 - z3;
		I = z3 - p3;
		if (!((A*(E+H) - B*(D+G)) && (B*(D+G) - A*(E+H))))
		{
			swapper = A; A = D; D = swapper;
			swapper = B; B = E; E = swapper;
			swapper = C; C = F; F = swapper;
		}
		b1[0] = (B*(F+I) - C*(E+H)) / (A*(E+H) - B*(D+G));
		b2[0] = (A*(F+I) - C*(D+G)) / (B*(D+G) - A*(E+H));
		b3[0] = 1 - b1[0] - b2[0];
	} else
	{
//This actually doesn't run any faster at all. Just going to set solveInverse to 1 where EucToBary is called.
		b1[0] = (GLfloat) (1 - sqrt((float)rand()/RAND_MAX));
		b2[0] = (GLfloat) (1 - b1[0]) * (float)rand()/RAND_MAX;
		b3[0] = (GLfloat) (1 - b1[0] - b2[0]);
	}
}


void
RenderModel(scene *InScene, model *Model)
{
  int MeshIndex;
  granny_pnt332_vertex *MeshVertices;

    /* Since I'm going to need it constantly, I dereference the composite
       transform buffer for the model's current world-space pose.  This
       buffer holds the transforms that move vertices from the position
       in which they were modeled to their current position in world space. */
    granny_matrix_4x4 *CompositeBuffer =
        GrannyGetWorldPoseComposite4x4Array(Model->WorldPose);

    /* Before I do any rendering, I enable the arrays for the vertex
       format I'm using.  You could do this once for the entire app,
       since I never render anything else, but I figured it'd me more
       instructive to put it here where I actually do the rendering. */
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    // Render all the meshes.
    for(MeshIndex = 0;
         MeshIndex < Model->MeshCount;
         ++MeshIndex)
    {
      granny_pnt332_vertex *Vertices = InScene->MutableVertexBuffer;
      mesh *Mesh = &Model->Meshes[MeshIndex];
      
      // Dereference the index table that maps mesh bone indices to
      // bone indices in the model.
      int *ToBoneIndices =
	(int *) GrannyGetMeshBindingToBoneIndices(Mesh->GrannyBinding);
      
      // Load the mesh's vertices, or deform into a temporary
      // buffer and load those, depending on whether the mesh is
      // rigid or not.
      int VertexCount = GrannyGetMeshVertexCount(Mesh->GrannyMesh);
      
      if (!Mesh->Visible) continue;

      if(GrannyMeshIsRigid(Mesh->GrannyMesh)) {
	granny_matrix_4x4 *Transform;
	// It's a rigid mesh, so I use the display list we've
	// previously created for it.
	glPushMatrix();
	Transform = &CompositeBuffer[ToBoneIndices[0]];
	glMultMatrixf((GLfloat *)Transform);
	
	if (Mesh->MorphIndex) {
	  MeshVertices = GrannyGetMeshMorphVertices(Mesh->GrannyMesh, 
						Mesh->MorphIndex);
	}
	else {
	  MeshVertices = GrannyGetMeshVertices(Mesh->GrannyMesh);
	}
	RenderMesh(Mesh, MeshVertices);

	glPopMatrix();
      }
      else if(Mesh->GrannyDeformer) {
	// Tell Granny to deform the vertices of the mesh with the
	// current world pose of the model, and dump the results
	// into the mutable vertex buffer.

	if (Mesh->MorphIndex) {
	  MeshVertices = GrannyGetMeshMorphVertices(Mesh->GrannyMesh, 
						Mesh->MorphIndex);
	}
	else {
	  MeshVertices = GrannyGetMeshVertices(Mesh->GrannyMesh);
	}
	
	GrannyDeformVertices(Mesh->GrannyDeformer,
			     ToBoneIndices, (float *)CompositeBuffer,
			     VertexCount,
			     MeshVertices,
			     Vertices);
	RenderMesh(Mesh, Vertices);
      }
    }
    
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

/* DEFTUTORIAL EXPGROUP(BasicLoading) (BasicLoading_RenderMesh, RenderMesh) */
/* RenderMesh() is a utility function that will set up the vertex
   array pointers for a vertex buffer, and then render the triangles
   in each material group of a mesh. */
void
RenderMesh(mesh const *Mesh, granny_pnt332_vertex const *Vertices)
{
    /* Now both the indices and vertices are loaded, so I can
       render.  I grab the material groups and spin over them,
       changing to the appropriate texture and rendering each batch.
       A more savvy rendering loop might have instead built a
       sorted list of material groups to minimize texture changes,
       etc., but this is the most basic way to render. */
  FILE *console = getConsoleFP();
  int i;
  int IndexSize = GrannyGetMeshBytesPerIndex(Mesh->GrannyMesh);
  unsigned char *Indices = (unsigned char *)
    GrannyGetMeshIndices(Mesh->GrannyMesh);
  int GroupCount = GrannyGetMeshTriangleGroupCount(Mesh->GrannyMesh);
  granny_tri_material_group *Group =
    GrannyGetMeshTriangleGroups(Mesh->GrannyMesh);
  
  glVertexPointer(3, GL_FLOAT, sizeof(*Vertices), Vertices->Position);
  glNormalPointer(GL_FLOAT, sizeof(*Vertices), Vertices->Normal);
  glTexCoordPointer(2, GL_FLOAT, sizeof(*Vertices), Vertices->UV);
  
  for (i = 0; i < GroupCount; i++, Group++) {
    if (Mesh->TriGroupsVisible[i]) {
      glColor4f(Mesh->TriGroupsColors[i].r,
		Mesh->TriGroupsColors[i].g,
		Mesh->TriGroupsColors[i].b,
		Mesh->TriGroupsColors[i].a);

#ifdef VERBOSE_DEBUG      
      fprintf(console, "Render Mesh: %s %d %d %d\n", 
	      Mesh->GrannyMesh->MaterialBindings[Group->MaterialIndex].Material->Name, Group->MaterialIndex, Mesh->TextureCount, Mesh->TextureHandled[Group->MaterialIndex]);
#endif      

      if (Group->MaterialIndex < Mesh->TextureCount) {
	texture *Texture = Mesh->TextureReferences[Group->MaterialIndex];
	if(Texture) {
	  glEnable(GL_BLEND);
	  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	  glBindTexture(GL_TEXTURE_2D, Texture->TextureHandle);
	}
	else {
	  
	  /* This shouldn't happen -- but it does sometimes... */
	  
	  if (!Mesh->TextureHandled[Group->MaterialIndex]) continue;
	  glBindTexture(GL_TEXTURE_2D, 0);
	}
      }
      else {
	glBindTexture(GL_TEXTURE_2D, 0);
      }
      
      glDrawElements(
		     GL_TRIANGLES,
		     Group->TriCount*3,
		     (IndexSize == 4) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
		     &Indices[Group->TriFirst*3*IndexSize]);
    }
  }
}


static int grannyGetBounds(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id, animation = 0;
  int ModelIndex, MeshIndex, BoneBindingIndex, *ToBoneIndices;
  mesh *Mesh;
  model *Model;
  granny_bone_binding BoneBinding;
  float *OBBMin;
  float *OBBMax;
  float mins[3], maxs[3];

  if (argc < 2) {
    interp->result = "usage: granny_getBounds granny_object";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }

  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));

  for (ModelIndex = 0; ModelIndex < g->scene.ModelCount; ++ModelIndex) {
    Model = &g->scene.Models[ModelIndex];
    
    for (MeshIndex = 0;
	 MeshIndex < Model->MeshCount;
	 ++MeshIndex) {
      Mesh = &Model->Meshes[MeshIndex];
      
      ToBoneIndices =
	(int *) GrannyGetMeshBindingToBoneIndices(Mesh->GrannyBinding);
      
      for (BoneBindingIndex = 0;
	   BoneBindingIndex < Mesh->GrannyMesh->BoneBindingCount;
	   ++BoneBindingIndex) {
	BoneBinding =
	  Mesh->GrannyMesh->BoneBindings[BoneBindingIndex];
	
	OBBMin = BoneBinding.OBBMin;
	OBBMax = BoneBinding.OBBMax;
	
	if (!ModelIndex && !MeshIndex && !BoneBindingIndex) {
	  mins[0] = OBBMin[0];
	  mins[1] = OBBMin[1];
	  mins[2] = OBBMin[2];
	  maxs[0] = OBBMax[0];
	  maxs[1] = OBBMax[1];
	  maxs[2] = OBBMax[2];
	}
	else {
	  if (OBBMin[0] < mins[0]) mins[0] = OBBMin[0];
	  if (OBBMin[1] < mins[1]) mins[1] = OBBMin[1];
	  if (OBBMin[2] < mins[2]) mins[2] = OBBMin[2];
	  if (OBBMax[0] > maxs[0]) maxs[0] = OBBMax[0];
	  if (OBBMax[1] > maxs[1]) maxs[1] = OBBMax[1];
	  if (OBBMax[2] > maxs[2]) maxs[2] = OBBMax[2];
	}
      }
    }
  }
  sprintf(interp->result, "%.5f %.5f %.5f %.5f %.5f %.5f",
	  mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);
  
  return TCL_OK;
}


static int grannySetOntimeCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id;
  double ontime;
  
  if (argc < 3) {
    interp->result = 
      "usage: granny_setBegin granny_object begin(sec) [animation]";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &ontime) != TCL_OK) return TCL_ERROR;

  g->ontime = ontime;

  return(TCL_OK);
}

static int grannyReplaceTextureCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;
  int slot;
  int texid;
  GRANNY_MODEL *g;
  texture *Texture;

  if (argc < 4) {
    interp->result = "usage: granny_replaceTexture objid slot texid";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }

  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));


  if (Tcl_GetInt(interp, argv[2], &slot) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &texid) != TCL_OK) return TCL_ERROR;

  if (slot >= g->scene.TextureCount) {
    Tcl_AppendResult(interp, argv[0], ": texture slot out of range", NULL);
    return TCL_ERROR;
  }

  Texture = &g->scene.Textures[slot];
  if (Texture->Allocated)
    glDeleteTextures(1, &Texture->TextureHandle);
  Texture->TextureHandle = texid;
  Texture->Allocated = 0;

  return TCL_OK;
}


static int grannyAddAnimationCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  granny_file *LoadedFile;
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  granny_file_info *FileInfo; 
  model *model = NULL;
  int i, id, animation_id = 0;
  granny_art_tool_info *ArtToolInfo;
  double attime = 0.0f;

  granny_real32 Affine3[3];	/* for recomputing basis */
  granny_real32 Linear3x3[9];
  granny_real32 InverseLinear3x3[9];

  /* This is our desired coordinate system */
  granny_real32 Origin[] = {0, 0, 0};
  granny_real32 RightVector[] = {1, 0, 0};
  granny_real32 UpVector[] = {0, 1, 0};
  granny_real32 BackVector[] = {0, 0, 1};
  granny_real32 UnitsPerMeter = 1.0;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", 
		     argv[0], " objid animfile ?modelname?", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }

  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (g->nanimations >= MAX_ANIMATIONS) {
    Tcl_AppendResult(interp, argv[0], ": animation count exceeded", NULL);
    return TCL_ERROR;
  }
  
  if (argc > 3) {
    for(i = 0; i < g->scene.ModelCount; i++) {
      model = &g->scene.Models[i];
      if (!strcmp(model->Name, argv[3])) break;
    }
    if (i == g->scene.ModelCount) {
      Tcl_AppendResult(interp, argv[0], ": model ", argv[4], 
		       " not found", NULL);
      return TCL_ERROR;
    }
  }
  else if (g->scene.ModelCount) {
    model = &g->scene.Models[0];
  }
  else {
    Tcl_AppendResult(interp, argv[0], ": no models found", NULL);
    return TCL_ERROR;
  }
  
  LoadedFile = GrannyReadEntireFile(argv[2]);
  if (!LoadedFile) {
    Tcl_AppendResult(interp, argv[0], ": error reading animation from ", 
		     argv[2], NULL);
    return TCL_ERROR;
  }

  FileInfo = GrannyGetFileInfo(LoadedFile);
  ArtToolInfo = FileInfo ->ArtToolInfo;
    
  if (FileInfo) {
    if (!FileInfo->AnimationCount || 
	FileInfo->AnimationCount <= animation_id) {
      GrannyFreeFile(LoadedFile);
      Tcl_SetResult(interp, "-1", TCL_STATIC);
      return TCL_OK;
    }

    UnitsPerMeter = ArtToolInfo->UnitsPerMeter;
    /* Special case: this is the default conversion factor out of Max */
    if (UnitsPerMeter > 39.3 && UnitsPerMeter < 39.4)
      UnitsPerMeter = 1.0;	/* inches */
    if (UnitsPerMeter == 100.)
      UnitsPerMeter = 1.0;	/* cm */

    /* Tell Granny to construct the transform from the file's */
    /* coordinate system to our coordinate system */
    GrannyComputeBasisConversion(FileInfo, UnitsPerMeter, 
				 Origin, RightVector, UpVector, 
				 BackVector,
				 Affine3, Linear3x3, InverseLinear3x3);
    
    /* Tell Granny to transform the file into our coordinate system */
    GrannyTransformFile(FileInfo, Affine3, Linear3x3, 
			InverseLinear3x3,  1e-5f, 1e-5f,
			GrannyRenormalizeNormals | 
			GrannyReorderTriangleIndices);
  }

  g->animations[g->nanimations].animation = 
    FileInfo->Animations[animation_id]; 
  g->animations[g->nanimations].model = model;
  g->animations[g->nanimations].control = NULL;
  
  /* Default to showing the whole thing */
  g->animations[g->nanimations].end = 
    FileInfo->Animations[animation_id]->Duration;
  
  /* Figure out when to free this?! */
  g->animations[g->nanimations].loaded_file = LoadedFile;
  
  sprintf(interp->result, "%d", g->nanimations++);
  return TCL_OK;
}


static int grannyPlayAnimationCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id, animation = 0;
  double begin;
  double speed = 1.0;
  int loopcount = 1;
  
  if (argc < 4) {
    interp->result = 
      "usage: granny_playAnimation object animation startAt ?speed? ?loopcount?";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));
  
  if (Tcl_GetInt(interp, argv[2], &animation) != TCL_OK) return TCL_ERROR;
  if (animation >= g->nanimations) {
    Tcl_AppendResult(interp, argv[0], ": animation id out of range", NULL);
    return TCL_ERROR;
  }
  if (Tcl_GetDouble(interp, argv[3], &begin) != TCL_OK) return TCL_ERROR;
  if (argc > 4) {
    if (Tcl_GetDouble(interp, argv[4], &speed) != TCL_OK) return TCL_ERROR;
  }
  if (argc > 5) {
    if (Tcl_GetInt(interp, argv[5], &loopcount) != TCL_OK) return TCL_ERROR;
  }

  g->animations[animation].begin = begin;
  g->animations[animation].speed = speed;
  g->animations[animation].loopcount = loopcount;
  g->animations[animation].clock_override = -1;
	g->animations[animation].control =
	  GrannyPlayControlledAnimation(0.0,
					g->animations[animation].animation,
					g->animations[animation].model->GrannyInstance);
	GrannySetControlSpeed(g->animations[animation].control, speed);
	GrannySetControlLoopCount(g->animations[animation].control, loopcount);
    GrannySetControlForceClampedLooping(g->animations[animation].control, 1);
	if (g->animations[animation].easein > 0.0) {
	  GrannyEaseControlIn(g->animations[animation].control,
			      g->animations[animation].easein,
			      g->animations[animation].easein_from_current);
	}
	if (g->animations[animation].easeout > 0.0) {
/*	   GrannySetControlEaseOut(g->animations[animation].control, 1);
	   GrannySetControlEaseOutCurve(g->animations[animation].control,
        g->animations[animation].end, g->animations[animation].end + g->animations[animation].easeout,
        1.0, 1.0, 0.0, 0.0);*/
	}
	g->animations[animation].been_played = 1;
	GrannyFreeControlOnceUnused(g->animations[animation].control);

  return(TCL_OK);
}

static int grannyCompleteAnimationCmd(ClientData clientData, Tcl_Interp *interp,
				      int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id, animation = 0;
  double end;
  
  if (argc < 4) {
    interp->result = 
      "usage: granny_setEnd granny_object animation completeAt";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &animation) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &end) != TCL_OK) return TCL_ERROR;

  if (animation >= g->nanimations) {
    Tcl_AppendResult(interp, argv[0], ": animation id out of range", NULL);
    return TCL_ERROR;
  }

  g->animations[animation].end = end;
  return(TCL_OK);
}


static int grannyEaseControlInCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id, animation = 0, from_current = 0;
  double easein;
  
  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0], 
		     " granny_object animation easein_duration ?from_current?", 
		     NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &animation) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &easein) != TCL_OK) return TCL_ERROR;

  if (argc > 4) {
    if (Tcl_GetInt(interp, argv[4], &from_current) != TCL_OK) return TCL_ERROR;
  }
  
  if (animation >= g->nanimations) {
    Tcl_AppendResult(interp, argv[0], ": animation id out of range", NULL);
    return TCL_ERROR;
  }
  g->animations[animation].easein = easein;
  g->animations[animation].easein_from_current = from_current;
  return(TCL_OK);
}


static int grannyEaseControlOutCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id, animation = 0;
  double easeout;
  
  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0], 
		     " granny_object animation easeout_duration", 
		     NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &animation) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &easeout) != TCL_OK) return TCL_ERROR;

  if (animation >= g->nanimations) {
    Tcl_AppendResult(interp, argv[0], ": animation id out of range", NULL);
    return TCL_ERROR;
  }

  g->animations[animation].easeout = easeout;
  return(TCL_OK);
}



static int grannySetControlWeightCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id, animation = 0;
  double weight;
  
  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0], 
		     " granny_object animation weight",
		     NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &animation) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &weight) != TCL_OK) return TCL_ERROR;

  if (animation >= g->nanimations) {
    Tcl_AppendResult(interp, argv[0], ": animation id out of range", NULL);
    return TCL_ERROR;
  }

  if (g->animations && &g->animations[animation] && g->animations[animation].control) {
	GrannySetControlWeight(g->animations[animation].control, weight);
  }
  return(TCL_OK);
}




static int grannySetColorCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  double red, green, blue, alpha = 1.0;
  int id;

  if (argc < 5) {
    interp->result = "usage: granny_setColor objid r g b ?alpha?";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &red) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &green) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &blue) != TCL_OK) return TCL_ERROR;
  if (argc > 5) {
    if (Tcl_GetDouble(interp, argv[5], &alpha) != TCL_OK) return TCL_ERROR;
  }

  g->color[0] = red;
  g->color[1] = green;
  g->color[2] = blue;
  g->color[3] = alpha;

  return(TCL_OK);
}

static int find_granny_mesh(Tcl_Interp *interp, OBJ_LIST *olist, char *a0,
			    char *idstr, char *meshname, mesh **meshPtr)
{
  GRANNY_MODEL *g;
  int id;
  Tcl_HashEntry *entryPtr;

  if (Tcl_GetInt(interp, idstr, &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, a0, ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, a0, ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));
  
  if (entryPtr = Tcl_FindHashEntry(&g->meshTable, meshname)) {
      *meshPtr = (mesh *) Tcl_GetHashValue(entryPtr);
      return TCL_OK;
  }
  else {
    Tcl_AppendResult(interp, a0, ": mesh \"", meshname, "\" not found", 
		     NULL);
    return TCL_ERROR;
  }
}

static int grannySetMeshVisibleCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  mesh *meshPtr;
  int val;

  if (argc < 3) {
    interp->result = "usage: granny_setMeshVisible granny_obj name ?val?";
    return TCL_ERROR;
  }

  if (find_granny_mesh(interp, olist, argv[0], argv[1], argv[2], 
		       &meshPtr) != TCL_OK) 
    return TCL_ERROR;

  /* always return old value */
  sprintf(interp->result, "%d", meshPtr->Visible);

  if (argc > 3) {
    if (Tcl_GetInt(interp, argv[3], &val) != TCL_OK) return TCL_ERROR;
    meshPtr->Visible = (val != 0);
  }

  return(TCL_OK);
}


static int grannySetAnimationClockCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id;
  double time;

  if (argc < 2) {
    interp->result = "usage: granny_setAnimationClock granny_obj ?time(sec)?";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  /* always return old value */
  sprintf(interp->result, "%f", g->clock_override);
  
  if (argc > 2) {
    if (Tcl_GetDouble(interp, argv[2], &time) != TCL_OK) return TCL_ERROR;
    g->clock_override = time;
  }

  return(TCL_OK);
}


static int grannySetControlClockCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id, animation=0;
  double time;

  if (argc < 2) {
    interp->result = "usage: granny_setControlClock granny_obj animation_control ?time(sec)?";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  /* always return old value */
  sprintf(interp->result, "%f", g->clock_override);
  
  if (Tcl_GetInt(interp, argv[2], &animation) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &time) != TCL_OK) return TCL_ERROR;

  if (animation >= g->nanimations) {
    Tcl_AppendResult(interp, argv[0], ": animation id out of range", NULL);
    return TCL_ERROR;
  }

  if (g->animations && &g->animations[animation] && g->animations[animation].control) {
  	g->animations[animation].clock_override = time;
  }

  return(TCL_OK);
}


static int grannySetMeshMorphIndexCmd(ClientData clientData,
				      Tcl_Interp *interp,
				      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  mesh *meshPtr;
  int val, count;


  if (argc < 3) {
    interp->result = "usage: granny_setMeshMorphIndex granny_obj name ?val?";
    return TCL_ERROR;
  }

  if (find_granny_mesh(interp, olist, argv[0], argv[1], argv[2], 
		       &meshPtr) != TCL_OK) 
    return TCL_ERROR;

  /* always return old value */
  sprintf(interp->result, "%d", meshPtr->Visible);

  if (argc > 3) {
    if (Tcl_GetInt(interp, argv[3], &val) != TCL_OK) return TCL_ERROR;
    count = GrannyGetMeshMorphTargetCount(meshPtr->GrannyMesh);
    if (val >= count) meshPtr->MorphIndex = 0;
    else meshPtr->MorphIndex = val;
  }
  
  return(TCL_OK);
}

static int grannySetMeshGroupVisibleCmd(ClientData clientData, Tcl_Interp *interp,
					int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  mesh *meshPtr;
  int val, i;
  int groupCount;
  char *name;
  granny_tri_material_group *group;

  if (argc < 4) {
    interp->result = "usage: granny_setMeshGroupVisible granny_obj name group ?val?";
    return TCL_ERROR;
  }

  if (find_granny_mesh(interp, olist, argv[0], argv[1], argv[2], 
		       &meshPtr) != TCL_OK) 
    return TCL_ERROR;


  groupCount = GrannyGetMeshTriangleGroupCount(meshPtr->GrannyMesh);

  if (Tcl_GetInt(interp, argv[3], &i) == TCL_OK) {
    if (i < 0 || i >= groupCount) {
      Tcl_AppendResult(interp, argv[0], ": mesh group index out of range", 
		       NULL);
      return TCL_ERROR;
    }
  }
  else {
    Tcl_ResetResult(interp);
    group = GrannyGetMeshTriangleGroups(meshPtr->GrannyMesh);

    for (i = 0; i < groupCount; i++) {
      name = 
	(char *) meshPtr->GrannyMesh->MaterialBindings[group[i].MaterialIndex].Material->Name;
      if (!strcmp(name, argv[3])) break;
    }
    if (i == groupCount) {
      Tcl_AppendResult(interp, argv[0], ": mesh group \"", argv[3], "\" not found", 
		       NULL);
      return TCL_ERROR;
    }
  }

  /* always return old value */
  sprintf(interp->result, "%d", meshPtr->TriGroupsVisible[i]);

  if (argc > 4) {
    if (Tcl_GetInt(interp, argv[4], &val) != TCL_OK) return TCL_ERROR;
    meshPtr->TriGroupsVisible[i] = (val != 0);
  }
  
  return(TCL_OK);
}


static int grannyGetMeshGroupNames(ClientData clientData, 
				   Tcl_Interp *interp,
				   int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  mesh *meshPtr;
  int i;
  int groupCount;
  char *name;
  granny_tri_material_group *group;

  Tcl_DString materialList;

  if (argc < 3) {
    interp->result = "usage: granny_getMeshGroupNames granny_obj names";
    return TCL_ERROR;
  }

  if (find_granny_mesh(interp, olist, argv[0], argv[1], argv[2], 
		       &meshPtr) != TCL_OK) 
    return TCL_ERROR;


  Tcl_DStringInit(&materialList);
  groupCount = GrannyGetMeshTriangleGroupCount(meshPtr->GrannyMesh);
  group = GrannyGetMeshTriangleGroups(meshPtr->GrannyMesh);

  for (i = 0; i < groupCount; i++) {
    name = 
      (char *) meshPtr->GrannyMesh->MaterialBindings[group[i].MaterialIndex].Material->Name;
    Tcl_DStringAppendElement(&materialList, name);
  }
  Tcl_DStringResult(interp, &materialList);
  
  return(TCL_OK);
}


static int grannyGetMeshVertices(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  mesh *meshPtr;
  int VertexCount, VertexBufferSize;
  float *VertexBufferPointer;
  DYN_LIST *dl;

  if (argc < 3) {
    interp->result = "usage: granny_getMeshVertices granny_obj name ?deform?";
    return TCL_ERROR;
  }

  if (find_granny_mesh(interp, olist, argv[0], argv[1], argv[2], 
		       &meshPtr) != TCL_OK) 
    return TCL_ERROR;
  
  VertexCount = GrannyGetMeshVertexCount(meshPtr->GrannyMesh);
  VertexBufferSize = VertexCount * sizeof(granny_p3_vertex);
  VertexBufferPointer = (float *) malloc(VertexBufferSize);
  GrannyCopyMeshVertices(meshPtr->GrannyMesh, 
			 GrannyP3VertexType, 
			 VertexBufferPointer);
  dl = dfuCreateDynListWithVals(DF_FLOAT, VertexCount*3, 
				VertexBufferPointer);
  return tclPutList(interp, dl);
}

static int grannySetColorMaterialCmd(ClientData clientData, Tcl_Interp *interp,
				     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id;
  int val;

  if (argc < 2) {
    interp->result = "usage: granny_setColorMaterial granny_obj ?val?";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &val) != TCL_OK) return TCL_ERROR;
  g->color_material = (val != 0);
  
  return(TCL_OK);
}

static int grannyShowCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  char buf[64];
  double scale = 10.0;
  if (argc < 2) {
    interp->result = "usage: granny_show filename ?scale?";
    return TCL_ERROR;
  }

  if (argc > 2) {
    if (Tcl_GetDouble(interp, argv[2], &scale) != TCL_OK)
      return TCL_ERROR;
  }

  Tcl_VarEval(interp, "load light; glistInit 1; resetObjList", NULL);
  Tcl_VarEval(interp, "glistAddObject [light enable] 0", NULL);
  Tcl_VarEval(interp, "glistAddObject [light on] 0", NULL);
  if (Tcl_VarEval(interp, "set gm [granny_model ", argv[1], "]", NULL) 
      != TCL_OK) return TCL_ERROR;

  sprintf(buf, "scaleObj $gm %f", scale);
  Tcl_VarEval(interp, buf, NULL);

  if (Tcl_VarEval(interp, "set anim [granny_addAnimation $gm ", argv[1], "]", 
		  NULL) 
      != TCL_OK) return TCL_ERROR;
  Tcl_VarEval(interp, buf, NULL);

  if (Tcl_VarEval(interp, "if { $anim >= 0 } { granny_playAnimation $gm $anim 0 1. 0 }", NULL) 
      != TCL_OK) return TCL_ERROR;
  Tcl_VarEval(interp, buf, NULL);

  Tcl_VarEval(interp, "glistAddObject $gm 0; unset gm; unset anim", NULL);
  Tcl_VarEval(interp, "glistSetDynamic 0 1", NULL);
  Tcl_VarEval(interp, "glistSetVisible 1; glistSetCurGroup 0; redraw", NULL);
  return TCL_OK;
}


static int grannySetBendCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id;
  int val;
  double dval;

  if (argc < 4) {
    interp->result = "usage: granny_setBend granny_obj val boneindex";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));
  
  if (Tcl_GetDouble(interp, argv[2], &dval) != TCL_OK) return TCL_ERROR;
  g->bend = dval;

  if (Tcl_GetInt(interp, argv[3], &val) != TCL_OK) return TCL_ERROR;
  g->bone_to_bend = val;
  
  return(TCL_OK);
}

static int grannySetDotfieldCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id;
  int val;
  int val2;
  double exX = 1.0;
  double exY = 1.0;
  double dX = 0.0;
  double dY = 0.0;
  GLint viewport[4];

  glGetIntegerv(GL_VIEWPORT, viewport);
  if (argc < 3 || argc == 5) {
    interp->result = "usage: granny_setDotfield granny_obj dots_to_use(0 for normal drawing) ?frames_to_persist ?extentX extentY ?dX dY???";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));
  
  if (Tcl_GetInt(interp, argv[2], &val) != TCL_OK) return TCL_ERROR;
  if (argc > 3) {
  	if (Tcl_GetInt(interp, argv[3], &val2) != TCL_OK) return TCL_ERROR;
  	if (argc > 5)
  	{
      if (Tcl_GetDouble(interp, argv[4], &exX) != TCL_OK) return TCL_ERROR;
      if (Tcl_GetDouble(interp, argv[5], &exY) != TCL_OK) return TCL_ERROR;
      if (argc > 7)
      {
	  	if (Tcl_GetDouble(interp, argv[6], &dX) != TCL_OK) return TCL_ERROR;
	  	if (Tcl_GetDouble(interp, argv[7], &dY) != TCL_OK) return TCL_ERROR;
	  	if (exX/2 + abs(dX) > 1.0 || exY + abs(dY) > 1.0)
	  	{
		  Tcl_AppendResult(interp, argv[0], ": extent and d parameters would place dots offscreen", NULL);
		  return TCL_ERROR;
		}
	  }
	}
  } else
  {
    val2 = 2;
  }
  if (val == 0)
  {
    if (g->dots) {
	  dotfieldDelete(g->dots);
	  g->dots = 0;
	}
  } else if (val > 0)
  {
    if (g->dots) {
	  dotfieldDelete(g->dots);
	}
	g->dots = allocDotfield(val, val2);
	g->dots->drawingmode = 1;
	g->dots->extentX = (float)exX;
	g->dots->extentY = (float)exY;
	g->dots->dX = (float)dX;
	g->dots->dY = (float)dY;
//JED
/*fprintf(getConsoleFP(), "screenbuffer has %d GLubytes in it.\n\n", ((int) ((viewport[2] - viewport[0]) * g->dots->extentX)) *
	  ((int) ((viewport[3] - viewport[1]) * g->dots->extentY)) * 4);*/
	g->dots->screenbuffer = (GLubyte *) calloc(((int) ((viewport[2] - viewport[0]) * g->dots->extentX)) *
	  ((int) ((viewport[3] - viewport[1]) * g->dots->extentY)) * 4, sizeof(GLubyte));
  }
  if (val == 0)
  {
    if (g->backdots) {
	  dotfieldDelete(g->backdots);
	  g->backdots = 0;
	}
  } else if (val > 0)
  {
    if (g->backdots) {
	  dotfieldDelete(g->backdots);
	}
	g->backdots = allocDotfield(val, val2);
	g->backdots->drawingmode = 1;
	g->backdots->extentX = (float)exX;
	g->backdots->extentY = (float)exY;
	g->backdots->dX = (float)dX;
	g->backdots->dY = (float)dY;
  }
  return(TCL_OK);
}

static int grannySetDotForeColorCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  double red, green, blue, alpha = 1.0;
  int id;

  if (argc < 5) {
    interp->result = "usage: granny_setDotForeColor objid r g b ?alpha?";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &red) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &green) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &blue) != TCL_OK) return TCL_ERROR;
  if (argc > 5) {
    if (Tcl_GetDouble(interp, argv[5], &alpha) != TCL_OK) return TCL_ERROR;
  }

  g->dots->color[0] = (GLfloat) red;
  g->dots->color[1] = (GLfloat) green;
  g->dots->color[2] = (GLfloat) blue;
  g->dots->color[3] = (GLfloat) alpha;

  return(TCL_OK);
}

static int grannySetDotBackColorCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  double red, green, blue, alpha = 1.0;
  int id;

  if (argc < 5) {
    interp->result = "usage: granny_setDotBackColor objid r g b ?alpha?";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &red) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &green) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &blue) != TCL_OK) return TCL_ERROR;
  if (argc > 5) {
    if (Tcl_GetDouble(interp, argv[5], &alpha) != TCL_OK) return TCL_ERROR;
  }

  g->backdots->color[0] = (GLfloat) red;
  g->backdots->color[1] = (GLfloat) green;
  g->backdots->color[2] = (GLfloat) blue;
  g->backdots->color[3] = (GLfloat) alpha;

  return(TCL_OK);
}

static int grannySetDotsizeCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  double dotsize;
  int id;

  if (argc < 3) {
    interp->result = "usage: granny_setDotsize objid dotsize";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &dotsize) != TCL_OK) return TCL_ERROR;

  g->dots->dotsize = (GLfloat) dotsize;
  g->backdots->dotsize = (GLfloat) dotsize;

  return(TCL_OK);
}

static int grannyResetDotsCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id;

  if (argc != 2) {
    interp->result = "usage: granny_resetDots objid";
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  g->dots->frameind = -1;
  g->backdots->frameind = -1;

  return(TCL_OK);
}


static int grannyGetDotsCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GRANNY_MODEL *g;
  int id, dotind, frontcount;
  float *dotverts;
  DYN_LIST *dl;
  int frontdots = 1;
  int backdots = 1;
  int dotcount;

  if (argc < 2) {
    interp->result =
      "usage: granny_getDots granny_object ?frontdots? ?backdots?";
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a granny object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != GrannyID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type granny", NULL);
    return TCL_ERROR;
  }
  g = (GRANNY_MODEL *) GR_CLIENTDATA(OL_OBJ(olist,id));

  if (argc > 2) {
    if (Tcl_GetInt(interp, argv[2], &frontdots) != TCL_OK) return TCL_ERROR;
  }
  if (argc > 3) {
  	if (Tcl_GetInt(interp, argv[3], &backdots) != TCL_OK) return TCL_ERROR;
  }

//this depends on float and GLfloat being the same... Probably not great.
//it also returns non-drawn vertices at the expense of later drawn ones... definitely not great.
/*  if (frontdots) dotcount += g->dots->drawcount;
  if (backdots) dotcount += g->backdots->drawcount;
  dotverts = (float *) malloc(2*dotcount*sizeof(float));
  if (frontdots) memcpy(dotverts, g->dots->vertices, sizeof(float)*2*g->dots->drawcount);
  if (backdots) memcpy(dotverts + frontdots*2*g->dots->drawcount,
    g->backdots->vertices, sizeof(float)*2*g->backdots->drawcount);
  dl = dfuCreateDynListWithVals(DF_FLOAT, 2*dotcount, dotverts);
  */
//This is probably slower, but at least works properly:
  frontcount = frontdots * g->dots->drawcount;
  dotcount = frontcount;
  if (backdots) dotcount += g->backdots->drawcount;
  dotverts = (float *) malloc(2*dotcount*sizeof(float));
  for (dotind = 0; dotind < frontcount; dotind++)
  {
    dotverts[dotind*2] = (float) g->dots->vertices[g->dots->drawlist[dotind]*3];
    dotverts[dotind*2+1] = (float) g->dots->vertices[g->dots->drawlist[dotind]*3+1];
  }
  for (dotind = 0; dotind < backdots*g->backdots->drawcount; dotind++)
  {
    dotverts[dotind*2 + frontcount*2] = (float) g->backdots->vertices[g->backdots->drawlist[dotind]*3];
    dotverts[dotind*2+1 + frontcount*2] = (float) g->backdots->vertices[g->backdots->drawlist[dotind]*3+1];
  }
  dl = dfuCreateDynListWithVals(DF_FLOAT, 2*dotcount, dotverts);

  return tclPutList(interp, dl);
}

static void ConstructQuaternion4(granny_real32 *Quaternion,
				 granny_real32 const *Axis,
				 granny_real32 const Angle)
{
  granny_real32 const HalfSin = (granny_real32)sin(Angle * 0.5f);
  granny_real32 const HalfCos = (granny_real32)cos(Angle * 0.5f);
  Quaternion[0] = Axis[0] * HalfSin;
  Quaternion[1] = Axis[1] * HalfSin;
  Quaternion[2] = Axis[2] * HalfSin;
  Quaternion[3] = HalfCos;
}


static void
GrannyError(granny_log_message_type Type,
            granny_log_message_origin Origin,
	    char const* SourceFile, granny_int32x SourceLine,
	    char const *Message,
            void *UserData)
{
  /* Don't worry about granny file fixups */
  if (Origin==GrannyFileReadingLogMessage) return;

  
  fprintf(getConsoleFP(), "GRANNY [%s:%d]: \"%s\" \n", 
	  SourceFile, SourceLine, Message);
}


EXPORT(int,Grannystim_Init) _ANSI_ARGS_((Tcl_Interp *interp))
{
  OBJ_LIST *OBJList = getOBJList();
  granny_log_callback Callback;

  if (
#ifdef USE_TCL_STUBS
      Tcl_InitStubs(interp, "8.5", 0)
#else
      Tcl_PkgRequire(interp, "Tcl", "8.5", 0)
#endif
      == NULL) {
    return TCL_ERROR;
  }
  
  if(!GrannyVersionsMatch) {
    //    Tcl_AppendResult(interp, "grannystim: granny version error", NULL);
    //    return TCL_ERROR;
  }

  Callback.Function = GrannyError;
  Callback.UserData = 0;
  GrannySetLogCallback(&Callback);

  if (GrannyID < 0) {
    GrannyID = gobjRegisterType();
  }

  // Turn off logging of file reading messages
  GrannyFilterMessage(GrannyFileReadingLogMessage, 1);

  Tcl_CreateCommand(interp, "granny_model", 
		    (Tcl_CmdProc *) grannyModelCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_copyModel", 
		    (Tcl_CmdProc *) grannyCopyModelCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "granny_replaceTexture", 
		    (Tcl_CmdProc *) grannyReplaceTextureCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "granny_setColor",
		    (Tcl_CmdProc *) grannySetColorCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_setColorMaterial", 
		    (Tcl_CmdProc *) grannySetColorMaterialCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_setBend", 
		    (Tcl_CmdProc *) grannySetBendCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  
  Tcl_CreateCommand(interp, "granny_setDotfield",
  			(Tcl_CmdProc *) grannySetDotfieldCmd,
  			(ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_setDotForeColor",
		    (Tcl_CmdProc *) grannySetDotForeColorCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_setDotBackColor",
		    (Tcl_CmdProc *) grannySetDotBackColorCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_setDotsize",
		    (Tcl_CmdProc *) grannySetDotsizeCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_getDots",
		    (Tcl_CmdProc *) grannyGetDotsCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_resetDots",
		    (Tcl_CmdProc *) grannyResetDotsCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "granny_addAnimation", 
		    (Tcl_CmdProc *) grannyAddAnimationCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_playAnimation", 
		    (Tcl_CmdProc *) grannyPlayAnimationCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_completeAnimation", 
		    (Tcl_CmdProc *) grannyCompleteAnimationCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_easeAnimationIn", 
		    (Tcl_CmdProc *) grannyEaseControlInCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_easeAnimationOut",
		    (Tcl_CmdProc *) grannyEaseControlOutCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_setAnimationClock",
		    (Tcl_CmdProc *) grannySetAnimationClockCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_setControlClock",
		    (Tcl_CmdProc *) grannySetControlClockCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_setControlWeight",
		    (Tcl_CmdProc *) grannySetControlWeightCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "granny_setMeshVisible", 
		    (Tcl_CmdProc *) grannySetMeshVisibleCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_getMeshVertices",
		    (Tcl_CmdProc *) grannyGetMeshVertices, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "granny_setMeshGroupVisible", 
		    (Tcl_CmdProc *) grannySetMeshGroupVisibleCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_getMeshGroupNames", 
		    (Tcl_CmdProc *) grannyGetMeshGroupNames, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "granny_getMeshMorphCounts", 
		    (Tcl_CmdProc *) grannyGetMeshMorphCounts, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_setMeshMorphIndex", 
		    (Tcl_CmdProc *) grannySetMeshMorphIndexCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);


  Tcl_CreateCommand(interp, "granny_getVertices",
		    (Tcl_CmdProc *) grannyGetVertices, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_getIndices",
		    (Tcl_CmdProc *) grannyGetIndices, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "granny_getBounds", 
		    (Tcl_CmdProc *) grannyGetBounds, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "granny_getMeshNames", 
		    (Tcl_CmdProc *) grannyGetMeshNames, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "granny_getBoneNames", 
		    (Tcl_CmdProc *) grannyGetBoneNames, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "granny_allocated", 
		    (Tcl_CmdProc *) grannyAllocatedCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "granny_show", 
		    (Tcl_CmdProc *) grannyShowCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  OurInterp = interp;

  return TCL_OK;
}
