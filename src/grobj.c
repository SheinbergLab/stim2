/*
 * NAME 
 *    grobj.c - generic object function definitions
 *
 * DESCRIPTION
 *    Functions to create and initialize graphics objects.
 *
 * AUTHOR
 *  DLS
 *
 */

#ifdef WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stim2.h"
#include "objname.h"
#include "animate.h"

static char *typenames[256];	/* for holding typenames of objects  */
static int ntypes = 0;		/* Number of currently defined types */

OBJ_LIST *getOBJList(void)
{
  return OBJList;
}

int gobjRegisterType(void)
{
  return ntypes++;		/* just keep returning unique id's */
}

static void gobjAddObjName(char *name, int type)
{
  if (typenames[type]) free(typenames[type]);
  typenames[type] = (char *) malloc(strlen(name)+1);
  strcpy(typenames[type], name);
}

char *gobjTypeName(int type)
{
  if (type > 255) return NULL;
  return typenames[type];
}

/********************************************************************
 * Function:     objListCreate
 * Returns:      OBJ_LIST *
 * Arguments:    None
 * Description:  Initialize a list of graphics objects
 ********************************************************************/

OBJ_LIST *objListCreate(void)
{
  OBJ_LIST *objlist = (OBJ_LIST *) calloc(1, sizeof(OBJ_LIST));
  if (!objlist) return(NULL);

  /* initialize space for mobj pointers */
  
  OL_MAXOBJS(objlist) = GR_DEFAULT_GROBJS;
  OL_OBJS(objlist) = (GR_OBJ **) calloc(OL_MAXOBJS(objlist), 
					sizeof (GR_OBJ *));
  OL_NOBJS(objlist) = 0;

  /* set defaults for the mlist parameters */

  OL_SX(objlist) = 1.0;
  OL_SY(objlist) = 1.0;
  OL_SZ(objlist) = 1.0;

  OL_SPIN(objlist) = 0.0;
  OL_AX1(objlist) = 1.0;
  OL_AX2(objlist) = 0.0;
  OL_AX3(objlist) = 0.0;

  OL_VISIBLE(objlist) = 1;	/* default to visible */
  OL_DYNAMIC(objlist) = 0;	/* default to static */

  return(objlist);
}


/********************************************************************
 * Function:     objListReset
 * Returns:      none
 * Arguments:    OBJ_LIST *olist
 * Description:  frees all objs in ObjList
 ********************************************************************/

void objListReset(OBJ_LIST *list)
{
  int i;
  for (i = 0; i < OL_MAXOBJS(list); i++) {
    gobjUnloadObj(list, i);
  }
  OL_TX(list) = OL_TY(list) = OL_TZ(list) = 0.0;
  OL_SX(list) = 1.0;
  OL_SY(list) = 1.0;
  OL_SZ(list) = 1.0;

  OL_SPIN(list) = 0.0;
  OL_AX1(list) = 1.0;
  OL_AX2(list) = 0.0;
  OL_AX3(list) = 0.0;
  OL_VISIBLE(list) = 1;

  objNameClearRegistry(list);
  return;
}

/********************************************************************
 * Function:     objListSetSpinRate
 * Returns:      None
 * Arguments:    OBJ_LIST *list, rate
 * Description:  Set the rate of rotation
 ********************************************************************/

void objListSetSpinRate(OBJ_LIST *list, float rate)
{
  OL_SPINRATE(list) = rate;
}

/********************************************************************
 * Function:     objListSetSpin
 * Returns:      None
 * Arguments:    OBJ_LIST *list
 * Description:  Sets spin to particular angular position
 ********************************************************************/

void objListSetSpin(OBJ_LIST *list, float spin)
{
  OL_SPIN(list) = spin;
}

/********************************************************************
 * Function:     objListSetRotAxis
 * Returns:      None
 * Arguments:    OBJ_LIST *list, float x, float y, float z
 * Description:  Set the axis of rotation
 ********************************************************************/

void objListSetRotAxis(OBJ_LIST *list, float x, float y, float z)
{
  OL_AX1(list) = x;
  OL_AX2(list) = y;
  OL_AX3(list) = z;
}

/********************************************************************
 * Function:     objListTranslate
 * Returns:      None
 * Arguments:    OBJ_LIST *list, x, y, z
 * Description:  Sets global offset for all objects
 ********************************************************************/

void objListTranslate(OBJ_LIST *list, float x, float y, float z)
{
  OL_TX(list) = x;
  OL_TY(list) = y;
  OL_TZ(list) = z;
}


/********************************************************************
 * Function:     gobjFindObj
 * Returns:      1 on success, 0 on failure
 * Arguments:    OBJ_LIST *objlist, int objid
 * Description:  find object by name
 ********************************************************************/

int gobjFindObj(OBJ_LIST *objlist, char *name, int *id)
{
  int i;
  GR_OBJ *obj;
  
  for (i = 0; i < OL_MAXOBJS(objlist); i++) {
    if ((obj = OL_OBJ(objlist, i))) {
      if (!strcmp(GR_NAME(obj), name)) {
	if (id) *id = i;
	return 1;
      }
    }
  }

  if (sscanf(name, "%d", &i) == 1 && i < OL_MAXOBJS(objlist)) {
    if (id) *id = i;
    return 1;
  }

  int id_by_name = objNameGet(OL_NAMEINFO(objlist), name);
  if (id_by_name >= 0 && id_by_name < OL_NOBJS(objlist)) {
    if (id) *id = id_by_name;
    return 1;
  }
  
  
  if (id) *id = -1;
  return 0;
}


/********************************************************************
 * Function:     gobjAppendObj
 * Returns:      Index of new object or -1 on error
 * Arguments:    OBJ_LIST *objlist, char *filename
 * Description:  Opens filename and appends new obj to objlist
 ********************************************************************/

int gobjAppendNewObj(OBJ_LIST *olist, char *name)
{
  GR_OBJ *obj;
  
  obj = gobjCreateObj();
  strcpy(GR_NAME(obj), name);
  return(gobjAddObj(olist, obj));
}

/********************************************************************
 * Function:     gobjAddObj
 * Returns:      Index of new object or -1 on error
 * Arguments:    OBJ_LIST *list, GR_OBJ *
 * Description:  Adds graphics obj to objlist
 ********************************************************************/

int gobjAddObj(OBJ_LIST *list, GR_OBJ *obj)
{
  int i;
  if (OL_NOBJS(list) == OL_MAXOBJS(list)) {
    OL_MAXOBJS(list) += GR_DEFAULT_GROBJS;
    OL_OBJS(list) = (GR_OBJ **) realloc(OL_OBJS(list), 
					OL_MAXOBJS(list)*sizeof(GR_OBJ *));
    memset(&OL_OBJ(list, OL_NOBJS(list)), 0, 
	   sizeof(GR_OBJ *)*GR_DEFAULT_GROBJS);
  }
  
  /* now set i to empty slot */
  for (i = 0; i < OL_MAXOBJS(list); i++) {
    if (!OL_OBJ(list, i)) break;
  }
  if (i == OL_MAXOBJS(list)) return -1; /* should never happen */
  
  OL_OBJ(list,i) = obj;
  OL_NOBJS(list)++;

  if (GR_NAME(obj)[0]) gobjAddObjName(GR_NAME(obj), GR_OBJTYPE(obj));

  return (i);
}

/********************************************************************
 * Function:     gobjCreateObj
 * Returns:      GR_OBJ *newobj
 * Arguments:    none
 * Description:  Use allocate and setup new GR_OBJ
 ********************************************************************/

GR_OBJ *gobjCreateObj(void)
{
  GR_OBJ *obj = (GR_OBJ *) calloc(1, sizeof(GR_OBJ));
  if (!obj) return(NULL);

  GR_VISIBLE(obj) = 1;
  gobjSetEye(obj, 1, 1);
  gobjScaleObj(obj, 1.0, 1.0, 1.0);
  gobjRotateObj(obj, 0, 1.0, 0.0, 0.0);
  
  GR_MATRIX(obj)[0] = 1.;
  GR_MATRIX(obj)[5] = 1.;
  GR_MATRIX(obj)[10] = 1.;
  GR_MATRIX(obj)[15] = 1.;

  return(obj);
}

/********************************************************************
 * Function:     gobjAddPre/PostScript
 * Returns:      1 on success, 0 on error
 * Arguments:    GR_OBJ *o
 * Description:  Add script to execute
 ********************************************************************/

int gobjAddPreScript(GR_OBJ *o, char *script)
{
  if (GR_N_PRE_SCRIPTS(o) >= MAXSCRIPTS) return 0;
  GR_PRE_SCRIPT(o, GR_N_PRE_SCRIPTS(o)) = strdup(script);
  GR_PRE_SCRIPT_ACTIVE(o, GR_N_PRE_SCRIPTS(o)) = 1;
  GR_N_PRE_SCRIPTS(o)++;
  return GR_N_PRE_SCRIPTS(o);
}

int gobjAddPostScript(GR_OBJ *o, char *script)
{
  if (GR_N_POST_SCRIPTS(o) >= MAXSCRIPTS) return 0;
  GR_POST_SCRIPT(o, GR_N_POST_SCRIPTS(o)) = strdup(script);
  GR_POST_SCRIPT_ACTIVE(o, GR_N_POST_SCRIPTS(o)) = 1;
  GR_N_POST_SCRIPTS(o)++;
  return GR_N_POST_SCRIPTS(o);
}


int gobjAddPostFrameScript(GR_OBJ *o, char *script)
{
  if (GR_N_POSTFRAME_SCRIPTS(o) >= MAXSCRIPTS) return 0;
  GR_POSTFRAME_SCRIPT(o, GR_N_POSTFRAME_SCRIPTS(o)) = strdup(script);
  GR_POSTFRAME_SCRIPT_ACTIVE(o, GR_N_POSTFRAME_SCRIPTS(o)) = 1;
  GR_N_POSTFRAME_SCRIPTS(o)++;
  return GR_N_POSTFRAME_SCRIPTS(o);
}

int gobjAddThisFrameScript(GR_OBJ *o, char *script)
{
  if (GR_N_THISFRAME_SCRIPTS(o) >= MAXSCRIPTS) return 0;
  GR_THISFRAME_SCRIPT(o, GR_N_THISFRAME_SCRIPTS(o)) = strdup(script);
  GR_N_THISFRAME_SCRIPTS(o)++;
  return GR_N_THISFRAME_SCRIPTS(o);
}


static int gobjSetScriptActivation(GR_OBJ *o, int type, int slot, int val)
{
  int old;
  int *actives;
  int n;

  switch (type) {
  case STIM_PRE_SCRIPT:
    n = GR_N_PRE_SCRIPTS(o);
    actives = GR_PRE_SCRIPT_ACTIVES(o);
    break;
  case STIM_POST_SCRIPT:
    n = GR_N_POST_SCRIPTS(o);
    actives = GR_POST_SCRIPT_ACTIVES(o);
    break;
  case STIM_POSTFRAME_SCRIPT:
    n = GR_N_POSTFRAME_SCRIPTS(o);
    actives = GR_POSTFRAME_SCRIPT_ACTIVES(o);
  }
  
  if (slot < n) {
    old = actives[slot];
    actives[slot] = (val != 0);
    return old;
  }
  return -1;
}

int gobjActivatePreScript(GR_OBJ *obj, int slot)
{
  return gobjSetScriptActivation(obj, STIM_PRE_SCRIPT, slot, 1);
}

int gobjActivatePostScript(GR_OBJ *obj, int slot)
{
  return gobjSetScriptActivation(obj, STIM_POST_SCRIPT, slot, 1);
}

int gobjActivatePostFrameScript(GR_OBJ *obj, int slot)
{
  return gobjSetScriptActivation(obj, STIM_POSTFRAME_SCRIPT, slot, 1);
}

int gobjDeactivatePreScript(GR_OBJ *obj, int slot)
{
  return gobjSetScriptActivation(obj, STIM_PRE_SCRIPT, slot, 0);
}

int gobjDeactivatePostScript(GR_OBJ *obj, int slot)
{
  return gobjSetScriptActivation(obj, STIM_POST_SCRIPT, slot, 0);
}

int gobjDeactivatePostFrameScript(GR_OBJ *obj, int slot)
{
  return gobjSetScriptActivation(obj, STIM_POSTFRAME_SCRIPT, slot, 0);
}

static int gobjReplaceScript(GR_OBJ *o, int type, int slot, char *script)
{
  int old;
  char **scripts;
  int *actives;
  int n;

  switch (type) {
  case STIM_PRE_SCRIPT:
    n = GR_N_PRE_SCRIPTS(o);
    scripts = GR_PRE_SCRIPTS(o);
    actives = GR_PRE_SCRIPT_ACTIVES(o);
    break;
  case STIM_POST_SCRIPT:
    n = GR_N_POST_SCRIPTS(o);
    scripts = GR_POST_SCRIPTS(o);
    actives = GR_POST_SCRIPT_ACTIVES(o);
    break;
  case STIM_POSTFRAME_SCRIPT:
    n = GR_N_POSTFRAME_SCRIPTS(o);
    scripts = GR_POSTFRAME_SCRIPTS(o);
    actives = GR_POSTFRAME_SCRIPT_ACTIVES(o);
  }
  
  if (slot < n) {
    old = actives[slot];
    if (scripts[slot]) free (scripts[slot]);
    scripts[slot] = strdup(script);
    actives[slot] = 1;		/* replaced scripts are activated */
    return old;
  }
  return -1;
}

int gobjReplacePreScript(GR_OBJ *obj, int slot, char *script)
{
  return gobjReplaceScript(obj, STIM_PRE_SCRIPT, slot, script);
}

int gobjReplacePostScript(GR_OBJ *obj, int slot, char *script)
{
  return gobjReplaceScript(obj, STIM_POST_SCRIPT, slot, script);
}

int gobjReplacePostFrameScript(GR_OBJ *obj, int slot, char *script)
{
  return gobjReplaceScript(obj, STIM_POSTFRAME_SCRIPT, slot, script);
}


static void free_scripts(GR_OBJ *o, int type)
{
  int i;
  char **scripts;
  int n;
  int *actives;

  switch (type) {
  case STIM_PRE_SCRIPT:
    {
      n = GR_N_PRE_SCRIPTS(o);
      scripts = GR_PRE_SCRIPTS(o);
      actives = GR_PRE_SCRIPT_ACTIVES(o);
    }
    break;
  case STIM_POST_SCRIPT:
    {
      n = GR_N_POST_SCRIPTS(o);
      scripts = GR_POST_SCRIPTS(o);
      actives = GR_POST_SCRIPT_ACTIVES(o);
    }
    break;
  case STIM_POSTFRAME_SCRIPT:
    {
      n = GR_N_POSTFRAME_SCRIPTS(o);
      scripts = GR_POSTFRAME_SCRIPTS(o);
      actives = GR_POSTFRAME_SCRIPT_ACTIVES(o);
    }
    break;
  } 
  for (i = 0; i < MAXSCRIPTS; i++) {
    if (scripts[i]) free(scripts[i]);
    actives[0] = 0;
  }
}

/********************************************************************
 * Function:     gobjDestroyObj
 * Returns:      1 on success, 0 on failure
 * Arguments:    GR_OBJ *o
 * Description:  frees the graphics objects
 ********************************************************************/

int gobjDestroyObj(GR_OBJ *o)
{
  if (!o) return(0);
  animateClearObj(o);           /* in animate.c */  
  gobjDelete(o);		/* in main.c    */
  free_scripts(o, STIM_PRE_SCRIPT);
  free_scripts(o, STIM_POST_SCRIPT);
  free_scripts(o, STIM_POSTFRAME_SCRIPT);
  delete_property_table(o);     /* tclproc.c    */
  free(o);
  return(1);
}

/********************************************************************
 * Function:     gobjUnloadObj
 * Returns:      1 on success, 0 on failure
 * Arguments:    OBJ_LIST *list, int objid
 * Description:  frees OL_OBJ(list,i)
 ********************************************************************/

int gobjUnloadObj(OBJ_LIST *list, int id)
{
  GR_OBJ *o = OL_OBJ(list, id);
  if (!o) return(0);

  gobjDestroyObj(o);

  OL_OBJ(list, id) = NULL;
  OL_NOBJS(list)--;
  return(1);
}


/********************************************************************
 * Function:     gobjResetObj
 * Returns:      1 on success, 0 on failure
 * Arguments:    GR_OBJ *obj
 * Description:  reset obj using reset callback
 ********************************************************************/

void gobjResetObj(GR_OBJ *gobj)
{
  if (gobj && GR_RESETFUNCP(gobj)) GR_RESETFUNC(gobj)(gobj);
}

/********************************************************************
 * Function:     gobjTranslateObj
 * Returns:      None
 * Arguments:    GR_OBJ *obj, x, y, z
 * Description:  Set the postion of a graphics object
 ********************************************************************/

void gobjTranslateObj(GR_OBJ *gobj, float x, float y, float z)
{
  GR_TX(gobj) = x;
  GR_TY(gobj) = y;
  GR_TZ(gobj) = z;
}

/********************************************************************
 * Function:     gobjScaleObj
 * Returns:      None
 * Arguments:    GR_OBJ *gobj, x, y, z
 * Description:  Set the scale of a graphics object
 ********************************************************************/

void gobjScaleObj(GR_OBJ *gobj, float x, float y, float z)
{
  GR_SX(gobj) = x;
  GR_SY(gobj) = y;
  GR_SZ(gobj) = z;
}



/********************************************************************
 * Function:     gobjRotateObj
 * Returns:      None
 * Arguments:    GR_OBJ *obj, float spin, float x, float y, float z
 * Description:  Set the axis of rotation for individual object
 ********************************************************************/

void gobjRotateObj(GR_OBJ *obj, float spin, float x, float y, float z)
{
  GR_SPIN(obj) = spin;
  GR_AX1(obj) = x;
  GR_AX2(obj) = y;
  GR_AX3(obj) = z;
}


/********************************************************************
 * Function:     gobjSetMatrix
 * Returns:      None
 * Arguments:    GR_OBJ *gobj, float *matrix
 * Description:  Set the matrix of a graphics object
 ********************************************************************/

float *gobjSetMatrix(GR_OBJ *gobj, float *matrix)
{
  static float oldmatrix[16];

  memcpy(oldmatrix, GR_MATRIX(gobj), 16*sizeof(float));

  if (matrix) 
    memcpy(GR_MATRIX(gobj), matrix, 16*sizeof(float));
  
  return oldmatrix;
}

/********************************************************************
 * Function:     gobjUseMatrix
 * Returns:      None
 * Arguments:    GR_OBJ *gobj, int use
 * Description:  Use matrix or scale/translate/rotate settings
 ********************************************************************/

int gobjUseMatrix(GR_OBJ *gobj, int use)
{
  int old = GR_USEMATRIX(gobj);;
  GR_USEMATRIX(gobj) = use;
  return old;
}


/********************************************************************
 * Function:     gobjSetEye
 * Returns:      None
 * Arguments:    GR_OBJ *gobj, int l, int r
 * Description:  Sets which eye is visible if in stereo
 ********************************************************************/

void gobjSetEye(GR_OBJ *gobj, int l, int r)
{
  if (!gobj) return;

  GR_LEFT_EYE(gobj) = l;
  GR_RIGHT_EYE(gobj) = r;
}

/********************************************************************
 * Function:     gobjSetVisibility
 * Returns:      int old
 * Arguments:    GR_OBJ *obj, status 
 * Description:  Set the object to be visible or not
 ********************************************************************/

int gobjSetVisibility(GR_OBJ *obj, int status)
{
  int old;
  if (!obj) return(-1);
  old = GR_VISIBLE(obj);
  GR_VISIBLE(obj) = status;
  return(old);
}

/********************************************************************
 * Function:     gobjSetCount
 * Returns:      int old
 * Arguments:    GR_OBJ *obj, count
 * Description:  Set the object count to specific value
 ********************************************************************/

int gobjSetCount(GR_OBJ *obj, int count)
{
  int old;
  if (!obj) return(-1);
  old = GR_COUNT(obj);
  GR_COUNT(obj) = count;
  return(old);
}

/********************************************************************
 * Function:     gobjGetCount
 * Returns:      int old
 * Arguments:    GR_OBJ *obj, count
 * Description:  Get the object count to specific value
 ********************************************************************/

int gobjGetCount(GR_OBJ *obj, int count)
{
  if (!obj) return(-1);
  return (GR_COUNT(obj));
}


