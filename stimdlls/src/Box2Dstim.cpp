/*
 * Box2Dstim.c
 *  Use the Box2D library to simulate 2D physics
 */

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#ifdef WIN32
#define _USE_MATH_DEFINES
#include <cmath>
#else
#include <math.h>
#endif

#include <iomanip>
#include <iostream>

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

static Tcl_Interp *OurInterp;
static int Box2DID = -1;	/* unique Box2D object id */

struct Box2D_world;		/* for circular reference */

/*
enum b2BodyType
 {
     b2_staticBody = 0,
     b2_kinematicBody,
     b2_dynamicBody
 };
*/


class ContactListener : public b2ContactListener 
{
public:
  Tcl_Interp *interp;
  struct Box2D_world *world;
  
private: 
  void BeginContact(b2Contact* contact);
  void EndContact(b2Contact* contact);
  void PreSolve(b2Contact* contact, const b2Manifold* oldManifold);
  //  void PostSolve(b2Contact* contact, const b2ContactImpulse * impulse);
};

typedef struct Box2D_world {
  char name[32];
  Tcl_Interp *interp;
  b2World *bWorld;
  b2Vec2 gravity;
  
  int bodyCount;
  Tcl_HashTable bodyTable;

  int figureDefCount;
  Tcl_HashTable figureDefTable;

  ContactListener *contact_listener;
  char *beginContactCallback;
  char *endContactCallback;
  char *preSolveCallback;
  char *postSolveCallback;
  int32 velocityIterations;
  int32 positionIterations;
  
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

void ContactListener::BeginContact(b2Contact *contact)
{
  b2Body *bodyA = contact->GetFixtureA()->GetBody();
  b2Body *bodyB = contact->GetFixtureB()->GetBody();

  BOX2D_USERDATA *dataA, *dataB;
  dataA = (BOX2D_USERDATA *)bodyA->GetUserData().pointer;
  dataB = (BOX2D_USERDATA *)bodyB->GetUserData().pointer;
  
  if (world->beginContactCallback)
  {
    int rc;
    Tcl_Obj *command = Tcl_NewObj();
    Tcl_IncrRefCount(command);
    Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(world->beginContactCallback, -1));
    Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(world->name, -1));
    Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(dataA->name, -1));
    Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(dataB->name, -1));

    rc = Tcl_EvalObjEx(interp, command, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount(command);
  }
}

void ContactListener::EndContact(b2Contact *contact)
{
  b2Body *bodyA = contact->GetFixtureA()->GetBody();
  b2Body *bodyB = contact->GetFixtureB()->GetBody();

  BOX2D_USERDATA *dataA, *dataB;
  dataA = (BOX2D_USERDATA *)bodyA->GetUserData().pointer;
  dataB = (BOX2D_USERDATA *)bodyB->GetUserData().pointer;

  if (world->endContactCallback)
  {
    int rc;
    Tcl_Obj *command = Tcl_NewObj();
    Tcl_IncrRefCount(command);
    Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(world->endContactCallback, -1));
    Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(world->name, -1));
    Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(dataA->name, -1));
    Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(dataB->name, -1));

    rc = Tcl_EvalObjEx(interp, command, TCL_EVAL_DIRECT);
    Tcl_DecrRefCount(command);
  }
}

void ContactListener::PreSolve(b2Contact *contact, const b2Manifold *oldManifold)
{
  b2WorldManifold worldManifold;
  contact->GetWorldManifold(&worldManifold);

  b2PointState state1[2], state2[2];
  b2GetPointStates(state1, state2, oldManifold, contact->GetManifold());

  BOX2D_USERDATA *dataA, *dataB;

  if (state2[0] == b2_addState)
  {
    const b2Body *bodyA = contact->GetFixtureA()->GetBody();
    const b2Body *bodyB = contact->GetFixtureB()->GetBody();
    b2Vec2 point = worldManifold.points[0];
    b2Vec2 vA = bodyA->GetLinearVelocityFromWorldPoint(point);
    b2Vec2 vB = bodyB->GetLinearVelocityFromWorldPoint(point);
    float approachVelocity = b2Dot(vB - vA, worldManifold.normal);
    dataA = (BOX2D_USERDATA *)bodyA->GetUserData().pointer;
    dataB = (BOX2D_USERDATA *)bodyB->GetUserData().pointer;

    if (world->preSolveCallback)
    {
      int rc;
      Tcl_Obj *command = Tcl_NewObj();
      Tcl_IncrRefCount(command);
      Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(world->preSolveCallback, -1));
      Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(world->name, -1));
      Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(dataA->name, -1));
      Tcl_ListObjAppendElement(interp, command, Tcl_NewStringObj(dataB->name, -1));
      Tcl_ListObjAppendElement(interp, command, Tcl_NewDoubleObj(point.x));
      Tcl_ListObjAppendElement(interp, command, Tcl_NewDoubleObj(point.y));
      Tcl_ListObjAppendElement(interp, command, Tcl_NewDoubleObj(approachVelocity));

      rc = Tcl_EvalObjEx(interp, command, TCL_EVAL_DIRECT);
      Tcl_DecrRefCount(command);
    }
  }
}

static void Box2D_free_userdata (b2Body* body);
static void Box2D_update_link (b2Body* body, float x, float y, float angle);

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
		     char *name, b2Body **b);

static BOX2D_WORLD *find_Box2D(Tcl_Interp *interp, 
				OBJ_LIST *olist, char *idstring)
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


static int find_body(BOX2D_WORLD *bw, char *name, b2Body **b)
{
  b2Body *body;
  Tcl_HashEntry *entryPtr;

  if ((entryPtr = Tcl_FindHashEntry(&bw->bodyTable, name))) {
    body = (b2Body *) Tcl_GetHashValue(entryPtr);
    if (!body) {
      Tcl_SetResult(bw->interp, "bad body ptr in hash table", TCL_STATIC);
      return TCL_ERROR;
    }
    if (b) *b = body;
    return TCL_OK;
  }
  else {
    if (b) {			/* If img was null, then don't set error */
      Tcl_AppendResult(bw->interp, "body \"", name, "\" not found", 
		       (char *) NULL);
    }
    return TCL_ERROR;
  }
}

/***********************************************************************/
/***********************      Box2D OBJ Funcs     **********************/
/***********************************************************************/

static int Box2DUpdate(GR_OBJ *g)
{
  BOX2D_WORLD *bw = (BOX2D_WORLD *) GR_CLIENTDATA(g);
  b2World *bWorld = bw->bWorld;
  float elapsed;

  bw->time = getStimTime();
  //  elapsed = (nw->time-nw->lasttime)/1000.;
  elapsed = getFrameDuration()/1000.;
  bw->lasttime = bw->time;

  // Instruct the world to perform a single step of simulation.
  // It is generally best to keep the time step and iterations fixed.
  bw->bWorld->Step((float) elapsed,
		   bw->velocityIterations, bw->positionIterations);

  // Need to update linked objects here
  for (b2Body* b = bw->bWorld->GetBodyList(); b; b = b->GetNext()) {
    b2Vec2 position = b->GetPosition();
    float angle = b->GetAngle();
    Box2D_update_link (b, position.x, position.y, angle);
  }
  
  return(TCL_OK);
}

static void Box2DDelete(GR_OBJ *g) 
{
  BOX2D_WORLD *bw = (BOX2D_WORLD *) GR_CLIENTDATA(g);

  Tcl_DeleteHashTable(&bw->bodyTable);

  delete bw->contact_listener;

  for (b2Body *b = bw->bWorld->GetBodyList(); b; b = b->GetNext())
  {
    if (b->GetUserData().pointer)
      free((void *)b->GetUserData().pointer);
  }

  if (bw->beginContactCallback)
  {
    free(bw->beginContactCallback);
    bw->beginContactCallback = NULL;
  }

  if (bw->endContactCallback)
  {
    free(bw->endContactCallback);
    bw->endContactCallback = NULL;
  }

  if (bw->preSolveCallback)
  {
    free(bw->preSolveCallback);
    bw->preSolveCallback = NULL;
  }

  if (bw->postSolveCallback)
  {
    free(bw->postSolveCallback);
    bw->postSolveCallback = NULL;
  }
  delete bw->bWorld;

  free((void *) bw);
}

static int Box2DReset(GR_OBJ *g)
{
  BOX2D_WORLD *bw = (BOX2D_WORLD *) GR_CLIENTDATA(g);
  b2World *bWorld = bw->bWorld;

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
  BOX2D_WORLD *bw;

  obj = gobjCreateObj();
  if (!obj) return -1;

  GR_OBJTYPE(obj) = Box2DID;
  strcpy(GR_NAME(obj), name);

  bw = (BOX2D_WORLD *) calloc(1, sizeof(BOX2D_WORLD));

  bw->gravity = {0.0f, -10.0f};

  /* Reasonable simulation settings */
  bw->velocityIterations = 6;
  bw->positionIterations = 2;

  bw->bWorld = new b2World(bw->gravity);
  
  if (!bw->bWorld) {
    Tcl_AppendResult(interp, argv[0], ": error creating Box2D world", NULL);
    free(bw);
    return TCL_ERROR;
  }
  /* Initialize contact listener for this world */
  bw->contact_listener = new ContactListener();
  bw->contact_listener->interp = interp;
  bw->contact_listener->world = bw;
  bw->bWorld->SetContactListener(bw->contact_listener);
  bw->beginContactCallback = NULL;
  bw->endContactCallback = NULL;
  bw->preSolveCallback = NULL;
  bw->postSolveCallback = NULL;
  bw->interp = interp;

  Tcl_InitHashTable(&bw->bodyTable, TCL_STRING_KEYS);

  GR_CLIENTDATA(obj) = bw;
  GR_DELETEFUNCP(obj) = Box2DDelete;
  GR_RESETFUNCP(obj) = (RESET_FUNC) Box2DReset;
  GR_UPDATEFUNCP(obj) = (UPDATE_FUNC) Box2DUpdate;

  int gid = gobjAddObj(olist, obj);
  sprintf(bw->name, "%d", gid);
  sprintf(interp->result, "%d", gid);
    
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
  b2Body *body;

  if (argc < 2)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world [typemask]", (char *)NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;

  if (argc > 2)
  {
    if (Tcl_GetInt(interp, argv[2], &typemask) != TCL_OK)
      return TCL_ERROR;
  }

  bodylist = Tcl_NewObj();

  for (entryPtr = Tcl_FirstHashEntry(&bw->bodyTable, &search);
       entryPtr != NULL;
       entryPtr = Tcl_NextHashEntry(&search))
  {
    body = (b2Body *)Tcl_GetHashValue(entryPtr);
    if ((1 << (int)body->GetType()) & typemask)
    {
      Tcl_ListObjAppendElement(interp, bodylist,
                               Tcl_NewStringObj((char *)Tcl_GetHashKey(&bw->bodyTable, entryPtr), -1));
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
    interp->result = "usage: Box2D_update world elapsed";
    return TCL_ERROR;
  }
  
  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &elapsed) != TCL_OK) return TCL_ERROR;

  bw->lasttime = bw->time;
  bw->time += (int) (elapsed*1000);

  bw->bWorld->Step((float) elapsed,
		   bw->velocityIterations, bw->positionIterations);

  // Need to update linked objects here
  
  return(TCL_OK);

}


static int Box2DCreateBodyCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;
  BOX2D_USERDATA *userdata;
  char body_name[32];
  Tcl_HashEntry *entryPtr;
  int newentry;
  double x, y, angle;
  int bodyType;
  
  if (argc < 5) {
    interp->result = "usage: Box2D_createBody world type x y [angle]";
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &bodyType) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &x) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &y) != TCL_OK) return TCL_ERROR;
  if (argc > 5) {
    if (Tcl_GetDouble(interp, argv[5], &angle) != TCL_OK) return TCL_ERROR;
  }
  else angle = 0.0;
  
  userdata = (BOX2D_USERDATA *) calloc(1, sizeof(BOX2D_USERDATA));
  userdata->world = bw;

  /* Default bodyDef */
  b2BodyDef bodyDef;
  bodyDef.type = (b2BodyType) bodyType;
  bodyDef.userData.pointer = (uintptr_t) userdata;
  bodyDef.position.Set(x, y);
  bodyDef.angle = angle*(M_PI/180.);

  // Damping
  bodyDef.angularDamping = .05;
  bodyDef.linearDamping = .05;

  b2Body *body = bw->bWorld->CreateBody(&bodyDef);

  sprintf(body_name, "body%d", bw->bodyCount++); 
  strcpy(userdata->name, body_name);

  entryPtr = Tcl_CreateHashEntry(&bw->bodyTable, body_name, &newentry);
  Tcl_SetHashValue(entryPtr, body);

  Tcl_SetResult(interp, body_name, TCL_VOLATILE);
  return(TCL_OK);
}


static int Box2DCreateBoxFixtureCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;
  BOX2D_USERDATA *userdata;
  b2Body *body;
  double width, height;
  double x, y, angle;
  
  if (argc < 8) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world body width height x y angle",
		     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(bw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &width) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &height) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &x) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[6], &y) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[7], &angle) != TCL_OK) return TCL_ERROR;

  userdata = (BOX2D_USERDATA *) body->GetUserData().pointer;

  b2FixtureDef fixtureDef;
  
  // Define the ground box shape.
  b2PolygonShape box;
  b2Vec2 pos(x, y);
  
  // The extents are the half-widths of the box.
  box.SetAsBox(width/2, height/2, pos, angle*(M_PI/180.));
  
  fixtureDef.shape = &box;
  // Set the box density to be non-zero, so it will be dynamic.
  fixtureDef.density = 1.0f;
  
  // Override the default friction.
  fixtureDef.friction = 0.6f;

  // Default resitution
  fixtureDef.restitution = 0.2f;

  // Add the ground fixture to the ground body.
  body->CreateFixture(&fixtureDef);

  return(TCL_OK);
}


static int Box2DCreateCircleFixtureCmd(ClientData clientData, Tcl_Interp *interp,
				       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;
  BOX2D_USERDATA *userdata;
  b2Body *body;
  double x, y, r;
  
  if (argc < 6) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world body x y radius",
		     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(bw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &x) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &y) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &r) != TCL_OK) return TCL_ERROR;

  userdata = (BOX2D_USERDATA *) body->GetUserData().pointer;

  b2FixtureDef fixtureDef;
  
  // Define the ground box shape.
  b2CircleShape circle;
  circle.m_p.Set(x, y);
  circle.m_radius = r;
  
  fixtureDef.shape = &circle;
  // Set the box density to be non-zero, so it will be dynamic.
  fixtureDef.density = 1.0f;
  
  // Override the default friction.
  fixtureDef.friction = 0.3f;

  // Add the ground fixture to the ground body.
  body->CreateFixture(&fixtureDef);

  return(TCL_OK);
}

static int Box2DSetBodyTypeCmd(ClientData clientData, Tcl_Interp *interp,
                               int argc, char *argv[])
{
  //  for (b2Body* b = bw->bWorld->GetBodyList(); b; b = b->GetNext()) {
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;

  b2Body *body;
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

  body->SetType((b2BodyType)body_type);

  return TCL_OK;
}

static int Box2DSetFilterDataCmd(ClientData clientData, Tcl_Interp *interp,
                                 int argc, char *argv[])
{
  //  for (b2Body* b = bw->bWorld->GetBodyList(); b; b = b->GetNext()) {
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;
  b2Body *body;
  int categoryBits, maskBits;
  int groupIndex;

  if (argc < 4)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0],
                     " world body categoryBits [maskBits [groupIndex]]",
                     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;

  if (find_body(bw, argv[2], &body) != TCL_OK)
    return TCL_ERROR;

  if (Tcl_GetInt(interp, argv[3], &categoryBits) != TCL_OK)
    return TCL_ERROR;

  if (argc > 4)
  {
    if (Tcl_GetInt(interp, argv[4], &maskBits) != TCL_OK)
      return TCL_ERROR;
  }

  if (argc > 5)
  {
    if (Tcl_GetInt(interp, argv[5], &groupIndex) != TCL_OK)
      return TCL_ERROR;
  }

  for (b2Fixture *f = body->GetFixtureList(); f; f = f->GetNext())
  {
    b2Filter filter = f->GetFilterData();
    filter.categoryBits = categoryBits;
    if (argc > 4)
      filter.maskBits = maskBits;
    if (argc > 5)
      filter.groupIndex = groupIndex;
    f->SetFilterData(filter);
  }

  return TCL_OK;
}

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

static int Box2DSetTransformCmd(ClientData clientData, Tcl_Interp *interp,
                                int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;
  b2Body *body;
  double x, y;
  double angle = 0;

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
  if (argc > 5)
  {
    if (Tcl_GetDouble(interp, argv[5], &angle) != TCL_OK)
      return TCL_ERROR;
  }

  body->SetTransform(b2Vec2(x, y), angle);

  printf("SetTransform: %f %f %f\n", x, y, angle);

  return TCL_OK;
}

static int Box2DSetSensorCmd(ClientData clientData, Tcl_Interp *interp,
                             int argc, char *argv[])
{
  //  for (b2Body* b = bw->bWorld->GetBodyList(); b; b = b->GetNext()) {
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

static int Box2DGetBodyInfoCmd(ClientData clientData, Tcl_Interp *interp,
                               int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  float angle;
  b2Vec2 position;
  BOX2D_WORLD *bw;
  b2Body *body;
  static char buf[64];

  if (argc < 3)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world body",
                     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;
  if (find_body(bw, argv[2], &body) != TCL_OK)
    return TCL_ERROR;

  position = body->GetPosition();
  angle = body->GetAngle();
  sprintf(buf, "%f %f %f", position.x, position.y, angle);
  Tcl_SetResult(interp, buf, TCL_STATIC);
  return TCL_OK;
}

static int Box2DSetBeginContactCallbackCmd(ClientData clientData, Tcl_Interp *interp,
                                           int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;

  if (argc < 3)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world callback",
                     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;

  if (bw->beginContactCallback)
  {
    free(bw->beginContactCallback);
    bw->beginContactCallback = NULL;
  }

  if (strlen(argv[2]))
  {
    bw->beginContactCallback = strdup(argv[2]);
  }

  return TCL_OK;
}

static int Box2DSetEndContactCallbackCmd(ClientData clientData, Tcl_Interp *interp,
                                         int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;

  if (argc < 3)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world callback",
                     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;

  if (bw->endContactCallback)
  {
    free(bw->endContactCallback);
    bw->endContactCallback = NULL;
  }

  if (strlen(argv[2]))
  {
    bw->endContactCallback = strdup(argv[2]);
  }

  return TCL_OK;
}

static int Box2DSetPreSolveCallbackCmd(ClientData clientData, Tcl_Interp *interp,
                                       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *)clientData;
  BOX2D_WORLD *bw;

  if (argc < 3)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world callback",
                     NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;

  if (bw->preSolveCallback)
  {
    free(bw->preSolveCallback);
    bw->preSolveCallback = NULL;
  }

  if (strlen(argv[2]))
  {
    bw->preSolveCallback = strdup(argv[2]);
  }

  return TCL_OK;
}

/***********************************************************************/
/**********************          Joints           **********************/
/***********************************************************************/

static int Box2DCreateRevoluteJointCmd(ClientData clientData, Tcl_Interp *interp,
                                       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;
  b2Body *bodyA;
  b2Body *bodyB;
  double anchorA_x, anchorA_y;
  double anchorB_x, anchorB_y;
  b2RevoluteJointDef revoluteJointDef;
  b2RevoluteJoint *joint;

  if (argc < 8)
  {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world bodyA bodyB anchorA_x anchorA_y",
                     "anchorB_x anchorB_y", NULL);
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1])))
    return TCL_ERROR;
  if (find_body(bw, argv[2], &bodyA) != TCL_OK)
    return TCL_ERROR;
  if (find_body(bw, argv[3], &bodyB) != TCL_OK)
    return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &anchorA_x) != TCL_OK)
    return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &anchorA_y) != TCL_OK)
    return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[6], &anchorB_x) != TCL_OK)
    return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[7], &anchorB_y) != TCL_OK)
    return TCL_ERROR;

  revoluteJointDef.Initialize(bodyA, bodyB, bodyA->GetWorldCenter());

  revoluteJointDef.collideConnected = false;
  revoluteJointDef.localAnchorA.Set(anchorA_x, anchorA_y);
  revoluteJointDef.localAnchorB.Set(anchorB_x, anchorB_y);

  revoluteJointDef.referenceAngle = 0;
  revoluteJointDef.enableLimit = true;
  revoluteJointDef.lowerAngle = -45 * (M_PI / 180.);
  revoluteJointDef.upperAngle = 45 * (M_PI / 180.);

  revoluteJointDef.enableMotor = false;
  revoluteJointDef.maxMotorTorque = 20;
  revoluteJointDef.motorSpeed = 360 * (M_PI / 180.);

  joint = (b2RevoluteJoint *)bw->bWorld->CreateJoint(&revoluteJointDef);

  return (TCL_OK);
}

static void Box2D_update_link (b2Body* body,
			       float x, float y, float angle)
{
  BOX2D_USERDATA *userdata;

  userdata = (BOX2D_USERDATA *) (body->GetUserData().pointer);

  if (!userdata->linked) return;
  if (userdata->linkid >= OL_NOBJS(userdata->olist)) {
    return;
  }

  if (!userdata->matrix) {
    userdata->matrix = GR_MATRIX(OL_OBJ(userdata->olist,userdata->linkid));
  }
  
  matrix4_set_translation_angle(userdata->matrix, x, y, angle);
}

static void Box2D_free_userdata (b2Body* body)
{
  BOX2D_USERDATA *userdata;
  userdata = (BOX2D_USERDATA *) (body->GetUserData().pointer);
  if (userdata) free(userdata);
}


static int Box2DLinkObjCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BOX2D_WORLD *bw;
  b2Body *body;
  int id;
  BOX2D_USERDATA *userdata;

  if (argc < 4) {
    interp->result = "usage: Box2D_linkObj world body linkobj";
    return TCL_ERROR;
  }

  if (!(bw = find_Box2D(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(bw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &id) != TCL_OK) return TCL_ERROR;

  userdata = (BOX2D_USERDATA *)  (body->GetUserData().pointer);
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
EXPORT(int,Box_Init) _ANSI_ARGS_((Tcl_Interp *interp))
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
      Tcl_InitStubs(interp, "8.5", 0)
#else
      Tcl_PkgRequire(interp, "Tcl", "8.5", 0)
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
  Tcl_CreateCommand(interp, "Box2D_createBody", 
		    (Tcl_CmdProc *) Box2DCreateBodyCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "Box2D_createBoxFixture", 
		    (Tcl_CmdProc *) Box2DCreateBoxFixtureCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "Box2D_createCircleFixture", 
		    (Tcl_CmdProc *) Box2DCreateCircleFixtureCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "Box2D_createRevoluteJoint",
                    (Tcl_CmdProc *)Box2DCreateRevoluteJointCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);

  Tcl_CreateCommand(interp, "Box2D_applyForce",
                    (Tcl_CmdProc *)Box2DApplyForceCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateCommand(interp, "Box2D_applyLinearImpulse",
                    (Tcl_CmdProc *)Box2DApplyLinearImpulseCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateCommand(interp, "Box2D_setTransform",
                    (Tcl_CmdProc *)Box2DSetTransformCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);

  Tcl_CreateCommand(interp, "Box2D_setBeginContactCallback",
                    (Tcl_CmdProc *)Box2DSetBeginContactCallbackCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateCommand(interp, "Box2D_setEndContactCallback",
                    (Tcl_CmdProc *)Box2DSetEndContactCallbackCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateCommand(interp, "Box2D_setPreSolveCallback",
                    (Tcl_CmdProc *)Box2DSetPreSolveCallbackCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);

  Tcl_CreateCommand(interp, "Box2D_getBodyInfo",
                    (Tcl_CmdProc *)Box2DGetBodyInfoCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateCommand(interp, "Box2D_getBodies",
                    (Tcl_CmdProc *)Box2DGetBodiesCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);

  Tcl_CreateCommand(interp, "Box2D_setFilterData",
                    (Tcl_CmdProc *)Box2DSetFilterDataCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateCommand(interp, "Box2D_setSensor",
                    (Tcl_CmdProc *)Box2DSetSensorCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);

  Tcl_CreateCommand(interp, "Box2D_setBodyType",
                    (Tcl_CmdProc *)Box2DSetBodyTypeCmd,
                    (ClientData) OBJList, (Tcl_CmdDeleteProc *)NULL);

  Tcl_CreateCommand(interp, "Box2D_update", 
		    (Tcl_CmdProc *) Box2DUpdateCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);


  Tcl_CreateCommand(interp, "Box2D_linkObj", 
		    (Tcl_CmdProc *) Box2DLinkObjCmd, 
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




