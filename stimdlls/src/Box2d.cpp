/*
 * Box2D.cpp
 *  Use the Box2D library to simulate 2D physics
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
#include <Box2D.h>

static Tcl_Interp *OurInterp;
static int Box2DID = -1;	/* unique box2d object id */

struct box2d_world;		/* for circular reference */

typedef struct box2d_world {
  Tcl_Interp *interp;
  NewtonWorld *nWorld;

  int collisionCount;
  Tcl_HashTable collisionTable;

  int bodyCount;
  Tcl_HashTable bodyTable;

  int jointCount;
  Tcl_HashTable jointTable;

  Tcl_HashTable effectsTable;

  int time;
  int lasttime;

  SPECIAL_EFFECT *current_effect;

} NEWTON_WORLD;

typedef struct newton_userdata {
  NEWTON_WORLD *world;
  char name[32];
  OBJ_LIST *olist;
  int linkid;
  float *matrix;
  float gravity;
  float force_vector[3];
  float torque_vector[3];
} NEWTON_USERDATA;


static void newton_free_userdata (const NewtonBody* body);
static void newton_apply_force_and_torque (const NewtonBody* body, dFloat, int);
static void newton_update_link (const NewtonBody* body, const float* matrix);

/***********************************************************************/
/**********************      Helper Functions     **********************/
/***********************************************************************/

static int find_matrix4(Tcl_Interp *interp, char *name, float *m);
static int find_vec_3(Tcl_Interp *interp, char *name, float *v);
static int find_vec_4(Tcl_Interp *interp, char *name, float *v);

static void matrix4_identity(float *mat);
static void matrix4_set_translation(float *mat, float x, float y, float z);
static void matrix4_add_translation(float *mat, float x, float y, float z);
static void matrix4_get_translation(float *mat, 
				    float *x, float *y, float *z);
static void matrix4_set_scale(float *mat, float x, float y, float z);



enum TRANS_TYPE { TRANS_ADD, TRANS_SET } ;

static NEWTON_WORLD *find_world(Tcl_Interp *interp, 
				 OBJ_LIST *olist, char *idstring);
static int find_collision(NEWTON_WORLD *newtonObj, 
			  char *name, NewtonCollision **c);
static int find_body(NEWTON_WORLD *newtonObj, 
		     char *name, NewtonBody **b);
static int newton_add_collision(NEWTON_WORLD *nw, NewtonCollision *c);
static int newton_add_joint(NEWTON_WORLD *nw, NewtonJoint *j);


static NEWTON_WORLD *find_newton(Tcl_Interp *interp, 
				 OBJ_LIST *olist, char *idstring)
{
  int id;
  
  if (Tcl_GetInt(interp, idstring, &id) != TCL_OK) return NULL;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, "objid out of range", NULL);
    return NULL;
  }
  
  /* Make sure it's a newton object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != NewtonID) {
    Tcl_AppendResult(interp, "object not a newton world", NULL);
    return NULL;
  }
  return (NEWTON_WORLD *) GR_CLIENTDATA(OL_OBJ(olist,id));
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


static int find_body(NEWTON_WORLD *nw, char *name, NewtonBody **b)
{
  NewtonBody *body;
  Tcl_HashEntry *entryPtr;

  if ((entryPtr = Tcl_FindHashEntry(&nw->bodyTable, name))) {
    body = (NewtonBody *) Tcl_GetHashValue(entryPtr);
    if (!body) {
      Tcl_SetResult(nw->interp, "bad body ptr in hash table", TCL_STATIC);
      return TCL_ERROR;
    }
    if (b) *b = body;
    return TCL_OK;
  }
  else {
    if (b) {			/* If img was null, then don't set error */
      Tcl_AppendResult(nw->interp, "body \"", name, "\" not found", 
		       (char *) NULL);
    }
    return TCL_ERROR;
  }
}

static int find_collision(NEWTON_WORLD *nw, char *name, NewtonCollision **c)
{
  NewtonCollision *collision;
  Tcl_HashEntry *entryPtr;

  if ((entryPtr = Tcl_FindHashEntry(&nw->collisionTable, name))) {
    collision = (NewtonCollision *) Tcl_GetHashValue(entryPtr);
    if (!collision) {
      Tcl_SetResult(nw->interp, "bad collision ptr in hash table", TCL_STATIC);
      return TCL_ERROR;
    }
    if (c) *c = collision;
    return TCL_OK;
  }
  else {
    if (c) {			/* If img was null, then don't set error */
      Tcl_AppendResult(nw->interp, "collision \"", name, "\" not found", 
		       (char *) NULL);
    }
    return TCL_ERROR;
  }
}

static int find_joint(NEWTON_WORLD *nw, char *name, NewtonJoint **j)
{
  NewtonJoint *joint;
  Tcl_HashEntry *entryPtr;

  if ((entryPtr = Tcl_FindHashEntry(&nw->jointTable, name))) {
    joint = (NewtonJoint *) Tcl_GetHashValue(entryPtr);
    if (!joint) {
      Tcl_SetResult(nw->interp, "bad joint ptr in hash table", TCL_STATIC);
      return TCL_ERROR;
    }
    if (j) *j = joint;
    return TCL_OK;
  }
  else {
    if (j) {			/* If was null, then don't set error */
      Tcl_AppendResult(nw->interp, "joint \"", name, "\" not found", 
		       (char *) NULL);
    }
    return TCL_ERROR;
  }
}

static int find_effect(NEWTON_WORLD *nw, char *name, SPECIAL_EFFECT **e)
{
  SPECIAL_EFFECT *effect;
  Tcl_HashEntry *entryPtr;

  if ((entryPtr = Tcl_FindHashEntry(&nw->effectsTable, name))) {
    effect = (SPECIAL_EFFECT *) Tcl_GetHashValue(entryPtr);
    if (!effect) {
      Tcl_SetResult(nw->interp, "bad effect ptr in hash table", TCL_STATIC);
      return TCL_ERROR;
    }
    if (e) *e = effect;
    return TCL_OK;
  }
  else {
    if (e) {			/* If img was null, then don't set error */
      Tcl_AppendResult(nw->interp, "effect \"", name, "\" not found", 
		       (char *) NULL);
    }
    return TCL_ERROR;
  }
}


static int newton_add_collision(NEWTON_WORLD *nw, NewtonCollision *c)
{
  char collision_name[32];
  Tcl_HashEntry *entryPtr;
  int newentry;

  sprintf(collision_name, "collision%d", nw->collisionCount++); 
  entryPtr = 
    Tcl_CreateHashEntry(&nw->collisionTable, collision_name, &newentry);
  Tcl_SetHashValue(entryPtr, c);
  Tcl_SetResult(nw->interp, collision_name, TCL_VOLATILE);
  return TCL_OK;
}


static int newton_add_joint(NEWTON_WORLD *nw, NewtonJoint *j)
{
  char joint_name[32];
  Tcl_HashEntry *entryPtr;
  int newentry;

  sprintf(joint_name, "joint%d", nw->jointCount++); 
  entryPtr = 
    Tcl_CreateHashEntry(&nw->jointTable, joint_name, &newentry);
  Tcl_SetHashValue(entryPtr, j);
  Tcl_SetResult(nw->interp, joint_name, TCL_VOLATILE);
  return TCL_OK;
}

/***********************************************************************/
/**********************      Newton OBJ Funcs     **********************/
/***********************************************************************/

static int newtonUpdate(GR_OBJ *g)
{
  NEWTON_WORLD *nw = (NEWTON_WORLD *) GR_CLIENTDATA(g);
  NewtonWorld *nWorld = nw->nWorld;
  float elapsed;

  nw->time = getStimTime();
  //  elapsed = (nw->time-nw->lasttime)/1000.;
  elapsed = getFrameDuration()/1000.;
  nw->lasttime = nw->time;

  NewtonUpdate (nw->nWorld, elapsed);
  return(TCL_OK);
}

static void newtonDelete(GR_OBJ *g) 
{
  NEWTON_WORLD *nw = (NEWTON_WORLD *) GR_CLIENTDATA(g);
  NewtonCollision *collision;
  Tcl_HashSearch search;
  Tcl_HashEntry *entryPtr;
  SPECIAL_EFFECT *effect;

  /* destroy all the collisions in the table */
  for (entryPtr = Tcl_FirstHashEntry(&nw->collisionTable, &search);
       entryPtr != NULL;
       entryPtr = Tcl_NextHashEntry(&search)) {
    collision = (NewtonCollision *) Tcl_GetHashValue(entryPtr);
    NewtonDestroyCollision(collision);
  }

  Tcl_DeleteHashTable(&nw->collisionTable);
  Tcl_DeleteHashTable(&nw->jointTable);
  Tcl_DeleteHashTable(&nw->bodyTable);

  /* remove any allocated effects structures */
  for (entryPtr = Tcl_FirstHashEntry(&nw->effectsTable, &search);
       entryPtr != NULL;
       entryPtr = Tcl_NextHashEntry(&search)) {
    effect = (SPECIAL_EFFECT *) Tcl_GetHashValue(entryPtr);
    if (effect->script) free(effect->script);
    free(effect);
  }
  
  Tcl_DeleteHashTable(&nw->effectsTable);
  
  NewtonDestroyAllBodies(nw->nWorld);
  NewtonMaterialDestroyAllGroupID(nw->nWorld);
  NewtonDestroy(nw->nWorld);
  free((void *) nw);
}

static int newtonReset(GR_OBJ *g)
{
  NEWTON_WORLD *nw = (NEWTON_WORLD *) GR_CLIENTDATA(g);
  NewtonWorld *nWorld = nw->nWorld;
  nw->lasttime = nw->time = 0;
  return(TCL_OK);
}

static int newtonCmd(ClientData clientData, Tcl_Interp *interp,
		    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *obj;
  static const char *name = "Newton";
  NEWTON_WORLD *nw;

  obj = gobjCreateObj();
  if (!obj) return -1;

  GR_OBJTYPE(obj) = NewtonID;
  strcpy(GR_NAME(obj), name);

  nw = (NEWTON_WORLD *) calloc(1, sizeof(NEWTON_WORLD));

  nw->nWorld = NewtonCreate();
  if (!nw->nWorld) {
    Tcl_AppendResult(interp, argv[0], ": error creating newton world", NULL);
    free(nw);
    return TCL_ERROR;
  }
  nw->interp = interp;

  Tcl_InitHashTable(&nw->bodyTable, TCL_STRING_KEYS);
  Tcl_InitHashTable(&nw->collisionTable, TCL_STRING_KEYS);
  Tcl_InitHashTable(&nw->jointTable, TCL_STRING_KEYS);
  Tcl_InitHashTable(&nw->effectsTable, TCL_STRING_KEYS);

  GR_CLIENTDATA(obj) = nw;
  GR_DELETEFUNCP(obj) = newtonDelete;
  GR_RESETFUNCP(obj) = (RESET_FUNC) newtonReset;
  GR_UPDATEFUNCP(obj) = (UPDATE_FUNC) newtonUpdate;

  sprintf(interp->result, "%d", gobjAddObj(olist, obj));
  return(TCL_OK);
}

/***********************************************************************/
/**********************      Tcl Bound Funcs     ***********************/
/***********************************************************************/

static int newtonUpdateCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  double elapsed;

  if (argc < 3) {
    interp->result = "usage: newton_update world elapsed";
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &elapsed) != TCL_OK) return TCL_ERROR;

  nw->lasttime = nw->time;
  nw->time += (int) (elapsed*1000);

  NewtonUpdate (nw->nWorld, elapsed);
  return(TCL_OK);

}

static int newtonCreateNullCmd(ClientData clientData, Tcl_Interp *interp,
				    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonCollision *collision;

  if (argc < 2) {
    interp->result = "usage: newton_createNull world";
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  collision = NewtonCreateNull(nw->nWorld);

  return(newton_add_collision(nw, collision));
}

static int newtonCreateSphereCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  NEWTON_WORLD *nw;
  NewtonCollision *collision;
  double radius;
  OBJ_LIST *olist = (OBJ_LIST *) clientData;

  if (argc < 3) {
    interp->result = "usage: newton_createSphere world radius ?offset?";
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &radius) != TCL_OK) return TCL_ERROR;

  if (argc > 3) {
    float offset[16];
    if (find_matrix4(interp, argv[3], offset) != TCL_OK) return TCL_ERROR;
    collision = NewtonCreateSphere(nw->nWorld, radius, 0, offset);
  }
  else {
    collision = NewtonCreateSphere(nw->nWorld, radius, 0, NULL);
  }

  return(newton_add_collision(nw, collision));
}

static int newtonCreateBoxCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  NEWTON_WORLD *nw;
  NewtonCollision *collision;
  double sx, sy, sz;
  OBJ_LIST *olist = (OBJ_LIST *) clientData;

  if (argc < 5) {
    interp->result = "usage: newton_createBox world sx sy sz ?offset?";
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &sx) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &sy) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &sz) != TCL_OK) return TCL_ERROR;

  if (argc > 5) {
    float offset[16];
    if (find_matrix4(interp, argv[5], offset) != TCL_OK) return TCL_ERROR;
    collision = NewtonCreateBox(nw->nWorld, sx, sy, sz, 0, offset);
  }
  else {
    collision = NewtonCreateBox(nw->nWorld, sx, sy, sz, 0, NULL);
  }

  return(newton_add_collision(nw, collision));
}


static int newtonCreateCapsuleCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  NEWTON_WORLD *nw;
  NewtonCollision *collision;
  double r0, r1, height;
  OBJ_LIST *olist = (OBJ_LIST *) clientData;

  if (argc < 5) {
    interp->result = "usage: newton_createCapsule world r0 r1 height ?offset?";
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &r0) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &r1) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &height) != TCL_OK) return TCL_ERROR;

  if (argc > 5) {
    float offset[16];
    if (find_matrix4(interp, argv[5], offset) != TCL_OK) return TCL_ERROR;
    collision = NewtonCreateCapsule(nw->nWorld, r0, r1, height, 0, offset);
  }
  else {
    collision = NewtonCreateCapsule(nw->nWorld, r0, r1, height, 0, NULL);
  }

  return(newton_add_collision(nw, collision));
}

static int newtonCreateCylinderCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  NEWTON_WORLD *nw;
  NewtonCollision *collision;
  double r0, r1, height;
  OBJ_LIST *olist = (OBJ_LIST *) clientData;

  if (argc < 5) {
    interp->result = "usage: newton_createCylinder world r0 r1 height ?offset?";
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &r0) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &r1) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &height) != TCL_OK) return TCL_ERROR;

  if (argc > 5) {
    float offset[16];
    if (find_matrix4(interp, argv[5], offset) != TCL_OK) return TCL_ERROR;
    collision = NewtonCreateCylinder(nw->nWorld, r0, r1, height, 0, offset);
  }
  else {
    collision = NewtonCreateCylinder(nw->nWorld, r0, r1, height, 0, NULL);
  }

  return(newton_add_collision(nw, collision));
}


static int newtonCreateConeCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  NEWTON_WORLD *nw;
  NewtonCollision *collision;
  double radius, height;
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  
  if (argc < 4) {
    interp->result = "usage: newton_createCone world radius height ?offset?";
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &radius) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &height) != TCL_OK) return TCL_ERROR;

  if (argc > 4) {
    float offset[16];
    if (find_matrix4(interp, argv[4], offset) != TCL_OK) return TCL_ERROR;
    collision = NewtonCreateCone(nw->nWorld, radius, height, 0, offset);
  }
  else {
    collision = NewtonCreateCone(nw->nWorld, radius, height, 0, NULL);
  }

  return(newton_add_collision(nw, collision));
}


static int newtonCreateChamferCylinderCmd(ClientData clientData, 
					  Tcl_Interp *interp,
					  int argc, char *argv[])
{
  NEWTON_WORLD *nw;
  NewtonCollision *collision;
  double radius, height;
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  
  if (argc < 4) {
    interp->result = 
      "usage: newton_createChamferCylinder world radius height ?offset?";
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &radius) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &height) != TCL_OK) return TCL_ERROR;
  
  if (argc > 4) {
    float offset[16];
    if (find_matrix4(interp, argv[4], offset) != TCL_OK) return TCL_ERROR;
    collision = NewtonCreateChamferCylinder(nw->nWorld, 
					    radius, height, 0, offset);
  }
  else {
    collision = NewtonCreateChamferCylinder(nw->nWorld, radius, height, 0, NULL);
  }

  return(newton_add_collision(nw, collision));
}




static int newtonCreateHeightFieldCollisionCmd(ClientData clientData, 
					  Tcl_Interp *interp,
					  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonCollision *collision;
  int width, height;
  double hscale_x, hscale_z, vscale;
  DYN_LIST *dl;
  int grids_diag = 1;
  char *attribs = NULL;
  int datatype;
  unsigned short *short_vals = NULL;
  float *float_vals = NULL;
  int shapeID = 0;
  
  /*
    NewtonCollision* NewtonCreateHeightFieldCollision (
    const NewtonWorld* newtonWorld, 
    int width, int height, int gridsDiagonals, 
    unsigned short* elevationMap, 
    char* attributeMap, 
    dFloat verticalScale,
    dFloat horizontalScale_x, dFloat horizontalScale_z, int shapeID);
  */
  
  if (argc < 8) {
    interp->result = 
      "usage: newton_createHeightFieldCollision world width height map vscale hscale_x hscale_z [shapeID]";
    return TCL_ERROR;
    }
  
  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &width) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &height) != TCL_OK) return TCL_ERROR;
  if (tclFindDynList(interp, argv[4], &dl) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &vscale) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[6], &hscale_x) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[7], &hscale_z) != TCL_OK) return TCL_ERROR;

  if (argc > 8)
    if (Tcl_GetInt(interp, argv[8], &shapeID) != TCL_OK) return TCL_ERROR;
  
  if (DYN_LIST_DATATYPE(dl) == DF_SHORT) {
    datatype = 1;
  }
  else if (DYN_LIST_DATATYPE(dl) == DF_FLOAT) {
    datatype = 0;
  }
  else {
    Tcl_AppendResult(interp, argv[0],
		     ": heightmap data not shorts (use dl_short to cast)",
		     (char *) NULL);
      return TCL_ERROR;
  }

  if (width*height != DYN_LIST_N(dl)) {
    Tcl_AppendResult(interp, argv[0],
		     ": length of heightmap data does not match width x height",
		     (char *) NULL);
    return TCL_ERROR;
  }
  
  attribs = (char *) malloc (width * height * sizeof (char));
  memset (attribs, 0, width * height * sizeof (char));

  // m_elevationDataType;  0 = 32 bit floats, 1 = unsigned 16 bit integers
  
  collision = NewtonCreateHeightFieldCollision(nw->nWorld, width, height,
					       grids_diag, datatype,
					       DYN_LIST_VALS(dl),
					       attribs, vscale,
					       hscale_x, hscale_z, shapeID);
  free(attribs);
  
  return(newton_add_collision(nw, collision));
}




static int newtonCreateConvexHullCmd(ClientData clientData, 
				     Tcl_Interp *interp,
				     int argc, char *argv[])
{
  NEWTON_WORLD *nw;
  NewtonCollision *collision;
  DYN_LIST *dl; 
  float offset[16];
  float *off = NULL;
  float tolerance = 0.01;
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  
  if (argc < 3) {
    interp->result = 
      "usage: newton_createConvexHull world verts ?offset?";
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (tclFindDynList(interp, argv[2], &dl) != TCL_OK) {
    return TCL_ERROR;
  }

  if (DYN_LIST_DATATYPE(dl) != DF_FLOAT ||
      DYN_LIST_N(dl) %3 != 0) {
    Tcl_AppendResult(interp, argv[0], ": invalid vertex list",
		     (char *) NULL);
    return TCL_ERROR;
  }

  if (argc > 3) {
    if (find_matrix4(interp, argv[3], offset) != TCL_OK) return TCL_ERROR;
    off = offset;
  }
  
  collision = NewtonCreateConvexHull(nw->nWorld, DYN_LIST_N(dl)/3,
				     (float *) DYN_LIST_VALS(dl), 
				     12, tolerance, 0, off);

  return(newton_add_collision(nw, collision));
}

static int newtonCreateBodyCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NEWTON_USERDATA *userdata;
  NewtonBody *body;
  NewtonCollision *collision;
  char body_name[32];
  Tcl_HashEntry *entryPtr;
  int newentry;

  // Neutral transform matrix.
  float	tm[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
  };
  
  if (argc < 3) {
    interp->result = "usage: newton_createBody world collision";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_collision(nw, argv[2], &collision) != TCL_OK) return TCL_ERROR;
  body = NewtonCreateDynamicBody(nw->nWorld, collision, tm);

  userdata = (NEWTON_USERDATA *) calloc(1, sizeof(NEWTON_USERDATA));
  userdata->world = nw;
  userdata->gravity = -9.8;	/* standard gravitational force */
  NewtonBodySetUserData(body, userdata);

  NewtonBodySetDestructorCallback (body, newton_free_userdata);
    
  sprintf(body_name, "body%d", nw->bodyCount++); 
  strcpy(userdata->name, body_name);

  entryPtr = Tcl_CreateHashEntry(&nw->bodyTable, body_name, &newentry);
  Tcl_SetHashValue(entryPtr, body);

  Tcl_SetResult(interp, body_name, TCL_VOLATILE);
  return(TCL_OK);
}

static int newtonBodySetSimulationStateCmd(ClientData clientData,
					   Tcl_Interp *interp,
					   int argc, char *argv[])
{
  NEWTON_WORLD *nw;
  NewtonBody *body;
  int state;
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  
  if (argc < 4) {
    interp->result = "usage: newton_bodySetSimulationState world body state";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &state) != TCL_OK) return TCL_ERROR;
  
  NewtonBodySetSimulationState(body, state);

  return TCL_OK;
}

#ifdef SET_RELEASE_COLLISION
static int newtonBodySetCollisionCmd(ClientData clientData, Tcl_Interp *interp,
				int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonBody *body;
  NewtonCollision *collision;

  if (argc < 4) {
    interp->result = "usage: newton_bodySetCollision world body collision";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (find_collision(nw, argv[3], &collision) != TCL_OK) return TCL_ERROR;

  NewtonBodySetCollision(body, collision);

  return TCL_OK;
}


static int newtonReleaseCollisionCmd(ClientData clientData, Tcl_Interp *interp,
				     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonCollision *collision;
  Tcl_HashEntry *entryPtr;
 
  if (argc < 3) {
    interp->result = "usage: newton_releaseCollision world collision";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if ((entryPtr = Tcl_FindHashEntry(&nw->collisionTable, argv[2]))) {
    collision = (NewtonCollision *) Tcl_GetHashValue(entryPtr);
    NewtonDestroyCollision(collision);
    Tcl_DeleteHashEntry(entryPtr);
  }
  return TCL_OK;
}

static int newtonBodySetCollidableCmd(ClientData clientData, Tcl_Interp *interp,
				      int argc, char *argv[])
{
  NEWTON_WORLD *nw;
  NewtonBody *body;
  int collidable;
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  
  if (argc < 4) {
    interp->result = "usage: newton_bodySetCollidable world body collidable";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &collidable) != TCL_OK) return TCL_ERROR;
  
  NewtonBodySetCollidable(body, collidable);

  return TCL_OK;
}
#endif



static int newtonBodySetMatrixCmd(ClientData clientData, Tcl_Interp *interp,
				     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonBody *body;
  static float matrix[16];

  if (argc < 4) {
    interp->result = "usage: newton_bodySetMatrix world body matrix";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (find_matrix4(interp, argv[3], matrix) != TCL_OK) return TCL_ERROR;

  NewtonBodySetMatrix(body, matrix);

  return TCL_OK;
}

static int newtonBodySetMaterialGroupIDCmd(ClientData clientData, 
					   Tcl_Interp *interp,
					   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonBody *body;
  int groupid;

  if (argc < 4) {
    interp->result = "usage: newton_bodySetMatrix world body groupid";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &groupid) != TCL_OK) return TCL_ERROR;

  NewtonBodySetMaterialGroupID(body, groupid);

  return TCL_OK;
}

static int newtonBodySetLinearDampingCmd(ClientData clientData, 
					 Tcl_Interp *interp,
					 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonBody *body;
  double linearDamp;

  if (argc < 4) {
    interp->result = "usage: newton_bodySetLinearDamping world body linearDamp";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &linearDamp) != TCL_OK) return TCL_ERROR;

  NewtonBodySetLinearDamping(body, linearDamp);

  return TCL_OK;
}

static int newtonBodySetAngularDampingCmd(ClientData clientData, 
					 Tcl_Interp *interp,
					 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonBody *body;
  double angularDamp;

  if (argc < 4) {
    interp->result = "usage: newton_bodySetAngularDamping world body angularDamp";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &angularDamp) != TCL_OK) return TCL_ERROR;

  NewtonBodySetLinearDamping(body, angularDamp);

  return TCL_OK;
}


static void newton_apply_force_and_torque (const NewtonBody* body,
					   dFloat timestep,
					   int threadIndex)
{
  NEWTON_USERDATA *userdata;
  static float force_vec[3];
  float mass, Ixx, Iyy, Izz;

  userdata = (NEWTON_USERDATA *) NewtonBodyGetUserData (body);

  NewtonBodyGetMass(body, &mass, &Ixx, &Iyy, &Izz);
  force_vec[0] = 0.;
  force_vec[1] = (mass)*userdata->gravity;
  force_vec[2] = 0.;
  NewtonBodySetForce(body, (float *) &force_vec);
  NewtonBodyAddForce(body, userdata->force_vector);
}

static void newton_update_link (const NewtonBody* body, const float* matrix,
				int threadIndex)
{
  NEWTON_USERDATA *userdata;

  userdata = (NEWTON_USERDATA *) NewtonBodyGetUserData (body);
  if (userdata->linkid >= OL_NOBJS(userdata->olist)) {
    return;
  }
  if (!userdata->matrix) {
    userdata->matrix = GR_MATRIX(OL_OBJ(userdata->olist,userdata->linkid));
  }

  memcpy(userdata->matrix, matrix, sizeof(float)*16);
}

static void newton_free_userdata (const NewtonBody* body)
{
  NEWTON_USERDATA *userdata;
  userdata = (NEWTON_USERDATA *) NewtonBodyGetUserData (body);
  free(userdata);
}


static int newtonLinkObjCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonBody *body;
  int id;
  NEWTON_USERDATA *userdata;

  if (argc < 4) {
    interp->result = "usage: newton_linkObj world body linkobj";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &id) != TCL_OK) return TCL_ERROR;

  userdata = (NEWTON_USERDATA *) NewtonBodyGetUserData(body);
  userdata->linkid = id;
  userdata->olist = olist;

  if (id < OL_NOBJS(olist)) {
    userdata->matrix = GR_MATRIX(OL_OBJ(olist,id));
  }
  else {
    userdata->matrix = NULL;
  }

  NewtonBodySetTransformCallback (body,
				  (NewtonSetTransform) newton_update_link);

  return TCL_OK;
}


static int newtonSetupForceAndTorqueCmd(ClientData clientData, 
					Tcl_Interp *interp,
					int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonBody *body;

  if (argc < 3) {
    interp->result = "usage: newton_setupForceAndTorque world body";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;
  NewtonBodySetForceAndTorqueCallback (body,
				       (NewtonApplyForceAndTorque)
				       newton_apply_force_and_torque);

  return TCL_OK;
}


static int newtonBodySetMassMatrixCmd(ClientData clientData, 
				      Tcl_Interp *interp,
				      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonBody *body;
  double mass, Ixx, Iyy, Izz;

  if (argc < 7) {
    interp->result = 
      "usage: newton_bodySetMassMatrix world body mass Ixx Iyy Izz";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;

  if (Tcl_GetDouble(interp, argv[3], &mass) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &Ixx) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &Iyy) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[6], &Izz) != TCL_OK) return TCL_ERROR;
  
  NewtonBodySetMassMatrix (body, mass, Ixx, Iyy, Izz);
  return TCL_OK;
}



static int newtonBodySetGravityCmd(ClientData clientData, 
				   Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonBody *body;
  NEWTON_USERDATA *userdata;
  double gravity;

  if (argc != 4 && argc != 3) {
    interp->result = 
      "usage: newton_bodySetGravity world body [gravity]";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;

  if (argc == 3) {
    userdata = (NEWTON_USERDATA *) NewtonBodyGetUserData (body);
    sprintf(interp->result, "%f",  userdata->gravity);
    return TCL_OK;
  }
  
  if (Tcl_GetDouble(interp, argv[3], &gravity) != TCL_OK) return TCL_ERROR;

  userdata = (NEWTON_USERDATA *) NewtonBodyGetUserData (body);
  userdata->gravity = gravity;

  return TCL_OK;
}


static int newtonBodySetForceVectorCmd(ClientData clientData, 
				       Tcl_Interp *interp,
				       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  NewtonBody *body;
  NEWTON_USERDATA *userdata;
  double Ixx, Iyy, Izz;

  if (argc != 6 && argc != 3) {
    interp->result = 
      "usage: newton_bodySetForceVector world body [Ixx Iyy Izz]";
    return TCL_ERROR;
  }

  if (!(nw = find_newton(interp, olist, argv[1]))) return TCL_ERROR;
  if (find_body(nw, argv[2], &body) != TCL_OK) return TCL_ERROR;

  if (argc == 3) {
    userdata = (NEWTON_USERDATA *) NewtonBodyGetUserData (body);
    sprintf(interp->result, "%f %f %f",  
	    userdata->force_vector[0],
	    userdata->force_vector[1],
	    userdata->force_vector[2]);
    return TCL_OK;
  }
  
  if (Tcl_GetDouble(interp, argv[3], &Ixx) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &Iyy) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &Izz) != TCL_OK) return TCL_ERROR;

  userdata = (NEWTON_USERDATA *) NewtonBodyGetUserData (body);
  userdata->force_vector[0] = Ixx;
  userdata->force_vector[1] = Iyy;
  userdata->force_vector[2] = Izz;

  return TCL_OK;
}



static int newtonMaterialCreateGroupIDCmd(ClientData clientData, 
					  Tcl_Interp *interp,
					  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  int id;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world",
		     (char *) NULL);
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1])))
    return TCL_ERROR;
  
  id = NewtonMaterialCreateGroupID(nw->nWorld);
  sprintf(interp->result, "%d", id);
  return TCL_OK;
}

static int newtonMaterialSetDefaultFrictionCmd(ClientData clientData, 
					       Tcl_Interp *interp,
					       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  int id0, id1;
  double staticFriction, kineticFriction;

  if (argc < 6) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world id0 id1 staticFriction kineticFriction",
		     (char *) NULL);
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1])))
    return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &id0) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &id1) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &staticFriction) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &kineticFriction) != TCL_OK) return TCL_ERROR;

  NewtonMaterialSetDefaultFriction(nw->nWorld, id0, id1, staticFriction, kineticFriction);

  return TCL_OK;
}


static int newtonMaterialSetDefaultElasticityCmd(ClientData clientData, 
						 Tcl_Interp *interp,
						 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  int id0, id1;
  double elasticCoef;

  if (argc < 5) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world id0 id1 elasticCoef",
		     (char *) NULL);
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1])))
    return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &id0) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &id1) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &elasticCoef) != TCL_OK) return TCL_ERROR;

  NewtonMaterialSetDefaultElasticity(nw->nWorld, id0, id1, elasticCoef);

  return TCL_OK;
}

static int newtonMaterialSetDefaultSoftnessCmd(ClientData clientData, 
					       Tcl_Interp *interp,
					       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  int id0, id1;
  double softnessCoef;

  if (argc < 5) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world id0 id1 softnessCoef",
		     (char *) NULL);
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1])))
    return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &id0) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &id1) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &softnessCoef) != TCL_OK) return TCL_ERROR;

  NewtonMaterialSetDefaultSoftness(nw->nWorld, id0, id1, softnessCoef);

  return TCL_OK;
}

static int newtonMaterialSetDefaultCollidableCmd(ClientData clientData, 
						 Tcl_Interp *interp,
						 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  int id0, id1, state;

  if (argc < 5) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world id0 id1 state",
		     (char *) NULL);
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1])))
    return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &id0) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &id1) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[4], &state) != TCL_OK) return TCL_ERROR;

  NewtonMaterialSetDefaultCollidable(nw->nWorld, id0, id1, state);

  return TCL_OK;
}


static int contact_begin_callback(NewtonMaterial *material,
				  NewtonBody* body0, NewtonBody* body1, int threadIndex)
{
  NEWTON_USERDATA *userdata;
  NEWTON_WORLD *world;

  userdata = (NEWTON_USERDATA *) NewtonBodyGetUserData (body0);
  world = userdata->world;

  world->current_effect = (SPECIAL_EFFECT *) NewtonMaterialGetMaterialPairUserData (material);
  world->current_effect->m_body0 = (NewtonBody*) body0;
  world->current_effect->m_body1 = (NewtonBody*) body1;

  /* clear the contact normal speed */
  world->current_effect->m_contactMaxNormalSpeed = 0.0f;
  
  /* clear the contact sliding speed */
  world->current_effect->m_contactMaxTangentSpeed = 0.0f;
  
  /* return one the tell Newton the application wants to process this contact */
  return 1;
}


static int  contact_process_callback (const NewtonJoint* contact_joint, 
				      const dFloat timestep, int threadIndex)
{
  NewtonBody *body0 = NewtonJointGetBody0(contact_joint);
  NewtonBody *body1 = NewtonJointGetBody1(contact_joint);

  NEWTON_USERDATA *userdata0 =
    (NEWTON_USERDATA *) NewtonBodyGetUserData (body0);
  NEWTON_USERDATA *userdata1 =
    (NEWTON_USERDATA *) NewtonBodyGetUserData (body1);
  NEWTON_WORLD *world = userdata0->world;

  int id0 = NewtonBodyGetMaterialGroupID(body0);
  int id1 = NewtonBodyGetMaterialGroupID(body1);
  
  float speed0;
  float speed1;
  float normal[3];
  SPECIAL_EFFECT *effect = 
    (SPECIAL_EFFECT *) NewtonMaterialGetUserData(world->nWorld, id0, id1);
  
  /* 
   * Get the maximum normal speed of this impact. 
   * This can be used for particles of playing collision sound 
   */
  auto contact = NewtonContactJointGetFirstContact(contact_joint);
  NewtonMaterial *material = NewtonContactGetMaterial(contact);

  effect->m_body0 = body0;
  effect->m_body1 = body1;
  
  speed0 = NewtonMaterialGetContactNormalSpeed (material);
  if (speed0 > effect->m_contactMaxNormalSpeed) {
    /* save the position of the contact (for 3d sound of particles effects) */
    effect->m_contactMaxNormalSpeed = speed0;
    NewtonMaterialGetContactPositionAndNormal (material, body0,
					       &effect->m_position[0], 
					       &normal[0]);
  }

  /* get the maximum of the two sliding contact speeds */
  speed0 = NewtonMaterialGetContactTangentSpeed (material, 0);
  speed1 = NewtonMaterialGetContactTangentSpeed (material, 1);
  if (speed1 > speed0) {
    speed0 = speed1;
  }

  /*
    Get the maximum tangent speed of this contact. 
    This can be used for particles(sparks) or playing scratch sounds 
  */
  if (speed0 > effect->m_contactMaxTangentSpeed) {

    /* save the position of the contact (for 3d sound of particles effects) */
    effect->m_contactMaxTangentSpeed = speed0;
    NewtonMaterialGetContactPositionAndNormal(material, body0, 
					      &effect->m_position[0], 
					      &normal[0]);
  }

  /* if the max contact speed is larger than some minimum value. play a sound */
  if (effect->m_contactMaxNormalSpeed > effect->m_contactScriptThreshold) {
    if (effect->script) {
      //      printf("evaluate script (%s %s)\n", effect->script, effect->name);
      Tcl_VarEval(effect->world->interp, effect->script, " ", 
		  effect->name, NULL);
    }
  }
  
  /* return one to tell Newton we want to accept this contact */
  return 1;
}

static int newtonMaterialSetCollisionCallbackCmd(ClientData clientData, 
						 Tcl_Interp *interp,
						 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  SPECIAL_EFFECT *effect;
  int id1, id2;
  char material_pair_name[32];
  Tcl_HashEntry *entryPtr;
  int newentry;
  double threshold = 1.0;

  if (argc < 5) {
    Tcl_AppendResult(interp, "usage: ", argv[0], 
		     " world id1 id2 script [contact_thresh]",
		     (char *) NULL);
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1])))
    return TCL_ERROR;
  
  if (Tcl_GetInt(interp, argv[2], &id1) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &id2) != TCL_OK) return TCL_ERROR;

  /* Parse threhold arg before allocating space for script */
  if (argc > 5) {
    if (Tcl_GetDouble(interp, argv[5], &threshold) != TCL_OK) return TCL_ERROR;
  }

  sprintf(material_pair_name, "%s-%s", argv[2], argv[3]);

  /* Remove previous effect entry if there was one */
  if ((entryPtr = Tcl_FindHashEntry(&nw->effectsTable, material_pair_name))) {
    effect = (SPECIAL_EFFECT *) Tcl_GetHashValue(entryPtr);
    free(effect);
  }

  /* Create a new special effect structure for these materials */
  effect = (SPECIAL_EFFECT *) calloc(1, sizeof(SPECIAL_EFFECT));
  effect->world = nw;
#ifdef _WIN32  
  effect->script = _strdup(argv[4]);
#else
  effect->script = strdup(argv[4]);
#endif  
  effect->m_contactScriptThreshold = threshold;
  strcpy(effect->name, material_pair_name);
  
  entryPtr = Tcl_CreateHashEntry(&nw->effectsTable, 
				 material_pair_name, &newentry);
  Tcl_SetHashValue(entryPtr, effect);

  NewtonMaterialSetCallbackUserData(nw->nWorld, id1, id2, effect);
  NewtonMaterialSetCollisionCallback(nw->nWorld, id1, id2, 
				     (NewtonOnAABBOverlap)
				     contact_begin_callback, 
				     (NewtonContactsProcess)
				     contact_process_callback);
  
  return TCL_OK;
}



static int newtonEffectGetBodiesCmd(ClientData clientData, 
				    Tcl_Interp *interp,
				    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  SPECIAL_EFFECT *effect;
  NEWTON_USERDATA *userdata0, *userdata1;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world effect",
		     (char *) NULL);
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1])))
    return TCL_ERROR;

  if (find_effect(nw, argv[2], &effect) != TCL_OK) return TCL_ERROR;

  userdata0 = (NEWTON_USERDATA *) NewtonBodyGetUserData (effect->m_body0);
  userdata1 = (NEWTON_USERDATA *) NewtonBodyGetUserData (effect->m_body1);
  
  Tcl_AppendResult(interp, userdata0->name, " ", userdata1->name,
		   (char *) NULL);
  return TCL_OK;
}

static int newtonEffectGetContactSpeedCmd(ClientData clientData, 
					  Tcl_Interp *interp,
					  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  SPECIAL_EFFECT *effect;
  static char speed_str[32];

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world effect",
		     (char *) NULL);
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1])))
    return TCL_ERROR;

  if (find_effect(nw, argv[2], &effect) != TCL_OK) return TCL_ERROR;

  sprintf(speed_str, "%.4f %.4f", 
	  effect->m_contactMaxNormalSpeed,
	  effect->m_contactMaxTangentSpeed);
  Tcl_AppendResult(interp, speed_str, (char *) NULL);
  return TCL_OK;
}


static int newtonEffectGetContactPointCmd(ClientData clientData, 
					  Tcl_Interp *interp,
					  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  SPECIAL_EFFECT *effect;
  static char point_str[32];

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world effect",
		     (char *) NULL);
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1])))
    return TCL_ERROR;

  if (find_effect(nw, argv[2], &effect) != TCL_OK) return TCL_ERROR;

  sprintf(point_str, "%.4f %.4f %.4f", 
	  effect->m_position[0],
	  effect->m_position[1],
	  effect->m_position[2]);
  Tcl_AppendResult(interp, point_str, (char *) NULL);
  return TCL_OK;
}


static int newtonEffectSetContactScriptThresholdCmd(ClientData clientData, 
						    Tcl_Interp *interp,
						    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  NEWTON_WORLD *nw;
  SPECIAL_EFFECT *effect;
  static char thresh_str[32];
  double thresh;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " world effect [threshold]",
		     (char *) NULL);
    return TCL_ERROR;
  }
  
  if (!(nw = find_newton(interp, olist, argv[1])))
    return TCL_ERROR;

  if (find_effect(nw, argv[2], &effect) != TCL_OK) return TCL_ERROR;

  sprintf(thresh_str, "%.4f", effect->m_contactScriptThreshold);

  if (argc > 3) {
    if (Tcl_GetDouble(interp, argv[3], &thresh) != TCL_OK) 
      return TCL_ERROR;
    effect->m_contactScriptThreshold = thresh;
  }
  
  Tcl_AppendResult(interp, thresh_str, (char *) NULL);
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
EXPORT(int,Newtonstim_Init) _ANSI_ARGS_((Tcl_Interp *interp))
#else
#ifdef __cplusplus
extern "C" {
#endif
  extern int Newtonstim_Init(Tcl_Interp *interp);
#ifdef __cplusplus
}
#endif
int Newtonstim_Init(Tcl_Interp *interp)
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
  
  if (NewtonID >= 0)		/* Already been here */
    return TCL_OK;
  
  NewtonID = gobjRegisterType();
  
  Tcl_CreateCommand(interp, "newton", 
		    (Tcl_CmdProc *) newtonCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_update", 
		    (Tcl_CmdProc *) newtonUpdateCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_createNull", 
		    (Tcl_CmdProc *) newtonCreateNullCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_createBox", 
		    (Tcl_CmdProc *) newtonCreateBoxCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_createSphere", 
		    (Tcl_CmdProc *) newtonCreateSphereCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_createCapsule", 
		    (Tcl_CmdProc *) newtonCreateCapsuleCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_createCone", 
		    (Tcl_CmdProc *) newtonCreateConeCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_createCylinder", 
		    (Tcl_CmdProc *) newtonCreateCylinderCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_createChamferCylinderCapsule", 
		    (Tcl_CmdProc *) newtonCreateChamferCylinderCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_createHeightFieldCollision", 
		    (Tcl_CmdProc *) newtonCreateHeightFieldCollisionCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_createConvexHull", 
		    (Tcl_CmdProc *) newtonCreateConvexHullCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_createBody", 
		    (Tcl_CmdProc *) newtonCreateBodyCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

#ifdef SET_RELEASE_COLLISION
  Tcl_CreateCommand(interp, "newton_bodySetCollision", 
		    (Tcl_CmdProc *) newtonBodySetCollisionCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_releaseCollision", 
		    (Tcl_CmdProc *) newtonReleaseCollisionCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_bodySetCollidable", 
		    (Tcl_CmdProc *) newtonBodySetCollidableCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
#endif
  

  Tcl_CreateCommand(interp, "newton_bodySetSimulationState", 
		    (Tcl_CmdProc *) newtonBodySetSimulationStateCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_bodySetMatrix", 
		    (Tcl_CmdProc *) newtonBodySetMatrixCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_bodySetMassMatrix", 
		    (Tcl_CmdProc *) newtonBodySetMassMatrixCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "newton_bodySetGravity", 
		    (Tcl_CmdProc *) newtonBodySetGravityCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "newton_bodySetForceVector", 
		    (Tcl_CmdProc *) newtonBodySetForceVectorCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_bodySetAngularDamping", 
		    (Tcl_CmdProc *) newtonBodySetAngularDampingCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "newton_bodySetLinearDamping", 
		    (Tcl_CmdProc *) newtonBodySetLinearDampingCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_bodySetMaterialGroupID", 
		    (Tcl_CmdProc *) newtonBodySetMaterialGroupIDCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_setupForceAndTorque", 
		    (Tcl_CmdProc *) newtonSetupForceAndTorqueCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_linkObj", 
		    (Tcl_CmdProc *) newtonLinkObjCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);


  Tcl_CreateCommand(interp, "newton_materialCreateGroupID", 
		    (Tcl_CmdProc *) newtonMaterialCreateGroupIDCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "newton_materialSetCollisionCallback", 
		    (Tcl_CmdProc *) newtonMaterialSetCollisionCallbackCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "newton_materialSetDefaultFriction", 
		    (Tcl_CmdProc *) newtonMaterialSetDefaultFrictionCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "newton_materialSetDefaultElasticity", 
		    (Tcl_CmdProc *) newtonMaterialSetDefaultElasticityCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "newton_materialSetDefaultSoftness", 
		    (Tcl_CmdProc *) newtonMaterialSetDefaultSoftnessCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "newton_materialSetDefaultCollidable", 
		    (Tcl_CmdProc *) newtonMaterialSetDefaultCollidableCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "newton_effectGetBodies",
		    (Tcl_CmdProc *) newtonEffectGetBodiesCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "newton_effectGetContactSpeed",
		    (Tcl_CmdProc *) newtonEffectGetContactSpeedCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "newton_effectGetContactPoint",
		    (Tcl_CmdProc *) newtonEffectGetContactPointCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "newton_effectSetContactScriptThresholdSpeed",
		    (Tcl_CmdProc *) newtonEffectSetContactScriptThresholdCmd, 
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




