/*
 * metagroup.c
 *  Module to create groups of objects to be treated as one
 */


#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tcl.h>
#include <math.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h> 

#include <prmutil.h>
#include <objname.h>

/* If you want access to dlsh connectivity, include these */
#include "df.h"
#include "tcl_dl.h"

#include <stim2.h>		/* Stim header      */

typedef struct {
  OBJ_LIST *objlist;
  int *objects;
  int maxobjs;
  int nobjs;
  int increment;
} METAGROUP;

static int MetagroupID = -1;	/* unique object id */

void metagroupDraw(GR_OBJ *o) 
{
  int i, id;
  METAGROUP *mg = (METAGROUP *) GR_CLIENTDATA(o);
  GR_OBJ *g;
  float modelmatrix[16];
  
  /* To do: check for recursive groups? */
  for (i = 0; i < mg->nobjs; i++) {
    id = mg->objects[i];
    if (id >= 0 && id < OL_NOBJS(mg->objlist)) {
      g = OL_OBJ(mg->objlist,id);

      stimGetMatrix(STIM_MODELVIEW_MATRIX, modelmatrix);
      //      glPushMatrix ();
      executeScripts(GR_PRE_SCRIPTS(g),
		     GR_PRE_SCRIPT_ACTIVES(g),
		     GR_N_PRE_SCRIPTS(g));

      stimMultGrObjMatrix(STIM_MODELVIEW_MATRIX, g);
      
      //      glTranslatef(GR_TX(g), GR_TY(g), GR_TZ(g));
      //      glRotatef (GR_SPIN(g), GR_AX1(g), GR_AX2(g), GR_AX3(g));
      //      glScalef(GR_SX(g), GR_SY(g), GR_SZ(g));

      if (GR_VISIBLE(g)) drawObj(g);

      executeScripts(GR_POST_SCRIPTS(g),
		     GR_POST_SCRIPT_ACTIVES(g),
		     GR_N_POST_SCRIPTS(g));

      stimPutMatrix(STIM_MODELVIEW_MATRIX, modelmatrix);
      //      glPopMatrix ();
    }
  }
}

void metagroupUpdate(GR_OBJ *o)
{
  int i, id;
  METAGROUP *mg = (METAGROUP *) GR_CLIENTDATA(o);
  GR_OBJ *g;
  
  /* To do: check for recursive groups? */
  for (i = 0; i < mg->nobjs; i++) {
    id = mg->objects[i];
    if (id >= 0 && id < OL_NOBJS(mg->objlist)) {
      g = OL_OBJ(mg->objlist,id);
      if (g && GR_UPDATEFUNCP(g)) GR_UPDATEFUNC(g)(g);
    }
  }
}

void metagroupReset(GR_OBJ *o)
{
  int i, id;
  METAGROUP *mg = (METAGROUP *) GR_CLIENTDATA(o);
  GR_OBJ *g;
  
  /* To do: check for recursive groups? */
  for (i = 0; i < mg->nobjs; i++) {
    id = mg->objects[i];
    if (id >= 0 && id < OL_NOBJS(mg->objlist)) {
      g = OL_OBJ(mg->objlist,id);
      if (g && GR_RESETFUNCP(g)) GR_RESETFUNC(g)(g);
    }
  }
}

void metagroupDelete(GR_OBJ *g) 
{
  METAGROUP *mg = (METAGROUP *) GR_CLIENTDATA(g);
  if (mg->objects) free(mg->objects);
  free((void *) mg);
}

int metagroupCreate(OBJ_LIST *objlist)
{
  const char *name = "Metagroup";
  GR_OBJ *obj;
  METAGROUP *g;
  int n = 100;

  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = MetagroupID;

  GR_ACTIONFUNCP(obj) = metagroupDraw;
  GR_DELETEFUNCP(obj) = metagroupDelete;
  GR_UPDATEFUNCP(obj) = metagroupUpdate;
  GR_RESETFUNCP(obj) = metagroupReset;

  g = (METAGROUP *) calloc(1, sizeof(METAGROUP));
  GR_CLIENTDATA(obj) = g;

  g->objlist = objlist;
  g->maxobjs = n;
  g->objects = (int *) calloc(n, sizeof(int));
  g->increment = 10;

  return(gobjAddObj(objlist, obj));
}


static int metagroupCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;
  char *handle;
  if (argc < 1) {
    Tcl_AppendResult(interp, "usage: ", argv[0], NULL);
    return TCL_ERROR;
  }
  else handle = argv[1];
  
  if ((id = metagroupCreate(olist)) < 0) {
    Tcl_AppendResult(interp, "error creating metagroup", TCL_STATIC);
    return(TCL_ERROR);
  }

  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
  return(TCL_OK);
}

static int metagroupAddCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  METAGROUP *mg;
  DYN_LIST *objs;
  int i, id, *objids;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " metagroup idlist", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
			 MetagroupID, "metagroup")) < 0)
    return TCL_ERROR;  
    
  mg = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (tclFindDynList(interp, argv[2], &objs) != TCL_OK) {
    return TCL_ERROR;
  }
  
  if (DYN_LIST_DATATYPE(objs) != DF_LONG) {
    Tcl_AppendResult(interp, argv[0], ": object list must be ints", NULL);
    return TCL_ERROR;
  }

  objids = (int *) DYN_LIST_VALS(objs);
  for (i = 0; i < DYN_LIST_N(objs); i++) {
    /* Realloc if out of space */
    if (mg->nobjs  >= mg->maxobjs) {
      mg->maxobjs += mg->increment;
      mg->objects = (int *) realloc(mg->objects, sizeof(int)*mg->maxobjs);
    }
    /* Add the id */
    mg->objects[mg->nobjs] = objids[i];
    mg->nobjs++;
  }

  return TCL_OK;
} 


static int metagroupClearCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  METAGROUP *mg;
  int id;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " metagroup", NULL);
    return TCL_ERROR;
  }
  
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
			 MetagroupID, "metagroup")) < 0)
    return TCL_ERROR;  
    
  mg = GR_CLIENTDATA(OL_OBJ(olist,id));
  mg->nobjs = 0;
  return TCL_OK;
}

static int metagroupSetCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  if (metagroupClearCmd(clientData, interp, argc, argv) != TCL_OK)
    return TCL_ERROR;
  return metagroupAddCmd(clientData, interp, argc, argv);
}


static int metagroupContentsCmd(ClientData clientData, Tcl_Interp *interp,
				int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  METAGROUP *mg;
  int i, id;
  Tcl_DString objlist;
  char ibuf[16];

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " metagroup", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
			 MetagroupID, "metagroup")) < 0)
    return TCL_ERROR;  
  
  Tcl_DStringInit(&objlist);

  mg = GR_CLIENTDATA(OL_OBJ(olist,id));
  for (i = 0; i < mg->nobjs; i++) {
    id = mg->objects[i];
    if (id >= 0 && id < OL_NOBJS(mg->objlist)) {
      sprintf(ibuf, "%d", mg->objects[i]);
      Tcl_DStringAppendElement(&objlist, ibuf);
    }
  }
  Tcl_DStringResult(interp, &objlist);

  return TCL_OK;
}

#ifdef _WIN32
EXPORT(int,Metagroup_Init) (Tcl_Interp *interp)
#else
int Metagroup_Init(Tcl_Interp *interp)
#endif
{
  OBJ_LIST *OBJList = getOBJList();

  gladLoadGL();			/* probably not necessary for this module */

  if (
#ifdef USE_TCL_STUBS
      Tcl_InitStubs(interp, "8.5-", 0)
#else
      Tcl_PkgRequire(interp, "Tcl", "8.5-", 0)
#endif
      == NULL) {
    return TCL_ERROR;
  }
  
  
  if (MetagroupID < 0) MetagroupID = gobjRegisterType();

Tcl_CreateCommand(interp, "metagroup", (Tcl_CmdProc *) metagroupCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "metagroupAdd", (Tcl_CmdProc *) metagroupAddCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "metagroupClear", (Tcl_CmdProc *) metagroupClearCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "metagroupSet", (Tcl_CmdProc *) metagroupSetCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "metagroupContents", (Tcl_CmdProc *) metagroupContentsCmd, 
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
