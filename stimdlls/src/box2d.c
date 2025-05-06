/*
 * box2d.c
 *  Use the Box2D (v3) library to simulate 2D physics
 */

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif
#include <tcl.h>
#include <df.h>
#include <dfana.h>
#include <tcl_dl.h>

#include <stim2.h>
#include "box2d/box2d.h"

static Tcl_Interp *OurInterp = NULL;
static int Box2DID = -1;	/* unique Box2D object id */

struct Box2D_world;		/* for circular reference */


/* limit the number of shapes inside a body */
#define MAX_SHAPES_PER_BODY 16

/*
enum b2BodyType
 {
     b2_staticBody = 0,
     b2_kinematicBody,
     b2_dynamicBody
 };
*/

typedef struct Box2D_world {
  char name[32];
  Tcl_Interp *interp;
  b2WorldId worldId;
  b2Vec2 gravity;
  
  /* store contact events after a simulation step */
  b2ContactEvents contactEvents;

  int bodyCount;
  Tcl_HashTable bodyTable;

  int jointCount;
  Tcl_HashTable jointTable;

  int subStepCount;
  
  int time;
  int lasttime;
} BOX2D_WORLD;

typedef struct Box2D_userdata {
  BOX2D_WORLD *world;
  char name[32];
  OBJ_LIST *olist;
  bool linked;
  int linkid;
  float *matrix;
  float gravity;
  float force_vector[3];
  float torque_vector[3];
} BOX2D_USERDATA;


static void Box2D_free_userdata (b2BodyId body);
static void Box2D_update_link (b2BodyId body, float x, float y, float angle);

/***********************************************************************/
/**********************      Helper Functions     **********************/
/***********************************************************************/

static int find_matrix4(Tcl_Interp *interp, char *name, float *m);
static int find_vec_3(Tcl_Interp *interp, char *name, float *v);
static int find_vec_4(Tcl_Interp *interp, char *name, float *v);

static void matrix4_rotation_from_angle_axis(float *mat, float x, float y,
					     float z, float theta);
static void matrix4_identity(float *mat);
static void matrix4_set_translation(float *mat, float x, float y, float z);
static void matrix4_add_translation(float *mat, float x, float y, float z);
static void matrix4_get_translation(float *mat, 
				    float *x, float *y, float *z);
static void matrix4_set_scale(float *mat, float x, float y, float z);
static void matrix4_set_translation_angle(float *mat, float x, float y,
					  float angle);



enum TRANS_TYPE { TRANS_ADD, TRANS_SET } ;

static BOX2D_WORLD *find_world(Tcl_Interp *interp, 
				 OBJ_LIST *olist, char *idstring);
static int find_body(BOX2D_WORLD *Box2DObj, 
		     char *name, b2BodyId *b);

static BOX2D_WORLD *find_Box2D(Tcl_Interp *interp, 
				OBJ_LIST *olist,
			       char *idstring)
{
  int id;
  
  if (Tcl_GetInt(interp, idstring, &id) != TCL_OK) return NULL;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, "objid out of range", NULL);
    return NULL;
  }
  
  /* Make sure it's a Box2D object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != Box2DID) {
    Tcl_AppendResult(interp, "object not a Box2D world", NULL);
    return NULL;
  }
  return (BOX2D_WORLD *) GR_CLIENTDATA(OL_OBJ(olist,id));
}

static int find_vec_3(Tcl_Interp *interp, char *name, float *m)
{
  DYN_LIST *dl;
  if (tclFindDynList(interp, name, &dl) != TCL_OK) {
    return TCL_ERROR;
  }
  if (DYN_LIST_DATATYPE(dl) == DF_FLOAT && 
      DYN_LIST_N(dl) == 3) {
    if (m) memcpy(m, (float *) DYN_LIST_VALS(dl), sizeof(float)*3);
    return TCL_OK;
  }
  else {
      Tcl_AppendResult(interp, "\"", name, "\" not a valid vec3", 
		       (char *) NULL);
      return TCL_ERROR;
  }
}

static int find_vec_4(Tcl_Interp *interp, char *name, float *m)
{
  DYN_LIST *dl;
  if (tclFindDynList(interp, name, &dl) != TCL_OK) {
    return TCL_ERROR;
  }
  if (DYN_LIST_DATATYPE(dl) == DF_FLOAT && 
      DYN_LIST_N(dl) == 4) {
    if (m) memcpy(m, (float *) DYN_LIST_VALS(dl), sizeof(float)*4);
    return TCL_OK;
  }
  else {
      Tcl_AppendResult(interp, "\"", name, "\" not a valid vec4", 
		       (char *) NULL);
      return TCL_ERROR;
  }
}

static int find_matrix4(Tcl_Interp *interp, char *name, float *m)
{
  DYN_LIST *dl;
  if (tclFindDynList(interp, name, &dl) != TCL_OK) {
    return TCL_ERROR;
  }
  if (DYN_LIST_DATATYPE(dl) == DF_FLOAT && 
      DYN_LIST_N(dl) == 16) {
    if (m) memcpy(m, (float *) DYN_LIST_VALS(dl), sizeof(float)*16);
    return TCL_OK;
  }
  else {
      Tcl_AppendResult(interp, "\"", name, "\" not a valid matrix", 
		       (char *) NULL);
      return TCL_ERROR;
  }
}


static int find_body(BOX2D_WORLD *bw, char *name, b2BodyId *b)
{
  b2BodyId *body;
  Tcl_HashEntry *entryPtr;
  
  if ((entryPtr = Tcl_FindHashEntry(&bw->bodyTable, name))) {
    body = (b2BodyId *) Tcl_GetHashValue(entryPtr);
    if (!body) {
      Tcl_AppendResult(bw->interp,
		       "bad body ptr in hash table", NULL);
      return TCL_ERROR;
    }
    if (b) *b = *body;
    return TCL_OK;
  }
  else {
    if (b) {
      Tcl_AppendResult(bw->interp, "body \"", name, "\" not found", 
		       (char *) NULL);
    }
    return TCL_ERROR;
  }
}


static int find_joint(BOX2D_WORLD *bw, char *name, b2JointId *j)
{
  b2JointId *joint;
  Tcl_HashEntry *entryPtr;
  
  if ((entryPtr = Tcl_FindHashEntry(&bw->jointTable, name))) {
    joint = (b2JointId *) Tcl_GetHashValue(entryPtr);
    if (!joint) {
      Tcl_AppendResult(bw->interp,
		       "bad joint ptr in hash table", NULL);
      return TCL_ERROR;
    }
    if (j) *j = *joint;
    return TCL_OK;
  }
  else {
    if (j) {
      Tcl_AppendResult(bw->interp, "joint \"", name, "\" not found", 
		       (char *) NULL);
    }
    return TCL_ERROR;
  }
}




static int find_revolute_joint(BOX2D_WORLD *bw, char *name, b2JointId *j)
{
  if (find_joint(bw, name, j) != TCL_OK) return TCL_ERROR;
  if (b2Joint_GetType(*j) != b2_revoluteJoint) {
    Tcl_AppendResult(bw->interp,
		     "joint ", name, " not a revolute joint", NULL);
    return TCL_ERROR;
  }
  return TCL_OK;
}



/***********************************************************************/
/***********************      Box2D OBJ Funcs     **********************/
/***********************************************************************/

static int Box2DUpdate(GR_OBJ *g)
{
  BOX2D_WORLD *bw = (BOX2D_WORLD *) GR_CLIENTDATA(g);
  b2WorldId bWorld = bw->worldId;
  float elapsed;
  b2BodyId *body;

  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch search;

  b2Vec2 position;
  float angle;
  
  bw->time = getStimTime();
  //  elapsed = (nw->time-nw->lasttime)/1000.;
  elapsed = getFrameDuration()/1000.;
  bw->lasttime = bw->time;


  b2World_Step(bw->worldId, elapsed, bw->subStepCount);
  bw->contactEvents = b2World_GetContactEvents(bw->worldId);

  /* iterate over the table of bodies and update matrices */
  for (entryPtr = Tcl_FirstHashEntry(&bw->bodyTable, &search);
       entryPtr != NULL;
       entryPtr = Tcl_NextHashEntry(&search)) {
    body = (b2BodyId *) Tcl_GetHashValue(entryPtr);
    position = b2Body_GetPosition(*body);
    angle = b2Rot_GetAngle(b2Body_GetRotation(*body));
    Box2D_update_link (*body, position.x, position.y, angle);
  }
  
  return(TCL_OK);
}

static void Box2DDelete(GR_OBJ *g) 
{
  BOX2D_WORLD *bw = (BOX2D_WORLD *) GR_CLIENTDATA(g);

  b2BodyId *body;
  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch search;

  /* iterate over the table of bodies and free userdata */
  for (entryPtr = Tcl_FirstHashEntry(&bw->bodyTable, &search);
       entryPtr != NULL;
       entryPtr = Tcl_NextHashEntry(&search)) {
    body = (b2BodyId *) Tcl_GetHashValue(entryPtr);
    Box2D_free_userdata(*body);
  }

  /* free hash table */
  Tcl_DeleteHashTable(&bw->bodyTable);

  b2DestroyWorld(bw->worldId);
  free((void *) bw);
}

static int Box2DReset(GR_OBJ *g)
{
  BOX2D_WORLD *bw = (BOX2D_WORLD *) GR_CLIENTDATA(g);

  // Need to reset all the body positions to original posision here
  
  bw->lasttime = bw->time = 0;
  return(TCL_OK);
}

static int Box2DCmd(ClientData clientData, Tcl_Interp *interp,
		    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *obj;
  static const char *name = "Box2D";
  static char worldname[128];
  BOX2D_WORLD *bw;
  
  obj = gobjCreateObj();
  if (!obj) return -1;

  GR_OBJTYPE(obj) = Box2DID;
  strcpy(GR_NAME(obj), name);

  bw = (BOX2D_WORLD *) calloc(1, sizeof(BOX2D_WORLD));

  bw->gravity.x = 0.0f;
  bw->gravity.y = -10.0f;

  /* Reasonable simulation settings */
  bw->subStepCount = 4;

  b2Vec2 gravity = {bw->gravity.x, bw->gravity.y};
  b2WorldDef worldDef = b2DefaultWorldDef();
  worldDef.gravity = gravity;
  b2WorldId worldId = b2CreateWorld(&worldDef);
  
  bw->worldId = worldId;

  bw->interp = interp;

  Tcl_InitHashTable(&bw->bodyTable, TCL_STRING_KEYS);
  Tcl_InitHashTable(&bw->jointTable, TCL_STRING_KEYS);
  
  GR_CLIENTDATA(obj) = bw;
  GR_DELETEFUNCP(obj) = Box2DDelete;
  GR_RESETFUNCP(obj) = (RESET_FUNC) Box2DReset;
  GR_UPDATEFUNCP(obj) = (UPDATE_FUNC) Box2DUpdate;

  int gid = gobjAddObj(olist, obj);
  snprintf(bw->name, sizeof(bw->name), "%d", gid);

  Tcl_SetObjResult(interp, Tcl_NewIntObj(gid));
  
  return(TCL_OK);
}

/***********************************************************************/
/**********************      Tcl Bound Funcs     ***********************/
/***********************************************************************/

static int Box2DGetBodiesCmd(ClientData clientData, Tcl_Interp *interp,
                             int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;
  Tcl_HashEntry *entryPtr;
  Tcl_Obj *bodylist;
  Tcl_HashSearch search;
  int typemask = 0x7; // all three types
  b2BodyId *body;

  if (argc < 2)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world [typemask]", (char *)NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;

  bodylist = Tcl_NewObj();
  
  for (entryPtr = Tcl_FirstHashEntry(&bw->bodyTable, &search);
       entryPtr != NULL;
       entryPtr = Tcl_NextHashEntry(&search)) {
    body = (b2BodyId *) Tcl_GetHashValue(entryPtr);
    if ((1 << (int) b2Body_GetType(*body)) & typemask) {
      Tcl_ListObjAppendElement(interp, bodylist,
			       Tcl_NewStringObj((char *) Tcl_GetHashKey(&bw->bodyTable, entryPtr), -1));
    }
  }
  Tcl_SetObjResult(interp, bodylist);
  return TCL_OK;
}

static int Box2DUpdateCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;
  double elapsed;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world elapsed", (char *) NULL);
    return TCL_ERROR;
  }
  
  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &elapsed) != TCL_OK) return TCL_ERROR;

  bw->lasttime = bw->time;
  bw->time += (int) (elapsed*1000);

  b2World_Step(bw->worldId, elapsed, bw->subStepCount);

  // Need to update linked objects here
  
  return(TCL_OK);

}



static int Box2DGetContactBeginEventCountCmd(ClientData clientData, Tcl_Interp *interp,
            int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world", NULL);
    return TCL_ERROR;
  }
  
  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;
  Tcl_SetObjResult(interp, Tcl_NewIntObj(bw->contactEvents.beginCount));
  return(TCL_OK);
}


static int Box2DGetContactEndEventCountCmd(ClientData clientData, Tcl_Interp *interp,
            int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world", NULL);
    return TCL_ERROR;
  }
  
  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;
  Tcl_SetObjResult(interp, Tcl_NewIntObj(bw->contactEvents.endCount));
  return(TCL_OK);
}

static int Box2DGetContactBeginEventsCmd(ClientData clientData, Tcl_Interp *interp,
				    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world", NULL);
    return TCL_ERROR;
  }
  
  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;

  // store lists of begin and end touches
  Tcl_Obj *events = Tcl_NewObj();

  for (int i = 0; i < bw->contactEvents.beginCount; ++i)
    {
      b2ContactBeginTouchEvent* beginEvent = bw->contactEvents.beginEvents + i;
      char *bodyName1 = 
  ((BOX2D_USERDATA *) b2Body_GetUserData(b2Shape_GetBody(beginEvent->shapeIdA)))->name;
      char *bodyName2 = 
      ((BOX2D_USERDATA *) b2Body_GetUserData(b2Shape_GetBody(beginEvent->shapeIdB)))->name;
      Tcl_Obj *bodies = Tcl_NewObj();
      Tcl_ListObjAppendElement(interp, bodies, Tcl_NewStringObj(bodyName1, -1));
      Tcl_ListObjAppendElement(interp, bodies, Tcl_NewStringObj(bodyName2, -1));
      Tcl_ListObjAppendElement(interp, events, bodies);
    }
  Tcl_SetObjResult(interp, events);
  return(TCL_OK);
}


static int Box2DGetContactEndEventsCmd(ClientData clientData, Tcl_Interp *interp,
            int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world", NULL);
    return TCL_ERROR;
  }
  
  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;

  // store lists of begin and end touches
  Tcl_Obj *events = Tcl_NewObj();

  Tcl_Obj *ends = Tcl_NewObj();
  for (int i = 0; i < bw->contactEvents.endCount; ++i)
    {
      b2ContactEndTouchEvent* endEvent = bw->contactEvents.endEvents + i;
      char *bodyName1 = 
  ((BOX2D_USERDATA *) b2Body_GetUserData(b2Shape_GetBody(endEvent->shapeIdA)))->name;
      char *bodyName2 = 
  ((BOX2D_USERDATA *) b2Body_GetUserData(b2Shape_GetBody(endEvent->shapeIdB)))->name;

      Tcl_Obj *bodies = Tcl_NewObj();
      Tcl_ListObjAppendElement(interp, bodies, Tcl_NewStringObj(bodyName1, -1));
      Tcl_ListObjAppendElement(interp, bodies, Tcl_NewStringObj(bodyName2, -1));
      Tcl_ListObjAppendElement(interp, events, bodies);
    }

  Tcl_SetObjResult(interp, events);
  return(TCL_OK);
}


static int Box2DCreateBoxCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;
  BOX2D_USERDATA *userdata;
  Tcl_HashEntry *entryPtr;
  int newentry;
  char *name;
  double x, y, width, height, angle;
  int bodyType;

  int enableContact = true;
  int enableHits = false;
  
  if (argc < 8) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " world name type x y w h [angle]", NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;


  name = argv[2];
  
  if (Tcl_GetInt(interp, argv[3], &bodyType) != TCL_OK) return TCL_ERROR;
  if (bodyType < 0 || bodyType > 2) {
    Tcl_AppendResult(interp, argv[0], ": invalid body type", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetDouble(interp, argv[4], &x) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &y) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[6], &width) != TCL_OK) return TCL_ERROR;

  if (width <= 0) {
    Tcl_AppendResult(interp, argv[0], ": invalid width", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetDouble(interp, argv[7], &height) != TCL_OK) return TCL_ERROR;

  if (height <= 0) {
    Tcl_AppendResult(interp, argv[0], ": invalid height", NULL);
    return TCL_ERROR;
  }
  
  if (argc > 8) {
    if (Tcl_GetDouble(interp, argv[8], &angle) != TCL_OK) return TCL_ERROR;
  }
  else angle = 0.0;
  
  userdata = (BOX2D_USERDATA *) calloc(1, sizeof(BOX2D_USERDATA));
  userdata->world = bw;

  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = (b2BodyType) bodyType;
  bodyDef.position = (b2Vec2){x, y};
  bodyDef.rotation = b2MakeRot(angle);
  bodyDef.angularDamping = .05;
  bodyDef.linearDamping = .05;
  
  b2BodyId bodyId = b2CreateBody(bw->worldId, &bodyDef);
  b2Body_SetUserData (bodyId, userdata);

  /* create the box shape for this body */
  b2Polygon box = b2MakeBox(width/2., height/2.);
  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.density = 1.0f;
  
  shapeDef.enableContactEvents = enableContact;
  shapeDef.enableHitEvents = enableHits;
    
  b2CreatePolygonShape(bodyId, &shapeDef, &box);

  if (!name || !strlen(name)) {
    snprintf(userdata->name, sizeof(userdata->name), "body%d", bw->bodyCount++); 
  }
  else {
    strncpy(userdata->name, name, sizeof(userdata->name));
  }

  entryPtr = Tcl_CreateHashEntry(&bw->bodyTable, userdata->name, &newentry);
  /* the body is an opaque structure, so to hash create copy and add to table */

  b2BodyId *body = (b2BodyId *) malloc(sizeof(b2BodyId));
  *body = bodyId;
  Tcl_SetHashValue(entryPtr, body);

  Tcl_SetResult(interp, userdata->name, TCL_VOLATILE);
  return(TCL_OK);
}



static int Box2DCreateCircleCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;
  BOX2D_USERDATA *userdata;
  char *name = NULL;
  Tcl_HashEntry *entryPtr;
  int newentry;
  double x, y, r;
  int bodyType;
  int enableContact = true;
  int enableHits = false;
  
  if (argc < 7) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " world name type x y r", NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;
  name = argv[2];
  
  if (Tcl_GetInt(interp, argv[3], &bodyType) != TCL_OK) return TCL_ERROR;

  if (bodyType < 0 || bodyType > 2) {
    Tcl_AppendResult(interp, argv[0], ": invalid body type", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetDouble(interp, argv[4], &x) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &y) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[6], &r) != TCL_OK) return TCL_ERROR;
  if (r <= 0) {
    Tcl_AppendResult(interp, argv[0], ": invalid radius", NULL);
    return TCL_ERROR;
  }

  
  userdata = (BOX2D_USERDATA *) calloc(1, sizeof(BOX2D_USERDATA));
  userdata->world = bw;

  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = (b2BodyType) bodyType;
  bodyDef.position = (b2Vec2){x, y};
  bodyDef.angularDamping = .05;
  bodyDef.linearDamping = .05;
  
  b2BodyId bodyId = b2CreateBody(bw->worldId, &bodyDef);
  b2Body_SetUserData (bodyId, userdata);

  /* create the box shape for this body */
  b2Circle circle;
  circle.center = (b2Vec2){0,0};
  circle.radius = r;
  
  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.density = 1.0f;

  shapeDef.enableContactEvents = enableContact;
  shapeDef.enableHitEvents = enableHits;
  
  b2CreateCircleShape(bodyId, &shapeDef, &circle);  

  if (!name || !strlen(name)) {
    snprintf(userdata->name, sizeof(userdata->name), "body%d", bw->bodyCount++); 
  }
  else {
    strncpy(userdata->name, name, sizeof(userdata->name));
  }

  entryPtr = Tcl_CreateHashEntry(&bw->bodyTable, userdata->name, &newentry);

  /* the body is an opaque structure, so to hash create copy and add to table */
  b2BodyId *body = (b2BodyId *) malloc(sizeof(b2BodyId));
  *body = bodyId;
  Tcl_SetHashValue(entryPtr, body);

  Tcl_SetResult(interp, userdata->name, TCL_VOLATILE);
  return(TCL_OK);
}

static int Box2DSetBodyTypeCmd(ClientData clientData, Tcl_Interp *interp,
                               int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;

  b2BodyId body;
  int body_type;

  if (argc < 4)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world body type",
                     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;

  if (find_body(bw, argv[2], &body) != TCL_OK)
    return TCL_ERROR;

  if (Tcl_GetInt(interp, argv[3], &body_type) != TCL_OK)
    return TCL_ERROR;
  if (body_type < 0 || body_type > 2)
  {
    Tcl_AppendResult(interp, argv[0], ": invalid body type", NULL);
    return TCL_ERROR;
  }

  b2Body_SetType(body, body_type);
  
  return TCL_OK;
}

static int Box2DGetBodyInfoCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  b2BodyId body;
  float angle;
  b2Vec2 position;
  b2Rot rotation;
  BOX2D_WORLD *bw;
  static char buf[64];

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world body",
		     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;
  if (find_body(bw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  
  position = b2Body_GetPosition(body);
  angle = b2Rot_GetAngle(b2Body_GetRotation(body));
  sprintf(buf, "%f %f %f", position.x, position.y, angle);
  Tcl_SetResult(interp, buf, TCL_STATIC);
  return TCL_OK;
}

static int Box2DSetTransformCmd(ClientData clientData, Tcl_Interp *interp,
                                int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;
  b2BodyId body;
  double x, y;
  double angle = 0;

  if (argc < 5)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world body x y [angle=0]", NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;
  if (find_body(bw, argv[2], &body) != TCL_OK)
    return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &x) != TCL_OK)
    return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &y) != TCL_OK)
    return TCL_ERROR;
  if (argc > 5)
  {
    if (Tcl_GetDouble(interp, argv[5], &angle) != TCL_OK)
      return TCL_ERROR;
  }

  
  b2Vec2 pos = {x,y};
  b2Rot rot = {angle};

  b2Body_SetTransform(body, (b2Vec2){x,y}, b2MakeRot(angle));

  return TCL_OK;
}

#if 0

static int Box2DApplyForceCmd(ClientData clientData, Tcl_Interp *interp,
                              int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;
  b2Body *body;
  double x, y;
  int wake = 1;

  if (argc < 4)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world body x y", NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;
  if (find_body(bw, argv[2], &body) != TCL_OK)
    return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &x) != TCL_OK)
    return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &y) != TCL_OK)
    return TCL_ERROR;
  body->ApplyForce(b2Vec2(x, y), body->GetWorldCenter(), wake);

  return TCL_OK;
}

static int Box2DApplyLinearImpulseCmd(ClientData clientData, Tcl_Interp *interp,
                                      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;
  b2Body *body;
  double x, y;
  int wake = 1;

  if (argc < 4)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world body x y", NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;
  if (find_body(bw, argv[2], &body) != TCL_OK)
    return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &x) != TCL_OK)
    return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &y) != TCL_OK)
    return TCL_ERROR;
  body->ApplyLinearImpulse(b2Vec2(x, y), body->GetWorldCenter(), wake);

  return TCL_OK;
}

static int Box2DSetSensorCmd(ClientData clientData, Tcl_Interp *interp,
                             int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;
  b2Body *body;
  int isSensor;

  if (argc < 3)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0],
                     " world body isSensor", NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;

  if (find_body(bw, argv[2], &body) != TCL_OK)
    return TCL_ERROR;

  if (Tcl_GetInt(interp, argv[3], &isSensor) != TCL_OK)
    return TCL_ERROR;

  for (b2Fixture *f = body->GetFixtureList(); f; f = f->GetNext())
  {
    f->SetSensor(isSensor);
  }

  return TCL_OK;
}
#endif

/***********************************************************************/
/**********************      Linking Objects      **********************/
/***********************************************************************/


static void Box2D_update_link (b2BodyId body,
			       float x, float y, float angle)
{
  BOX2D_USERDATA *userdata;


  userdata = (BOX2D_USERDATA *) b2Body_GetUserData(body);

  if (!userdata->linked) return;
  if (userdata->linkid >= OL_NOBJS(userdata->olist)) {
    return;
  }

  if (!userdata->matrix) {
    userdata->matrix = GR_MATRIX(OL_OBJ(userdata->olist,userdata->linkid));
  }
  
  matrix4_set_translation_angle(userdata->matrix, x, y, angle);
}

static void Box2D_free_userdata (b2BodyId body)
{
  BOX2D_USERDATA *userdata;
  userdata = (BOX2D_USERDATA *) b2Body_GetUserData(body);
  if (userdata) free(userdata);
}


static int Box2DLinkObjCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;
  b2BodyId body;
  int id;
  BOX2D_USERDATA *userdata;

  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world body linkobj", NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(bw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &id) != TCL_OK) return TCL_ERROR;

  userdata = (BOX2D_USERDATA *) b2Body_GetUserData(body);
  userdata->linked = true;
  userdata->linkid = id;
  userdata->olist = olist;

  if (id < OL_NOBJS(olist)) {
    userdata->matrix = GR_MATRIX(OL_OBJ(olist,id));
  }
  else {
    userdata->matrix = NULL;
  }

  return TCL_OK;
}


/***********************************************************************/
/**********************      Shape Settings       **********************/
/***********************************************************************/


static int Box2DSetRestitutionCmd(ClientData clientData, Tcl_Interp *interp,
				  int objc, Tcl_Obj * const objv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;
  b2BodyId body;
  b2ShapeId shapes[MAX_SHAPES_PER_BODY];
  double restitution;

  if (objc < 4) {
    Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]),
		     " world body restitution", NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, Tcl_GetString(objv[1]))))
    return TCL_ERROR;
  if (find_body(bw, Tcl_GetString(objv[2]), &body) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDoubleFromObj(interp, objv[3], &restitution) != TCL_OK) return TCL_ERROR;

  int nshapes = b2Body_GetShapes(body, shapes, MAX_SHAPES_PER_BODY);
  for (int i = 0; i < nshapes; i++) {
    b2Shape_SetRestitution(shapes[i], restitution);
  }
  return(TCL_OK);
}

static int Box2DSetFrictionCmd(ClientData clientData, Tcl_Interp *interp,
			       int objc, Tcl_Obj * const objv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;
  b2BodyId body;
  b2ShapeId shapes[MAX_SHAPES_PER_BODY];
  double friction;
  
  if (objc < 4) {
    Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]),
		     " world body friction", NULL);
    return TCL_ERROR;
  }
  
  if (!(bw = find_Box2D(interp, olist, Tcl_GetString(objv[1]))))
    return TCL_ERROR;
  if (find_body(bw, Tcl_GetString(objv[2]), &body) != TCL_OK)
    return TCL_ERROR;
  if (Tcl_GetDoubleFromObj(interp, objv[3], &friction) != TCL_OK)
    return TCL_ERROR;
  
  int nshapes = b2Body_GetShapes(body, shapes, MAX_SHAPES_PER_BODY);
  for (int i = 0; i < nshapes; i++) {
    b2Shape_SetFriction(shapes[i], friction);
  }
  return(TCL_OK);
}

/***********************************************************************/
/**********************          Joints           **********************/
/***********************************************************************/

static int Box2DRevoluteJointCreateCmd(ClientData clientData, Tcl_Interp *interp,
					      int objc, Tcl_Obj * const objv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  
  BOX2D_WORLD *bw;
  b2BodyId bodyA;
  b2BodyId bodyB;

  char joint_name[32];
  Tcl_HashEntry *entryPtr;
  int newentry;
  
  if (objc < 4) {
    Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]),
		     " world bodyA bodyB", NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, Tcl_GetString(objv[1])))) return TCL_ERROR;
  if (find_body(bw, Tcl_GetString(objv[2]), &bodyA) != TCL_OK) return TCL_ERROR;
  if (find_body(bw, Tcl_GetString(objv[3]), &bodyB) != TCL_OK) return TCL_ERROR;

  /* check this, was get GetWorldCenter for 2.4 */
  b2Vec2 pivot = b2Body_GetWorldCenterOfMass(bodyA);
  b2RevoluteJointDef jointDef = b2DefaultRevoluteJointDef();
  jointDef.bodyIdA = bodyA;
  jointDef.bodyIdB = bodyB;
  jointDef.localAnchorA = b2Body_GetLocalPoint(jointDef.bodyIdA, pivot);
  jointDef.localAnchorB = b2Body_GetLocalPoint(jointDef.bodyIdB, pivot);
  jointDef.collideConnected = false;

  b2JointId jointID = b2CreateRevoluteJoint(bw->worldId, &jointDef);
  snprintf(joint_name, sizeof(joint_name), "joint%d", bw->jointCount++); 
  entryPtr = Tcl_CreateHashEntry(&bw->jointTable, joint_name, &newentry);

  /* the joint is an opaque structure, so to hash create copy and add to table */
  b2JointId *joint = (b2JointId *) malloc(sizeof(b2JointId));
  *joint = jointID;
  Tcl_SetHashValue(entryPtr, joint);
  
  Tcl_SetResult(interp, joint_name, TCL_VOLATILE);
  return(TCL_OK);
}


/// Enable/disable the revolute joint spring
// B2_API void b2RevoluteJoint_EnableSpring( b2JointId jointId, bool enableSpring );

static int Box2DRevoluteJoint_EnableSpringCmd(ClientData clientData, Tcl_Interp *interp,
					      int objc, Tcl_Obj * const objv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int enable;
  BOX2D_WORLD *bw;
  b2JointId joint;

  if (objc < 4) {
    Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]), " world joint enable?",
		     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, Tcl_GetString(objv[1])))) return TCL_ERROR;
  if (find_revolute_joint(bw, Tcl_GetString(objv[2]), &joint) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetIntFromObj(interp, objv[3], &enable) != TCL_OK) return TCL_ERROR;

  b2RevoluteJoint_EnableSpring(joint, enable);
  return TCL_OK;
}

/// Enable/disable the revolute joint limit
// B2_API void b2RevoluteJoint_EnableLimit( b2JointId jointId, bool enableLimit );
static int Box2DRevoluteJoint_EnableLimitCmd(ClientData clientData, Tcl_Interp *interp,
					     int objc, Tcl_Obj * const objv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int enable;
  BOX2D_WORLD *bw;
  b2JointId joint;

  if (objc < 4) {
    Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]), " world joint enable?",
		     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, Tcl_GetString(objv[1])))) return TCL_ERROR;
  if (find_revolute_joint(bw, Tcl_GetString(objv[2]), &joint) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetIntFromObj(interp, objv[3], &enable) != TCL_OK) return TCL_ERROR;

  b2RevoluteJoint_EnableLimit(joint, enable);
  return TCL_OK;
}

/// Enable/disable a revolute joint motor
// B2_API void b2RevoluteJoint_EnableMotor( b2JointId jointId, bool enableMotor );
static int Box2DRevoluteJoint_EnableMotorCmd(ClientData clientData, Tcl_Interp *interp,
					     int objc, Tcl_Obj * const objv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int enable;
  BOX2D_WORLD *bw;
  b2JointId joint;

  if (objc < 4) {
    Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]), " world joint enable?",
		     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, Tcl_GetString(objv[1])))) return TCL_ERROR;
  if (find_revolute_joint(bw, Tcl_GetString(objv[2]), &joint) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetIntFromObj(interp, objv[3], &enable) != TCL_OK) return TCL_ERROR;

  b2RevoluteJoint_EnableMotor(joint, enable);
  return TCL_OK;
}

/// Set the revolute joint limits in radians
// B2_API void b2RevoluteJoint_SetLimits( b2JointId jointId, float lower, float upper );
static int Box2DRevoluteJoint_SetLimitsCmd(ClientData clientData, Tcl_Interp *interp,
					   int objc, Tcl_Obj * const objv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  double lower, upper;
  BOX2D_WORLD *bw;
  b2JointId joint;

  if (objc < 5) {
    Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]), " world joint lower_angle upper_angle",
		     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, Tcl_GetString(objv[1])))) return TCL_ERROR;
  if (find_revolute_joint(bw, Tcl_GetString(objv[2]), &joint) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDoubleFromObj(interp, objv[3], &lower) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDoubleFromObj(interp, objv[4], &upper) != TCL_OK) return TCL_ERROR;

  b2RevoluteJoint_SetLimits(joint, lower, upper);
  return TCL_OK;
}

/// Set the revolute joint spring stiffness in Hertz
// B2_API void b2RevoluteJoint_SetSpringHertz( b2JointId jointId, float hertz );
static int Box2DRevoluteJoint_SetSpringHertzCmd(ClientData clientData, Tcl_Interp *interp,
						int objc, Tcl_Obj * const objv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  double spring_hertz;
  BOX2D_WORLD *bw;
  b2JointId joint;

  if (objc < 4) {
    Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]), " world joint spring_hertz",
		     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, Tcl_GetString(objv[1])))) return TCL_ERROR;
  if (find_revolute_joint(bw, Tcl_GetString(objv[2]), &joint) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDoubleFromObj(interp, objv[3], &spring_hertz) != TCL_OK) return TCL_ERROR;

  b2RevoluteJoint_SetSpringHertz(joint, spring_hertz);
  return TCL_OK;
}

/// Set the revolute joint spring damping ratio, non-dimensional
// B2_API void b2RevoluteJoint_SetSpringDampingRatio( b2JointId jointId, float dampingRatio );
static int Box2DRevoluteJoint_SetSpringDampingRatioCmd(ClientData clientData, Tcl_Interp *interp,
						       int objc, Tcl_Obj * const objv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  double damping_ratio;
  BOX2D_WORLD *bw;
  b2JointId joint;
  
  if (objc < 4) {
    Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]), " world joint damping_ratio",
		     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, Tcl_GetString(objv[1])))) return TCL_ERROR;
  if (find_revolute_joint(bw, Tcl_GetString(objv[2]), &joint) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDoubleFromObj(interp, objv[3], &damping_ratio) != TCL_OK) return TCL_ERROR;

  b2RevoluteJoint_SetSpringDampingRatio(joint, damping_ratio);
  return TCL_OK;
}

/// Set the revolute joint motor speed in radians per second
// B2_API void b2RevoluteJoint_SetMotorSpeed( b2JointId jointId, float motorSpeed );
static int Box2DRevoluteJoint_SetMotorSpeedCmd(ClientData clientData, Tcl_Interp *interp,
					       int objc, Tcl_Obj * const objv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  double motor_speed;
  BOX2D_WORLD *bw;
  b2JointId joint;
  
  if (objc < 4) {
    Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]), " world joint motor_speed",
		     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, Tcl_GetString(objv[1])))) return TCL_ERROR;
  if (find_revolute_joint(bw, Tcl_GetString(objv[2]), &joint) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDoubleFromObj(interp, objv[3], &motor_speed) != TCL_OK) return TCL_ERROR;

  b2RevoluteJoint_SetMotorSpeed(joint, motor_speed);
  return TCL_OK;
}

/// Set the revolute joint maximum motor torque, typically in newton-meters
// B2_API void b2RevoluteJoint_SetMaxMotorTorque( b2JointId jointId, float torque );
static int Box2DRevoluteJoint_SetMaxMotorTorqueCmd(ClientData clientData, Tcl_Interp *interp,
						   int objc, Tcl_Obj * const objv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  double max_motor_torque;
  BOX2D_WORLD *bw;
  b2JointId joint;
  
  if (objc < 4) {
    Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]), " world joint max_motor_torque",
		     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, Tcl_GetString(objv[1])))) return TCL_ERROR;
  if (find_revolute_joint(bw, Tcl_GetString(objv[2]), &joint) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDoubleFromObj(interp, objv[3], &max_motor_torque) != TCL_OK) return TCL_ERROR;

  b2RevoluteJoint_SetMaxMotorTorque(joint, max_motor_torque);
  return TCL_OK;
}



#if 0
/// It the revolute angular spring enabled?
B2_API bool b2RevoluteJoint_IsSpringEnabled( b2JointId jointId );

/// Get the revolute joint spring stiffness in Hertz
B2_API float b2RevoluteJoint_GetSpringHertz( b2JointId jointId );

/// Get the revolute joint spring damping ratio, non-dimensional
B2_API float b2RevoluteJoint_GetSpringDampingRatio( b2JointId jointId );

/// Get the revolute joint current angle in radians relative to the reference angle
///	@see b2RevoluteJointDef::referenceAngle
B2_API float b2RevoluteJoint_GetAngle( b2JointId jointId );

/// Is the revolute joint limit enabled?
B2_API bool b2RevoluteJoint_IsLimitEnabled( b2JointId jointId );

/// Get the revolute joint lower limit in radians
B2_API float b2RevoluteJoint_GetLowerLimit( b2JointId jointId );

/// Get the revolute joint upper limit in radians
B2_API float b2RevoluteJoint_GetUpperLimit( b2JointId jointId );

/// Is the revolute joint motor enabled?
B2_API bool b2RevoluteJoint_IsMotorEnabled( b2JointId jointId );

/// Get the revolute joint motor speed in radians per second
B2_API float b2RevoluteJoint_GetMotorSpeed( b2JointId jointId );

/// Get the revolute joint current motor torque, typically in newton-meters
B2_API float b2RevoluteJoint_GetMotorTorque( b2JointId jointId );

/// Get the revolute joint maximum motor torque, typically in newton-meters
B2_API float b2RevoluteJoint_GetMaxMotorTorque( b2JointId jointId );

#endif




/***********************************************************************/
/**********************      Matrix Utilities     **********************/
/***********************************************************************/

#define RADIANS (57.29577951308)
#define CLAMP(v,l,u) ((v < l) ? l : ((v > u) ? u : v))

static void matrix4_identity(float *mat)
{
  mat[0] = mat[5] = mat[10] = mat[15] = 1.0;
  mat[1] = mat[2] = mat[3] = mat[4] = 0.;
  mat[6] = mat[7] = mat[8] = mat[9] = 0.;
  mat[11] = mat[12] = mat[13] = mat[14] = 0.;
}

static void matrix4_set_translation(float *mat, float x, float y, float z)
{
  mat[12] = x;
  mat[13] = y;
  mat[14] = z;
}

static void matrix4_add_translation(float *mat, float x, float y, float z)
{
  mat[12] += x;
  mat[13] += y;
  mat[14] += z;
}

static void matrix4_get_translation(float *mat, 
				       float *x, float *y, float *z)
{
  if (x) *x = mat[12];
  if (y) *y = mat[13];
  if (z) *z = mat[14];
}

static void matrix4_set_translation_angle(float *mat, float x, float y,
					  float angle)
{
  float costheta = cos(-angle);
  float sintheta = sin(-angle);

  mat[0] = costheta;
  mat[1] = -sintheta;
  mat[4] = sintheta;
  mat[5] = costheta;
  mat[12] = x;
  mat[13] = y;
  mat[14] = 0.0;
}

static void matrix4_set_scale(float *mat, float x, float y, float z)
{
  mat[0] *= x;
  mat[5] *= y;
  mat[10]*= z;
}

static void matrix4_rotation_from_euler(float *mat, 
				float angle_x, float angle_y, float angle_z) 
{
  float A       = cos(angle_x/RADIANS);
  float B       = sin(angle_x/RADIANS);
  float C       = cos(angle_y/RADIANS);
  float D       = sin(angle_y/RADIANS);
  float E       = cos(angle_z/RADIANS);
  float F       = sin(angle_z/RADIANS);

  float AD      =   A * D;
  float BD      =   B * D;

  mat[0]  =   C * E;
  mat[1]  =  -C * F;
  mat[2]  =  -D;
  mat[4]  = -BD * E + A * F;
  mat[5]  =  BD * F + A * E;
  mat[6]  =  -B * C;
  mat[8]  =  AD * E + B * F;
  mat[9]  = -AD * F + B * E;
  mat[10] =   A * C;
  
  mat[3]  =  mat[7] = mat[11] = mat[12] = mat[13] = mat[14] = 0;
  mat[15] =  1;
}
  
static void matrix4_euler_from_rotation(float *mat, 
					   float *ang_x,
					   float *ang_y,
					   float *ang_z)
{
  float C, D;
  float angle_x, angle_y, angle_z;
  float tr_x, tr_y;

  angle_y = D = -asin( mat[2]);        /* Calculate Y-axis angle */
  C           =  cos( angle_y );
  angle_y    *= RADIANS;
  
  if ( fabs( C ) < 0.005 )             /* Gimball lock? */
    {
      tr_x      =  mat[10] / C;           /* No, so get X-axis angle */
      tr_y      = -mat[6]  / C;
      
      angle_x  = atan2( tr_y, tr_x ) * RADIANS;
      
      tr_x      =  mat[0] / C;            /* Get Z-axis angle */
      tr_y      = -mat[1] / C;

      angle_z  = atan2( tr_y, tr_x ) * RADIANS;
      }
    else                                 /* Gimball lock has occurred */
      {
      angle_x  = 0;                      /* Set X-axis angle to zero */

      tr_x      = mat[5];                 /* And calculate Z-axis angle */
      tr_y      = mat[4];

      angle_z  = atan2( tr_y, tr_x ) * RADIANS;
      }

    if (ang_x) *ang_x = CLAMP( angle_x, 0, 360 );
    if (ang_y) *ang_y = CLAMP( angle_y, 0, 360 );
    if (ang_z) *ang_z = CLAMP( angle_z, 0, 360 );
}


static void 
matrix4_rotation_from_quaternion(float *mat, 
				 float X, float Y, float Z, float W) 
{
  float xx, xy, xz, xw, yy, yz, yw, zz, zw;
  
  xx      = X * X;
  xy      = X * Y;
  xz      = X * Z;
  xw      = X * W;
  
  yy      = Y * Y;
  yz      = Y * Z;
  yw      = Y * W;
  
  zz      = Z * Z;
  zw      = Z * W;

  mat[0]  = 1 - 2 * ( yy + zz );
  mat[1]  =     2 * ( xy - zw );
  mat[2]  =     2 * ( xz + yw );
  
  mat[4]  =     2 * ( xy + zw );
  mat[5]  = 1 - 2 * ( xx + zz );
  mat[6]  =     2 * ( yz - xw );
  
  mat[8]  =     2 * ( xz - yw );
  mat[9]  =     2 * ( yz + xw );
  mat[10] = 1 - 2 * ( xx + yy );
  
  mat[3]  = mat[7] = mat[11] = mat[12] = mat[13] = mat[14] = 0;
  mat[15] = 1;
}

static void
matrix4_quaternion_from_angle_axis(float *vec4, float x, float y, float z, float theta)
{
  float        s;
  float mag = sqrt(x*x+y*y+z*z);
  
  x /= mag;
  y /= mag;
  z /= mag;
  
  theta /= 2.0;
  theta /= RADIANS;
  
  s = sin(theta);
  
  vec4[0] = s * x;
  vec4[1] = s * y;
  vec4[2] = s * z;
  vec4[3] = cos(theta);
}

static void
matrix4_rotation_from_angle_axis(float *mat, float x, float y, float z, float theta)
{
  static float q[4];
  matrix4_quaternion_from_angle_axis(q, x, y, z, theta);
  matrix4_rotation_from_quaternion(mat, q[0], q[1], q[2], q[3]);
  return;
}

static void
matrix4_quaternion_from_rotation(float *mat, float *X, float *Y, float *Z, float *W)
{
  static float q[4];
  float s;
  float tr = mat[0] + mat[5] + mat[10] + 1.0;
  if (tr > 0.0f) {
    s = 0.5 / sqrt(tr);
    q[3] = 0.25 / s;
    q[0] = ( mat[6] - mat[9] ) * s;
    q[1] = ( mat[8] - mat[2] ) * s;
    q[2] = ( mat[1] - mat[4] ) * s;
  }
  else {
    if ((mat[0] > mat[5]) && (mat[0] > mat[10])) {
      s = sqrt( 1.0 + mat[0] - mat[5] - mat[10] ) * 2;
      q[3] = (mat[9] - mat[6]) / s;
      q[0] = 0.25 * s;
      q[1] = (mat[4] + mat[1]) / s; 
      q[2] = (mat[8] + mat[2]) / s; 
    }
    else if (mat[5] > mat[10]) { 
      s = sqrt( 1.0 + mat[5] - mat[0] - mat[10] ) * 2;
      q[3] = (mat[8] - mat[2]) / s;
      q[0] = (mat[4] + mat[1]) / s; 
      q[1] = 0.25 * s;
      q[2] = (mat[9] + mat[6]) / s; 
    } 
    else { 
      s = sqrt( 1.0 + mat[10] - mat[0] - mat[5] ) * 2;
      q[3] = (mat[4] - mat[1]) / s;
      q[0] = (mat[8] + mat[2]) / s; 
      q[1] = (mat[9] + mat[6]) / s; 
      q[2] = 0.25 * s;
    }
  }
  if (X) (*X) = q[0];
  if (Y) (*Y) = q[1];
  if (Z) (*Z) = q[2];
  if (W) (*W) = q[3];
}    

static void
matrix4_angle_axis_from_quaternion(float *vec4, float x, float y, float z, float w)
{
  double        cos_a, sin_a, angle;
  float mag = sqrt(x*x+y*y+z*z+w*w);

  x /= mag;
  y /= mag;
  z /= mag;
  w /= mag;

  cos_a = w;
  angle = acos( cos_a ) * 2;
  sin_a = sqrt( 1.0 - cos_a * cos_a );

  if ( fabs( sin_a ) < 0.0005 ) sin_a = 1;

  vec4[0] = angle*RADIANS;
  vec4[1] = x / sin_a;
  vec4[2] = y / sin_a;
  vec4[3] = z / sin_a;
}



static int matrix4IndentityCmd(ClientData clientData, 
				 Tcl_Interp *interp,
				 int argc, char *argv[])
{
  float *vals;
  DYN_LIST *mat;

  vals = (float *) malloc(16*sizeof(float));
  matrix4_identity(vals);
  mat = dfuCreateDynListWithVals(DF_FLOAT, 16, vals);
  return(tclPutList(interp, mat));
}

static int matrix4GetTranslationCmd(ClientData clientData, 
				      Tcl_Interp *interp,
				      int argc, char *argv[])
{
  float *vals;
  DYN_LIST *v;
  static float matrix[16];

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " matrix4",
		     (char *) NULL);
    return TCL_ERROR;
  }
  else {
    if (find_matrix4(interp, argv[1], matrix) != TCL_OK) return TCL_ERROR;
  }

  vals = (float *) malloc(3*sizeof(float));
  matrix4_get_translation(matrix, &vals[0], &vals[1], &vals[2]);
  v = dfuCreateDynListWithVals(DF_FLOAT, 3, vals);
  return(tclPutList(interp, v));
}

static int matrix4SetAddTranslationCmd(ClientData clientData, 
				      Tcl_Interp *interp,
				      int argc, char *argv[])
{
  int op = (int)(size_t)clientData;
  DYN_LIST *v;
  float *vals;
  double x, y, z;
  static float matrix[16], vec3[3];

  if (argc != 3 && argc != 5) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " matrix4 {vec3 | x y z}",
		     (char *) NULL);
    return TCL_ERROR;
  }
  if (find_matrix4(interp, argv[1], matrix) != TCL_OK) return TCL_ERROR;


  if (argc == 3) {
    if (find_vec_3(interp, argv[2], vec3) != TCL_OK) return TCL_ERROR;
  }
  else {
    if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &z) != TCL_OK) return TCL_ERROR;
    vec3[0] = x;
    vec3[1] = y;
    vec3[2] = z;
  }

  vals = (float *) malloc(16*sizeof(float));
  memcpy(vals, matrix, sizeof(float)*16);
  if (op == TRANS_SET)
    matrix4_set_translation(vals, vec3[0], vec3[1], vec3[2]);
  else
    matrix4_add_translation(vals, vec3[0], vec3[1], vec3[2]);
    
  v = dfuCreateDynListWithVals(DF_FLOAT, 16, vals);
  return(tclPutList(interp, v));
}

static int matrix4CreateTranslationAngleCmd(ClientData clientData, 
					    Tcl_Interp *interp,
					    int argc, char *argv[])
{
  int op = (int)(size_t)clientData;
  DYN_LIST *v;
  float *vals;
  double x, y, angle;

#ifdef _MSC_VER
#define M_PI 3.14159265358979323846
#endif
  const float DEG2RAD = M_PI/180.;
  
  static float matrix[16], vec3[3];

  if (argc != 4)  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " x y angle (deg)}",
		     (char *) NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetDouble(interp, argv[1], &x) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &y) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &angle) != TCL_OK) return TCL_ERROR;

  vals = (float *) malloc(16*sizeof(float));
  matrix4_identity(vals);
  matrix4_set_translation_angle(vals, x, y, angle*DEG2RAD);
  v = dfuCreateDynListWithVals(DF_FLOAT, 16, vals);
  return(tclPutList(interp, v));
}

static int matrix4SetScaleCmd(ClientData clientData, 
			      Tcl_Interp *interp,
			      int argc, char *argv[])
{
  int op = (int)(size_t)clientData;
  DYN_LIST *v;
  float *vals;
  double x, y, z;
  static float matrix[16], vec3[3];
  
  if (argc != 3 && argc != 5) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " matrix4 {vec3 | x y z}",
		     (char *) NULL);
    return TCL_ERROR;
  }
  if (find_matrix4(interp, argv[1], matrix) != TCL_OK) return TCL_ERROR;
  
  
  if (argc == 3) {
    if (find_vec_3(interp, argv[2], vec3) != TCL_OK) return TCL_ERROR;
  }
  else {
    if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &z) != TCL_OK) return TCL_ERROR;
    vec3[0] = x;
    vec3[1] = y;
    vec3[2] = z;
  }

  vals = (float *) malloc(16*sizeof(float));
  memcpy(vals, matrix, sizeof(float)*16);
  matrix4_set_scale(vals, vec3[0], vec3[1], vec3[2]);
  v = dfuCreateDynListWithVals(DF_FLOAT, 16, vals);
  return(tclPutList(interp, v));
}

static int matrix4RotationFromEulerCmd(ClientData clientData, 
					 Tcl_Interp *interp,
					 int argc, char *argv[])
{
  DYN_LIST *v;
  float *vals;
  static float vec3[3];

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " vec3",
		     (char *) NULL);
    return TCL_ERROR;
  }
  if (find_vec_3(interp, argv[1], vec3) != TCL_OK) return TCL_ERROR;

  vals = (float *) malloc(16*sizeof(float));
  matrix4_rotation_from_euler(vals, vec3[0], vec3[1], vec3[2]);
  v = dfuCreateDynListWithVals(DF_FLOAT, 16, vals);
  return(tclPutList(interp, v));
}

static int matrix4EulerFromRotationCmd(ClientData clientData, 
					 Tcl_Interp *interp,
					 int argc, char *argv[])
{
  float *vals;
  DYN_LIST *v;
  static float matrix[16];

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " matrix4",
		     (char *) NULL);
    return TCL_ERROR;
  }
  else {
    if (find_matrix4(interp, argv[1], matrix) != TCL_OK) return TCL_ERROR;
  }

  vals = (float *) malloc(3*sizeof(float));
  matrix4_euler_from_rotation(matrix, &vals[0], &vals[1], &vals[2]);
  v = dfuCreateDynListWithVals(DF_FLOAT, 3, vals);
  return(tclPutList(interp, v));
}


static int matrix4RotationFromAngleAxisCmd(ClientData clientData, 
					   Tcl_Interp *interp,
					   int argc, char *argv[])
{
  DYN_LIST *v;
  float *vals;
  static double x, y, z, spin;

  if (argc != 5 && argc != 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " {vec4 | spin x y z}",
		     (char *) NULL);
    return TCL_ERROR;
  }

  if (argc == 2) {
    static float vec4[4];
    if (find_vec_4(interp, argv[1], vec4) != TCL_OK) return TCL_ERROR;
    spin = vec4[0];
    x = vec4[1];
    y = vec4[2];
    z = vec4[3];
  } 
  else {
    if (Tcl_GetDouble(interp, argv[1], &spin) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &z) != TCL_OK) return TCL_ERROR;
  }

  vals = (float *) malloc(16*sizeof(float));
  matrix4_rotation_from_angle_axis(vals, x, y, z, spin);
  v = dfuCreateDynListWithVals(DF_FLOAT, 16, vals);
  return(tclPutList(interp, v));
}

static int matrix4RotationFromQuaternionCmd(ClientData clientData, 
					    Tcl_Interp *interp,
					    int argc, char *argv[])
{
  DYN_LIST *v;
  float *vals;
  static double q0, q1, q2, q3;

  if (argc != 5 && argc != 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " {vec4 | qx qy qz qw}",
		     (char *) NULL);
    return TCL_ERROR;
  }
  if (argc == 2) {
    static float vec4[4];
    if (find_vec_4(interp, argv[1], vec4) != TCL_OK) return TCL_ERROR;
    q0 = vec4[0];
    q1 = vec4[1];
    q2 = vec4[2];
    q3 = vec4[3];
  } 
  else {
    if (Tcl_GetDouble(interp, argv[1], &q0) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[2], &q1) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &q2) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &q3) != TCL_OK) return TCL_ERROR;
  }

  vals = (float *) malloc(16*sizeof(float));
  matrix4_rotation_from_quaternion(vals, q0, q1, q2, q3);
  v = dfuCreateDynListWithVals(DF_FLOAT, 16, vals);
  return(tclPutList(interp, v));
}

static int matrix4QuaternionFromRotationCmd(ClientData clientData, 
					    Tcl_Interp *interp,
					    int argc, char *argv[])
{
  float *vals;
  DYN_LIST *v;
  static float matrix[16];
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " matrix4",
		     (char *) NULL);
    return TCL_ERROR;
  }
  else {
    if (find_matrix4(interp, argv[1], matrix) != TCL_OK) return TCL_ERROR;
  }
  
  vals = (float *) malloc(4*sizeof(float));
  matrix4_quaternion_from_rotation(matrix, &vals[0], &vals[1], &vals[2], &vals[3]);
  v = dfuCreateDynListWithVals(DF_FLOAT, 4, vals);
  return(tclPutList(interp, v));
}

static int matrix4AngleAxisFromQuaternionCmd(ClientData clientData, 
					     Tcl_Interp *interp,
					     int argc, char *argv[])
{
  float *vals;
  DYN_LIST *v;
  static float vec4[16];
  double q0, q1, q2, q3;

  if (argc != 5 && argc != 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " {vec4 | qx qy qz qw}",
		     (char *) NULL);
    return TCL_ERROR;
  }
  if (argc == 2) {
    static float vec4[4];
    if (find_vec_4(interp, argv[1], vec4) != TCL_OK) return TCL_ERROR;
    q0 = vec4[0];
    q1 = vec4[1];
    q2 = vec4[2];
    q3 = vec4[3];
  } 
  else {
    if (Tcl_GetDouble(interp, argv[1], &q0) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[2], &q1) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &q2) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &q3) != TCL_OK) return TCL_ERROR;
  }

  vals = (float *) malloc(4*sizeof(float));
  matrix4_angle_axis_from_quaternion(vals, q0, q1, q2, q3);
  v = dfuCreateDynListWithVals(DF_FLOAT, 4, vals);
  return(tclPutList(interp, v));
}

static int matrix4QuaternionFromAngleAxisCmd(ClientData clientData, 
					     Tcl_Interp *interp,
					     int argc, char *argv[])
{
  float *vals;
  DYN_LIST *v;
  static float vec4[16];
  double spin, x, y, z;

  if (argc != 5 && argc != 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " {vec4 | spin x y z}",
		     (char *) NULL);
    return TCL_ERROR;
  }
  if (argc == 2) {
    static float vec4[4];
    if (find_vec_4(interp, argv[1], vec4) != TCL_OK) return TCL_ERROR;
    spin = vec4[0];
    x = vec4[1];
    y = vec4[2];
    z = vec4[3];
  } 
  else {
    if (Tcl_GetDouble(interp, argv[1], &spin) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &z) != TCL_OK) return TCL_ERROR;
  }

  vals = (float *) malloc(4*sizeof(float));
  matrix4_quaternion_from_angle_axis(vals, x, y, z, spin);
  v = dfuCreateDynListWithVals(DF_FLOAT, 4, vals);
  return(tclPutList(interp, v));
}


#ifdef _WIN32
EXPORT(int,Box_Init) (Tcl_Interp *interp)
#else
#ifdef __cplusplus
extern "C" {
#endif
  extern int Box_Init(Tcl_Interp *interp);
#ifdef __cplusplus
}
#endif
int Box_Init(Tcl_Interp *interp)
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
  
  if (Box2DID >= 0)		/* Already been here */
    return TCL_OK;
  
  Box2DID = gobjRegisterType();

  Tcl_CreateCommand(interp, "Box2D", 
		    (Tcl_CmdProc *) Box2DCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "Box2D_getBodies",
                    (Tcl_CmdProc *)Box2DGetBodiesCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateCommand(interp, "Box2D_update", 
		    (Tcl_CmdProc *) Box2DUpdateCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);


  Tcl_CreateCommand(interp, "Box2D_createBox", 
		    (Tcl_CmdProc *) Box2DCreateBoxCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "Box2D_createCircle", 
		    (Tcl_CmdProc *) Box2DCreateCircleCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "Box2D_setBodyType",
                    (Tcl_CmdProc *)Box2DSetBodyTypeCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateCommand(interp, "Box2D_getBodyInfo",
                    (Tcl_CmdProc *)Box2DGetBodyInfoCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);

  Tcl_CreateCommand(interp, "Box2D_setTransform",
                    (Tcl_CmdProc *)Box2DSetTransformCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);

#if 0

  Tcl_CreateCommand(interp, "Box2D_applyForce",
                    (Tcl_CmdProc *)Box2DApplyForceCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateCommand(interp, "Box2D_applyLinearImpulse",
                    (Tcl_CmdProc *)Box2DApplyLinearImpulseCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);

  Tcl_CreateCommand(interp, "Box2D_setFilterData",
                    (Tcl_CmdProc *)Box2DSetFilterDataCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateCommand(interp, "Box2D_setSensor",
                    (Tcl_CmdProc *)Box2DSetSensorCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);
#endif
  
  Tcl_CreateCommand(interp, "Box2D_linkObj", 
		    (Tcl_CmdProc *) Box2DLinkObjCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  /* Body and Shape Getters/Setters */
  Tcl_CreateObjCommand(interp, "Box2D_setRestitution", 
		       (Tcl_ObjCmdProc *) Box2DSetRestitutionCmd,
		       (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateObjCommand(interp, "Box2D_setFrictionn", 
		       (Tcl_ObjCmdProc *) Box2DSetFrictionCmd,
		       (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  
  /* Revolute Joints */
  Tcl_CreateObjCommand(interp, "Box2D_revoluteJointCreate", 
		       (Tcl_ObjCmdProc *) Box2DRevoluteJointCreateCmd, 
		       (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateObjCommand(interp, "Box2D_revoluteJointEnableSpring", 
		       (Tcl_ObjCmdProc *) Box2DRevoluteJoint_EnableSpringCmd, 
		       (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateObjCommand(interp, "Box2D_revoluteJointEnableMotor", 
		       (Tcl_ObjCmdProc *) Box2DRevoluteJoint_EnableMotorCmd, 
		       (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateObjCommand(interp, "Box2D_revoluteJointEnableLimit", 
		       (Tcl_ObjCmdProc *) Box2DRevoluteJoint_EnableLimitCmd, 
		       (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateObjCommand(interp, "Box2D_revoluteJointSetLimits", 
		       (Tcl_ObjCmdProc *) Box2DRevoluteJoint_SetLimitsCmd, 
		       (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateObjCommand(interp, "Box2D_revoluteJointSetSpringHertz", 
		       (Tcl_ObjCmdProc *) Box2DRevoluteJoint_SetSpringHertzCmd, 
		       (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateObjCommand(interp, "Box2D_revoluteJointSetSpringDampingRatio", 
		       (Tcl_ObjCmdProc *) Box2DRevoluteJoint_SetSpringDampingRatioCmd, 
		       (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateObjCommand(interp, "Box2D_revoluteJointSetMotorSpeed", 
		       (Tcl_ObjCmdProc *) Box2DRevoluteJoint_SetMotorSpeedCmd, 
		       (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateObjCommand(interp, "Box2D_revoluteJointSetMaxMotorTorque", 
		       (Tcl_ObjCmdProc *) Box2DRevoluteJoint_SetMaxMotorTorqueCmd, 
		       (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);  
  
  Tcl_CreateCommand(interp, "Box2D_getContactBeginEventCount", 
		    (Tcl_CmdProc *) Box2DGetContactBeginEventCountCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "Box2D_getContactBeginEvents", 
		    (Tcl_CmdProc *) Box2DGetContactBeginEventsCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "Box2D_getContactEndEventCount", 
		    (Tcl_CmdProc *) Box2DGetContactEndEventCountCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "Box2D_getContactEndEvents", 
		    (Tcl_CmdProc *) Box2DGetContactEndEventsCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  
  Tcl_CreateCommand(interp, "mat4_identity", 
		    (Tcl_CmdProc *) matrix4IndentityCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "mat4_getTranslation", 
		    (Tcl_CmdProc *) matrix4GetTranslationCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "mat4_setTranslation", 
		    (Tcl_CmdProc *) matrix4SetAddTranslationCmd, 
		    (ClientData) TRANS_SET, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "mat4_addTranslation", 
		    (Tcl_CmdProc *) matrix4SetAddTranslationCmd, 
		    (ClientData) TRANS_ADD, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "mat4_createTranslationAngle", 
		    (Tcl_CmdProc *) matrix4CreateTranslationAngleCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "mat4_setScale", 
		    (Tcl_CmdProc *) matrix4SetScaleCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);


  Tcl_CreateCommand(interp, "mat4_rotationFromEuler", 
		    (Tcl_CmdProc *) matrix4RotationFromEulerCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "mat4_eulerToRotation", 
		    (Tcl_CmdProc *) matrix4RotationFromEulerCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "mat4_eulerFromRotation", 
		    (Tcl_CmdProc *) matrix4EulerFromRotationCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "mat4_rotationToEuler", 
		    (Tcl_CmdProc *) matrix4EulerFromRotationCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "mat4_rotationFromAngleAxis", 
		    (Tcl_CmdProc *) matrix4RotationFromAngleAxisCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "mat4_angleAxisToRotation", 
		    (Tcl_CmdProc *) matrix4RotationFromAngleAxisCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "mat4_rotationFromQuaternion", 
		    (Tcl_CmdProc *) matrix4RotationFromQuaternionCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "mat4_quaternionToRotation", 
		    (Tcl_CmdProc *) matrix4RotationFromQuaternionCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "mat4_quaternionFromRotation", 
		    (Tcl_CmdProc *) matrix4QuaternionFromRotationCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "mat4_rotationToQuaternion", 
		    (Tcl_CmdProc *) matrix4QuaternionFromRotationCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "mat4_angleAxisFromQuaternion", 
		    (Tcl_CmdProc *) matrix4AngleAxisFromQuaternionCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "mat4_quaternionToAngleAxis", 
		    (Tcl_CmdProc *) matrix4AngleAxisFromQuaternionCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  
  Tcl_CreateCommand(interp, "mat4_quaternionFromAngleAxis", 
		    (Tcl_CmdProc *) matrix4QuaternionFromAngleAxisCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "mat4_angleAxisToQuaternion", 
		    (Tcl_CmdProc *) matrix4QuaternionFromAngleAxisCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);


  OurInterp = interp;

  return TCL_OK;
}
