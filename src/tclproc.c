/*
 * NAME
 *      tclproc.c - start up a tcl process inside a program
 *
 * DESCRIPTION
 *      Creates and initializes a tcl interpreter which can communicate
 * with a main process and the outside world.
 *
 * AUTHOR
 *     DLS
 *
 * DATE
 *     NOV94 - NOV17
 */

#if defined(_WIN32) || defined(_WIN64) 
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tcl.h>

#include "stim2.h"

#ifdef WIN32
static int strcasecmp(char *a,char *b) { return stricmp(a,b); }
static int strncasecmp(char *a,char *b, int n) 
{ 
  return strnicmp(a,b,n);
}
#endif

static int separate_tcl_thread = 0;
int Tcl_StimAppInit(Tcl_Interp *interp);
static Tcl_Interp *OurInterp = NULL;
void addTclCommands(Tcl_Interp *interp);
void tclAddParamTable(Tcl_Interp *interp, PARAM_ENTRY *table, char *name);

extern PARAM_ENTRY ScreenParamTable[];

/*********************************************************************/
/*                           Tcl Code                                */
/*********************************************************************/
#if 0
int sendTclKeyboardCommand(int key)
{
  int retcode;
  char procname[64];

  switch (key) {
  case STIMK_UP:       strcpy(procname,"onUpArrow");     break;
  case STIMK_DOWN:     strcpy(procname,"onDownArrow");   break;
  case STIMK_LEFT:     strcpy(procname,"onLeftArrow");   break;
  case STIMK_RIGHT:    strcpy(procname,"onRightArrow");  break;
  default:             sprintf(procname, "onKeyPress %d", key); break;
  }
  if (Tcl_FindCommand(OurInterp, procname, NULL, 0)) {
    retcode = Tcl_Eval(OurInterp, procname);
    Tcl_ResetResult(OurInterp);
    return 1;
  }
  else {
    return 0;
  }
}

int sendTclKeyboardUpCommand(int key)
{
  int retcode;
  char procname[64];

  switch (key) {
  case STIMK_UP:       strcpy(procname,"onUpArrowRelease");     break;
  case STIMK_DOWN:     strcpy(procname,"onDownArrowRelease");   break;
  case STIMK_LEFT:     strcpy(procname,"onLeftArrowRelease");   break;
  case STIMK_RIGHT:    strcpy(procname,"onRightArrowRelease");  break;
  default:             sprintf(procname, "onKeyRelease %d", key); break;
  }
  if (Tcl_FindCommand(OurInterp, procname, NULL, 0)) {
    retcode = Tcl_Eval(OurInterp, procname);
    Tcl_ResetResult(OurInterp);
    return 1;
  }
  else {
    return 0;
  }
}

int sendTclMouseMoveCommand(int x, int y)
{
  int retcode;
  static char xstr[16], ystr[16];

  const char *cmd = "onMouseMove";

  /* if the command onMouseMove is defined...*/
  if (Tcl_FindCommand(OurInterp, cmd, NULL, 0)) {
    sprintf(xstr,"%d", x);
    sprintf(ystr,"%d", y);
    retcode = 
      Tcl_VarEval(OurInterp, "onMouseMove ", xstr, " ", ystr, (char *) NULL);
    Tcl_ResetResult(OurInterp);
    return 1;
  }
  else {
    return 0;
  }



}

int sendTclMouseCommand(int key, int down)
{
  int retcode;
  char *procname;

  switch (down) {
  case 1:
    switch(key) {
    case STIMK_LBUTTON:    procname = "onLeftButtonDown";     break;
    case STIMK_MBUTTON:    procname = "onMiddleButtonDown";   break;
    case STIMK_RBUTTON:    procname = "onRightButtonDown";    break;
    }
    break;
  case 0:
    switch(key) {
    case STIMK_LBUTTON:    procname = "onLeftButtonUp";     break;
    case STIMK_MBUTTON:    procname = "onMiddleButtonUp";   break;
    case STIMK_RBUTTON:    procname = "onRightButtonUp";    break;
    }
    break;
  }
  if (Tcl_FindCommand(OurInterp, procname, NULL, 0)) {
    retcode = Tcl_Eval(OurInterp, procname);
    Tcl_ResetResult(OurInterp);
    return 1;
  }
  else {
    return 0;
  }
}

int sendTclMouseXCommand(int key, int down)
{
  int retcode;
  char *procname;

  switch (down) {
  case 1:
    switch(key) {
    case 1:    procname = "onButton4Down";     break;
    case 2:    procname = "onButton5Down";     break;
    }
    break;
  case 0:
    switch(key) {
    case 1:    procname = "onButton4Up";     break;
    case 2:    procname = "onButton5Up";     break;
    }
    break;
  }
  if (Tcl_FindCommand(OurInterp, procname, NULL, 0)) {
    retcode = Tcl_Eval(OurInterp, procname);
    Tcl_ResetResult(OurInterp);
    return 1;
  }
  else {
    return 0;
  }
}
#endif

/*********************************************************************/
/*                          Ping Command                             */
/*********************************************************************/

static int pingCmd(ClientData clientData, Tcl_Interp *interp,
		   int argc, char *argv[])
{
  Tcl_AppendResult(interp, "pong", NULL);
  if (argc > 1) {
    Tcl_AppendResult(interp, " ", argv[1], NULL);
  }
  return TCL_OK;
}


/*********************************************************************/
/*                     Setsystem Command                             */
/*********************************************************************/

static int setsystemCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  if (argc > 1) {
    char filename[80];
    sprintf(filename, "%s.tcl", argv[1]);
    Tcl_EvalFile(interp, filename);
  }
  return TCL_OK;
}

/*********************************************************************/
/*                          Exit Command                             */
/*********************************************************************/

static int exitCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  kill_window();
  exit(0);
  return TCL_OK;
}

/*********************************************************************/
/*                          Local Commands                           */
/*********************************************************************/

static int dumpCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  int dumptype = (int) (size_t) clientData;
  DUMP_INFO dinfo;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " filename [x y w h]", NULL); 
    return TCL_ERROR;
  }
  strcpy(dinfo.filename, argv[1]);
  dinfo.x = dinfo.y = dinfo.w = dinfo.h = 0;

  if (argc > 2) {
    if (Tcl_GetInt(interp, argv[2], &dinfo.x) != TCL_OK) return TCL_ERROR;
  }
  if (argc > 3) {
    if (Tcl_GetInt(interp, argv[3], &dinfo.y) != TCL_OK) return TCL_ERROR;
  }
  if (argc > 4) {
    if (Tcl_GetInt(interp, argv[4], &dinfo.w) != TCL_OK) return TCL_ERROR;
  }
  if (argc > 5) {
    if (Tcl_GetInt(interp, argv[5], &dinfo.h) != TCL_OK) return TCL_ERROR;
  }
  switch (dumptype) {
  case DUMP_RAW:
    //    sendDispMsgWithData(DUMP_RAW, (int) &dinfo);
    DumpInfo.x = dinfo.x; DumpInfo.y = dinfo.y;
    DumpInfo.w = dinfo.w; DumpInfo.h = dinfo.h;
    strcpy(DumpInfo.filename, dinfo.filename);
    
    sendDispMsg(DUMP_RAW);
    break;
  case DUMP_PS:
    //    sendDispMsgWithData(DUMP_PS, (int) argv[1]);
    break;
  }
  return TCL_OK;
}

static int toggleImguiCmd(ClientData clientData, Tcl_Interp *interp,
			  int argc, char *argv[])
{
  doToggleImgui();
  return TCL_OK;
}

/*
 * log to imgui widget
 */
static int logMessageCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  if (argc < 2) {
    Tcl_SetResult(interp, "usage: log message", TCL_STATIC);
    return TCL_ERROR;
  }

  logMessage(argv[1]);
  
  return TCL_OK;
}

static int setVerboseLevelCmd(ClientData clientData, Tcl_Interp *interp,
			  int argc, char *argv[])
{
  static int level;

  if (argc < 2) {
    Tcl_SetResult(interp, "usage: setVerboseLevel verbosity", TCL_STATIC);
    return TCL_ERROR;
  }
  if (Tcl_GetInt(interp, argv[1], &level) != TCL_OK) return TCL_ERROR;

  setVerboseLevel(level);
  return TCL_OK;
}

static int showCursorCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  sendDispMsg(SHOW_CURSOR);
  return TCL_OK;
}

static int hideCursorCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  sendDispMsg(HIDE_CURSOR);
  return TCL_OK;
}


static int setCursorPosCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  static int pos[2];
  if (argc < 3) {
    Tcl_SetResult(interp, "usage: setCursorPos x y", TCL_STATIC);
    return TCL_ERROR;
  }
  if (Tcl_GetInt(interp, argv[1], &pos[0]) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &pos[1]) != TCL_OK) return TCL_ERROR;
  //  sendDispMsgWithData(SET_CURSOR_POS, (int) &pos[0]);
  return TCL_OK;
}

static int setBackgroundCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  static char *usage_message = "usage: setBackground r g b";
  int r, g, b;
  char buf[32];

  if (argc != 1 && argc != 4) {
    Tcl_SetResult(interp, usage_message, TCL_STATIC);
    return TCL_ERROR;
  }

  /* With no args, return current color (in range 0-255) */
  if (argc == 1) {
    sprintf(buf, "%d %d %d", 
	    (int) (BackgroundColor[0]*255.),
	    (int) (BackgroundColor[1]*255.),
	    (int) (BackgroundColor[2]*255.));
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
  }

  if (Tcl_GetInt(interp, argv[1], &r) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &g) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &b) != TCL_OK) return TCL_ERROR;

  BackgroundColor[0] = r / 255.0;
  BackgroundColor[1] = g / 255.0;
  BackgroundColor[2] = b / 255.0;

  sendDispMsg(SET_BACKGROUND);
  return TCL_OK;
}

static int toggleAnimationCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  toggleAnimation();
  return TCL_OK;
}

static int startAnimationCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  startAnimation();
  return TCL_OK;
}

static int stopAnimationCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  stopAnimation();
  return TCL_OK;
}

static int kickAnimationCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  kickAnimation();
  return TCL_OK;
}

static int setStereoModeCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  int mode, old;
  char buf[8];
  if (argc < 2) {
    sprintf(buf, "%d", getStereoMode());
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
  }
   
  if (Tcl_GetInt(interp, argv[1], &mode) != TCL_OK) return TCL_ERROR;
  if (mode > 4) {
    Tcl_AppendResult(interp, argv[0], ": StereoMode must be 0, 1, 2, 3, or 4",
		     NULL);
    return TCL_ERROR;
  }
  old = setStereoMode(mode);
  sprintf(buf, "%d", old);
  Tcl_SetResult(interp, buf, TCL_VOLATILE);
  return TCL_OK;
}

static int setDynamicUpdateCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  static char buf[16];
  if (argc < 2) {
    sprintf(buf, "%d", OL_DYNAMIC(olist));
    Tcl_SetResult(interp, buf, TCL_STATIC);
  }
  else {
    int status;
    if (Tcl_GetInt(interp, argv[1], &status) != TCL_OK) return TCL_ERROR;
    if (status != 0) status = 1;
    OL_DYNAMIC(olist) = status;
  }
  return TCL_OK;
}

/********************************************************************/

static int redrawCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  redraw();
  return TCL_OK;
}

static int reshapeCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  reshape();
  return TCL_OK;
}

static int resetObjListCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  setDynamicUpdate(0);
  objListReset(olist);
  return TCL_OK;
}

static int resetGraphicsStateCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  resetGraphicsState();
  return TCL_OK;
}

int findObj(Tcl_Interp *interp, OBJ_LIST *olist,
	    char *name, int *id)
{
  int status;
  
  if (Tcl_GetInt(interp, name, id) == TCL_OK &&
      *id < OL_MAXOBJS(olist)) return TCL_OK;
  Tcl_ResetResult(interp);
  status = gobjFindObj(olist, name, id);
  if (!status) {
    Tcl_AppendResult(interp, "findObj: ", "obj \"", name, 
		     "\" not found", NULL);
    return TCL_ERROR;
  }
  else return TCL_OK;
}

static int unloadObjCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;
  static char buf[8];
  
  if (argc != 2) {
    Tcl_SetResult(interp, "usage: unloadObj object", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;

  sprintf(buf, "%d", gobjUnloadObj(olist, id));
  Tcl_SetResult(interp, buf, TCL_STATIC);

  return(TCL_OK);
}


static int resetObjCmd(ClientData clientData, Tcl_Interp *interp,
		       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *obj;
  int id;
  
  if (argc < 2) {
    Tcl_SetResult(interp, "usage: resetObj objid", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  obj = OL_OBJ(olist, id);
  
  if (!obj) {
    Tcl_SetResult(interp, "resetObj: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }
  
  gobjResetObj(obj);
  redraw();
  return TCL_OK;
}

static int nullObjCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  static int NullObjID = -1;
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  static char buf[16];
  const char *name = "Null Object";
  GR_OBJ *obj;

  obj = gobjCreateObj();
  if (!obj) return -1;

  if (NullObjID < 0) NullObjID = gobjRegisterType();
  
  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = NullObjID;
  sprintf(buf, "%d", gobjAddObj(olist, obj));
  Tcl_SetResult(interp, buf, TCL_STATIC);
  return TCL_OK;
}

static int translateObjListCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  double x, y, z = 0.0;
  
  if (argc < 3) {
    Tcl_SetResult(interp, "usage: translateObjList x y [z]", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (Tcl_GetDouble(interp, argv[1], &x) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &y) != TCL_OK) return TCL_ERROR;
  if (argc > 3) 
	if (Tcl_GetDouble(interp, argv[3], &z) != TCL_OK) return TCL_ERROR;

  objListTranslate(olist, x, y, z);
  
  return TCL_OK;
}

static int translateObjCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id;
  double x, y, z = 0.0;
  static char buf[48];
  if (argc < 2) {
  usage:
    Tcl_SetResult(interp, "usage: translateObj objid x y [z]", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;

  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "translateObj: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (argc == 2) {
    sprintf(buf, "%.4f %.4f %.4f",  
      o->position[0], o->position[1], o->position[2]);
    Tcl_SetResult(interp, buf, TCL_STATIC);
    return TCL_OK;
  }
  else if (argc < 4) goto usage;
  
  if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;
  if (argc > 4) 
	if (Tcl_GetDouble(interp, argv[4], &z) != TCL_OK) return TCL_ERROR;

  gobjTranslateObj(o, x, y, z);

  return TCL_OK;
}

static int scaleObjCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id;
  double x, y, z;

  if (argc < 3) {
    Tcl_SetResult(interp, "usage: scaleObj objid x [y [z]]", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;

  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "scaleObj: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }


  if (argc == 2) {
    static char buf[32];
    sprintf(buf, "%.4f %.4f %.4f",
	    o->scale[0], o->scale[1], o->scale[2]);
    Tcl_SetResult(interp, buf, TCL_STATIC);
    return TCL_OK;
  }
  
  if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
  if (argc > 3) {
	if (Tcl_GetDouble(interp, argv[3], &y) != TCL_OK) return TCL_ERROR;
  }
  else y = x;
  if (argc > 4) {
	if (Tcl_GetDouble(interp, argv[4], &z) != TCL_OK) return TCL_ERROR;
  }
  else z = x;

  gobjScaleObj(o, x, y, z);

  return TCL_OK;
}

static int rotateObjCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id;
  double spin, x, y, z;

  if (argc < 2) {
  usage:
    Tcl_SetResult(interp, "usage: rotateObj objid spin x y z", TCL_STATIC);
    return TCL_ERROR;
  }

  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;

  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "rotateObj: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }

  if (argc == 2) {
    static char buf[48];
    sprintf(buf, "%.4f %.4f %.4f %.4f",
	    o->spin, o->rotation[0], o->rotation[1], o->rotation[2]);
    Tcl_SetResult(interp, buf, TCL_STATIC);
    return TCL_OK;
  }

  else if (argc < 6) goto usage;

  if (Tcl_GetDouble(interp, argv[2], &spin) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &x) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &y) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &z) != TCL_OK) return TCL_ERROR;
  
  gobjRotateObj(o, spin, x, y, z);

  return TCL_OK;
}



static int setVisibleCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id, status;
  static char buf[8];
  
  if (argc < 2) {
    Tcl_SetResult(interp, "usage: setVisible objid [{0|1}]", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "setVisible: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }

  if (argc > 2) {
    if (Tcl_GetInt(interp, argv[2], &status) != TCL_OK) return TCL_ERROR;
    gobjSetVisibility(o, status);
  }

  sprintf(buf, "%d", GR_VISIBLE(o));
  Tcl_SetResult(interp, buf, TCL_STATIC);
  return TCL_OK;
}


static int setProjMatrixCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int i;
  static float matrix[16];
  double dval;
  Tcl_DString mstring;
  char dbuf[16];
  
  if (argc != 1 && argc != 17) {
    Tcl_SetResult(interp, "usage: setProjMatrix objid {matrix_vals}", TCL_STATIC);
    return TCL_ERROR;
  }

  stimGetMatrix(STIM_PROJECTION_MATRIX, matrix);

  Tcl_DStringInit(&mstring);
  for (i = 0; i < 16; i++) {
    sprintf(dbuf, "%.8f", matrix[i]);
    Tcl_DStringAppendElement(&mstring, dbuf);
  }
  
  if (argc == 17) {
    for (i = 0; i < 16; i++) {
      if (Tcl_GetDouble(interp, argv[i+1], &dval) != TCL_OK) {
	Tcl_DStringFree(&mstring);
	return TCL_ERROR;
      }
      matrix[i] = (float) dval;
    }
    stimPutMatrix(STIM_PROJECTION_MATRIX, matrix);
  }
      
  Tcl_DStringResult(interp, &mstring);
  return TCL_OK;
}


static int setObjMatrixCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id, i;
  static float matrix[16];
  double dval;
  Tcl_DString mstring;
  char dbuf[16];
  
  if (argc != 2 && argc != 18) {
    Tcl_SetResult(interp, "usage: setObjMatrix objid {matrix_vals}", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "setObjMatrix: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }
  
  
  Tcl_DStringInit(&mstring);
  for (i = 0; i < 16; i++) {
    sprintf(dbuf, "%.8f", GR_MATRIX(o)[i]);
    Tcl_DStringAppendElement(&mstring, dbuf);
  }
  
  if (argc == 18) {
    for (i = 0; i < 16; i++) {
      if (Tcl_GetDouble(interp, argv[i+2], &dval) != TCL_OK) {
	Tcl_DStringFree(&mstring);
	return TCL_ERROR;
      }
      matrix[i] = (float) dval;
    }
    gobjSetMatrix(o, matrix);
    if (!GR_USEMATRIX(o)) 
      gobjUseMatrix(o, 1);	/* this is a side effect of setting */
  }
      
  Tcl_DStringResult(interp, &mstring);
  return TCL_OK;
}


static int useObjMatrixCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id;
  int old, use;
  static char buf[8];
 
  if (argc < 2) {
    Tcl_SetResult(interp, "usage: useObjMatrix objid {use}", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "useObjMatrix: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }
  
  old = GR_USEMATRIX(o);
  if (argc > 2) {
    if (Tcl_GetInt(interp, argv[2], &use) != TCL_OK) 
      return TCL_ERROR;
    gobjUseMatrix(o, use);    
  }
      
  sprintf(buf, "%d", old);
  Tcl_SetResult(interp, buf, TCL_STATIC);
  return TCL_OK;
}


/*
 * Property tables are simply hash tables associated with a graphics
 * object that are created upon first insertion and deleted (if created)
 * a gobj is deleted
 * 
 */

void delete_property_table(GR_OBJ *o)
{
  Tcl_HashTable *table = (Tcl_HashTable *) GR_PROPERTY_TABLE(o);
  static Tcl_HashSearch search;
  Tcl_HashEntry *entryPtr;

  if (!table) return;

  /* Free strings in the table */
  for (entryPtr = Tcl_FirstHashEntry(table, &search);
       entryPtr != NULL;
       entryPtr = Tcl_NextHashEntry(&search)) {
    free(Tcl_GetHashValue(entryPtr));
  }

  /* Free table pointers */
  Tcl_DeleteHashTable((Tcl_HashTable *) table);

  /* Free table */
  free(table);

  /* Reset to NULL */
  GR_PROPERTY_TABLE(o) = NULL;

  return;
}

static int setObjPropCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  Tcl_HashTable *table;
  Tcl_HashEntry *entryPtr;
  GR_OBJ *o;
  int id;

  if (argc < 3) {
    Tcl_SetResult(interp, "usage: setObjProp objid property [value]", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "setObjProp: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }

  /* Check on table existence */
  if (!GR_PROPERTY_TABLE(o)) {
    if (argc < 4)		/* query fails since there's no table */
      goto prop_not_found;
    else {			/* want to add new prop, need table */
      GR_PROPERTY_TABLE(o) = calloc(1, sizeof(Tcl_HashTable));
      Tcl_InitHashTable(GR_PROPERTY_TABLE(o), TCL_STRING_KEYS);
    }
  }

  table = (Tcl_HashTable *) GR_PROPERTY_TABLE(o);
  
  if (argc < 4) {		/* lookup */
    if ((entryPtr = Tcl_FindHashEntry(table, argv[2]))) {
      Tcl_SetResult(interp, Tcl_GetHashValue(entryPtr), TCL_STATIC);
      return TCL_OK;
    }
    else goto prop_not_found;
  }
  else {			/* insert */
    if ((entryPtr = Tcl_FindHashEntry(table, argv[2]))) {
      if (Tcl_GetHashValue(entryPtr)) {
	free(Tcl_GetHashValue(entryPtr));
      }
      Tcl_SetHashValue(entryPtr, strdup(argv[3]));
    }
    else {
      int newentry;
      entryPtr = Tcl_CreateHashEntry(table, argv[2], &newentry);
      Tcl_SetHashValue(entryPtr, strdup(argv[3]));
    }
    Tcl_SetResult(interp, Tcl_GetHashValue(entryPtr), TCL_STATIC);
    return TCL_OK;
  }

 prop_not_found:
  Tcl_AppendResult(interp, "setObjProp: property \"", argv[2], 
		   "\" not found", NULL);
  return TCL_ERROR;
}

static int addPreScriptCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id;
  int slot;
    static char buf[8];
  
  if (argc != 3) {
    Tcl_SetResult(interp, "usage: addPreScript objid script", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "addPreScript: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }

  slot = gobjAddPreScript(o, argv[2]);
  sprintf(buf, "%d", slot);
  Tcl_SetResult(interp, buf, TCL_STATIC);

  return TCL_OK;
}

static int addPostScriptCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id, slot;
      static char buf[8];

  if (argc != 3) {
    Tcl_SetResult(interp, "usage: addPostScript objid script", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "addPostScript: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }

  slot = gobjAddPostScript(o, argv[2]);
  sprintf(buf, "%d", slot);
  Tcl_SetResult(interp, buf, TCL_STATIC);

  return TCL_OK;
}

static int addThisFrameScriptCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id, slot;
      static char buf[8];

  if (argc != 3) {
    Tcl_SetResult(interp, "usage: addThisFrameScript objid script", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "addThisScript: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }

  slot = gobjAddThisFrameScript(o, argv[2]);
  sprintf(buf, "%d", slot);
  Tcl_SetResult(interp, buf, TCL_STATIC);

  return TCL_OK;
}

static int addPostFrameScriptCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id, slot;
  static char buf[8];

  if (argc != 3) {
    Tcl_SetResult(interp, "usage: addPostFrameScript objid script", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "addPostFrameScript: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }

  slot = gobjAddPostFrameScript(o, argv[2]);
  sprintf(buf, "%d", slot);
  Tcl_SetResult(interp, buf, TCL_STATIC);

  return TCL_OK;
}

static int activateScriptCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id, slot, old;
  static char buf[64];
  
  if (argc != 3) {
    sprintf(buf, "usage: %s objid slot", argv[0]);
    Tcl_SetResult(interp, buf, TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    sprintf(buf, "%s: invalid object specified", argv[0]);;
    Tcl_SetResult(interp, buf, TCL_STATIC);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[2], &slot) != TCL_OK) return TCL_ERROR;

  if (!strcmp(argv[0], "activatePreScript")) {
    old = gobjActivatePreScript(o, slot);
  }
  else if (!strcmp(argv[0], "activatePostScript")) {
    old = gobjActivatePostScript(o, slot);
  }
  else if (!strcmp(argv[0], "activatePostFrameScript")) {
    old = gobjActivatePostFrameScript(o, slot);
  }
  else return TCL_ERROR;

  sprintf(buf, "%d", old);
  Tcl_SetResult(interp, buf, TCL_STATIC);
  return TCL_OK;
}

static int deactivateScriptCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id, slot, old;
  static char buf[64];
  
  if (argc != 3) {
    sprintf(buf, "usage: %s objid slot", argv[0]);
    Tcl_SetResult(interp, buf, TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    sprintf(buf, "%s: invalid object specified", argv[0]);;
    Tcl_SetResult(interp, buf, TCL_STATIC);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[2], &slot) != TCL_OK) return TCL_ERROR;

  if (!strcmp(argv[0], "deactivatePreScript")) {
    old = gobjDeactivatePreScript(o, slot);
  }
  else if (!strcmp(argv[0], "deactivatePostScript")) {
    old = gobjDeactivatePostScript(o, slot);
  }
  else if (!strcmp(argv[0], "deactivatePostFrameScript")) {
    old = gobjDeactivatePostFrameScript(o, slot);
  }
  else return TCL_ERROR;

  sprintf(buf, "%d", old);
  Tcl_SetResult(interp, buf, TCL_STATIC);

  return TCL_OK;
}

static int replaceScriptCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id, slot, old;
  static char buf[64];
  if (argc != 4) {
    sprintf(buf, "usage: %s objid slot", argv[0]);
    Tcl_SetResult(interp, buf, TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    sprintf(buf, "%s: invalid object specified", argv[0]);;
    Tcl_SetResult(interp, buf, TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[2], &slot) != TCL_OK) return TCL_ERROR;
  
  if (!strcmp(argv[0], "replacePreScript")) {
    old = gobjReplacePreScript(o, slot, argv[3]);
  }
  else if (!strcmp(argv[0], "replacePostScript")) {
    old = gobjReplacePostScript(o, slot, argv[3]);
  }
  else if (!strcmp(argv[0], "replacePostFrameScript")) {
    old = gobjReplacePostFrameScript(o, slot, argv[3]);
  }
  else return TCL_ERROR;

  sprintf(buf, "%d", old);
  Tcl_SetResult(interp, buf, TCL_STATIC);
  return TCL_OK;
}

static int setEyeCmd(ClientData clientData, Tcl_Interp *interp,
		     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id, left, right;
  
  if (argc != 4) {
    Tcl_SetResult(interp, "usage: setEye objid left right", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_SetResult(interp, "setEye: invalid object specified", TCL_STATIC);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[2], &left) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &right) != TCL_OK) return TCL_ERROR;
  
  gobjSetEye(o, left, right);

  return TCL_OK;
}


static int setSpinCmd(ClientData clientData, Tcl_Interp *interp,
		      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  double spin;
  
  static char *usage_message = "usage: setSpin {on|off|#(degrees)}";

  if (argc < 2) {
    Tcl_SetResult(interp, usage_message, TCL_STATIC);
    return TCL_ERROR;
  }
  if (Tcl_GetDouble(interp, argv[1], &spin) == TCL_OK) {
    objListSetSpin(olist, spin);
  }
  else return TCL_ERROR;

  return TCL_OK;
}

static int setSpinRateCmd(ClientData clientData, Tcl_Interp *interp,
			  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  double rate;
  static char *usage_message = "usage: setSpinRate rate";
  
  if (argc < 2) {
    Tcl_SetResult(interp, usage_message, TCL_STATIC);
    return TCL_ERROR;
  }

  if (Tcl_GetDouble(interp, argv[1], &rate) != TCL_OK) return TCL_ERROR;
  objListSetSpinRate(olist, rate);
  
  return TCL_OK;
}

static int setRotationCmd(ClientData clientData, Tcl_Interp *interp,
			  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  double x, y, z;
  static char *usage_message = "usage: setRotation random | x y z";
  
  if (argc < 4) {
    Tcl_SetResult(interp, usage_message, TCL_STATIC);
    return TCL_ERROR;
  }
    
  if (Tcl_GetDouble(interp, argv[1], &x) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &y) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &z) != TCL_OK) return TCL_ERROR;
  objListSetRotAxis(olist, x, y, z);
  return TCL_OK;
}


static int gobjNameCmd(ClientData clientData, Tcl_Interp *interp,
		       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id;
  
  if (argc != 2) {
    Tcl_SetResult(interp, "usage: gobjName objid", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_ResetResult(interp);
    return TCL_ERROR;
  }
  Tcl_SetResult(interp, gobjTypeName(GR_OBJTYPE(o)), TCL_STATIC);
  return TCL_OK;
}

static int gobjTypeCmd(ClientData clientData, Tcl_Interp *interp,
		       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  GR_OBJ *o;
  int id;
  static char buf[16];
  
  if (argc != 2) {
    Tcl_SetResult(interp, "usage: gobjType objid", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (findObj(interp, olist, argv[1], &id) != TCL_OK) 
    return TCL_ERROR;
  
  o = OL_OBJ(olist,id);
  if (!o) {
    Tcl_ResetResult(interp);
    return TCL_ERROR;
  }
  sprintf(buf, "%d", GR_OBJTYPE(o));
  Tcl_SetResult(interp, buf, TCL_STATIC);

  return TCL_OK;
}

static int gobjTypeNameCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int type;
  
  if (argc != 2) {
    Tcl_SetResult(interp, "usage: gobjTypeName objtype", TCL_STATIC);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[1], &type) != TCL_OK) return TCL_ERROR;
  if (type < 0 || type > 255) {
    Tcl_ResetResult(interp);
    return TCL_OK;
  }
  Tcl_SetResult(interp, gobjTypeName(type), TCL_STATIC);
  return TCL_OK;
}

static int gobjNameTypeCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  char *name;
  int i;
  static char buf[8];
  
  if (argc != 2) {
    Tcl_SetResult(interp, "usage: gobjNameType name", TCL_STATIC);
    return TCL_ERROR;
  }

  for (i = 0; i < 256; i++) {
    if (!strcmp(name = gobjTypeName(i), argv[1])) break;
  }
  if (i == 256) {
    Tcl_AppendResult(interp, argv[0], ": object type ", argv[1], " not found",
		     NULL);
    return TCL_ERROR;
  }
  sprintf(buf, "%d", i);
  Tcl_SetResult(interp, buf, TCL_STATIC);

  return TCL_OK;
}

/*****************************************************************/
/*                       Group List Funcs                        */
/*****************************************************************/

static int glistInitCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  int ngroups;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  
  if (argc < 2) {
    Tcl_SetResult(interp, "usage: glistInit ngroups", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &ngroups) != TCL_OK) 
    return TCL_ERROR;
  
  glistInit(glist, ngroups);
  return TCL_OK;
}

static int glistNGroupsCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  static char buf[8];
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  sprintf(buf, "%d", OGL_NGROUPS(glist));
  Tcl_SetResult(interp, buf, TCL_STATIC);
  return TCL_OK;
}


static int glistAddObjectCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  int slot, frame = 0, status;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;

  if (argc < 3) {
    Tcl_SetResult(interp, "usage: glistAddObject object slot [frame]",
		  TCL_STATIC);
    return TCL_ERROR;
  }
  if (Tcl_GetInt(interp, argv[2], &slot) != TCL_OK) return TCL_ERROR;
  if (slot < 0) {
    Tcl_AppendResult(interp, argv[0], ": invalid slot specified", NULL);
    return TCL_ERROR;
  }


  if (argc > 3) {
    if (Tcl_GetInt(interp, argv[3], &frame) != TCL_OK) return TCL_ERROR;
  }
  
  status = glistAddObject(glist, argv[1], slot, frame);
  switch (status) {
  case -2:
    Tcl_SetResult(interp,
		  "glistAddObject: invalid frame specified", TCL_STATIC);
    return TCL_ERROR;
    break;
  case -1:
    Tcl_SetResult(interp, "glistAddObject: invalid group specified", TCL_STATIC);
    return TCL_ERROR;
    break;
  case 0:
    Tcl_AppendResult(interp, argv[0], ": object \"", argv[1],
		     "\" not found", NULL);
    return TCL_ERROR;
    break;
  default:
    return TCL_OK;
  }
}

static int glistSetParamsCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  int slot, status;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  
  if (argc < 3) {
    Tcl_SetResult(interp, "usage: glistSetParams paramstring slot", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[2], &slot) != TCL_OK) return TCL_ERROR;
  status = glistSetParams(glist, argv[1], slot);
  switch (status) {
  case -1:
    Tcl_SetResult(interp, "glistSetParams: invalid group specified", TCL_STATIC);
    return TCL_ERROR;
    break;
  case 0:
  default:
    return TCL_OK;
  }
}

static int glistSetDynamicCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  int slot, status;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  static char buf[8];
  
  if (argc < 3) {
    Tcl_SetResult(interp, "usage: glistSetDynamic slot status", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &slot) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &status) != TCL_OK) return TCL_ERROR;
  glistSetDynamic(glist, status, slot);
  
  sprintf(buf, "%d", status);
  Tcl_SetResult(interp, buf, TCL_STATIC);

  return TCL_OK;
}

static int glistSetEyeCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  int slot, left, right;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  
  if (argc < 4) {
    Tcl_SetResult(interp, "usage: glistSetEye group [left_status right_status]", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &slot) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &left) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &right) != TCL_OK) return TCL_ERROR;
  glistSetEye(glist, slot, left, right);
  return TCL_OK;
}

static int glistSetInitCmdCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  int slot, status;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  
  if (argc < 3) {
    Tcl_SetResult(interp, "usage: glistSetInitCmd cmdstring slot", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[2], &slot) != TCL_OK) return TCL_ERROR;
  status = glistSetInitCmd(glist, argv[1], slot);
  switch (status) {
  case -1:
    Tcl_SetResult(interp, "glistSetInitCmd: invalid group specified", TCL_STATIC);
    return TCL_ERROR;
    break;
  case 0:
    Tcl_SetResult(interp, "glistSetInitCmd: error allocating space for cmd", TCL_STATIC);
    return TCL_ERROR;
    break;
  default:
    return TCL_OK;
  }
}

static int glistSetFrameInitCmdCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  int slot, frame, status;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  
  if (argc < 4) {
    Tcl_SetResult(interp, "usage: glistSetFrameInitCmd cmdstring slot frame", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[2], &slot) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &frame) != TCL_OK) return TCL_ERROR;
  
  status = glistSetFrameInitCmd(glist, argv[1], slot, frame);
  switch (status) {
  case -2:
    Tcl_SetResult(interp, "glistSetFrameInitCmd: invalid frame specified", TCL_STATIC);
    return TCL_ERROR;
    break;
  case -1:
    Tcl_SetResult(interp, "glistSetFrameInitCmd: invalid group specified", TCL_STATIC);
    return TCL_ERROR;
    break;
  case 0:
    Tcl_SetResult(interp, "glistSetFrameInitCmd: error allocating space for cmd", TCL_STATIC);
    return TCL_ERROR;
    break;
  default:
    return TCL_OK;
  }
}

static int glistSetFrameTimeCmd(ClientData clientData, Tcl_Interp *interp,
				int argc, char *argv[])
{
  int slot, frame, time, status;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  
  if (argc < 4) {
    Tcl_SetResult(interp, "usage: glistSetFrameTime slot frame time", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &slot) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &frame) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &time) != TCL_OK) return TCL_ERROR;
  
  status = glistSetFrameTime(glist, slot, frame, time);
  switch (status) {
  case -2:
    Tcl_SetResult(interp, "glistSetFrameInitCmd: invalid frame specified", TCL_STATIC);
    return TCL_ERROR;
    break;
  case -1:
    Tcl_SetResult(interp, "glistSetFrameInitCmd: invalid group specified", TCL_STATIC);
    return TCL_ERROR;
    break;
  default:
    return TCL_OK;
  }
}

static int glistSetPostFrameCmdCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  int slot, frame, status;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  
  if (argc < 4) {
    Tcl_SetResult(interp, "usage: glistSetPostFrameCmd cmdstring slot frame", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[2], &slot) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &frame) != TCL_OK) return TCL_ERROR;
  
  status = glistSetPostFrameCmd(glist, argv[1], slot, frame);
  switch (status) {
  case -2:
    Tcl_SetResult(interp, "glistSetPostFrameCmd: invalid frame specified", TCL_STATIC);
    return TCL_ERROR;
    break;
  case -1:
    Tcl_SetResult(interp, "glistSetPostFrameCmd: invalid group specified", TCL_STATIC);
    return TCL_ERROR;
    break;
  case 0:
    Tcl_SetResult(interp, "glistSetPostFrameCmd: error allocating space for cmd", TCL_STATIC);
    return TCL_ERROR;
    break;
  default:
    return TCL_OK;
  }
}

static int glistSetVisibleCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  int status;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  static char buf[8];
  
  if (argc < 2) {
    sprintf(buf, "%d", OGL_VISIBLE(glist));
    Tcl_SetResult(interp, buf, TCL_STATIC);
    return TCL_OK;
  }
  
  if (Tcl_GetInt(interp, argv[1], &status) != TCL_OK) return TCL_ERROR;
  glistSetVisible(glist, status);
  return TCL_OK;
}


static int glistGetObjectsCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  int i;
  int frame, slot;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  OBJ_GROUP *g;
  Tcl_DString objlist;
  char ibuf[16];

  if (argc < 2) {
    Tcl_SetResult(interp, "usage: glistGetObjects group [frame]", TCL_STATIC);
    return TCL_ERROR;
  }
  if (Tcl_GetInt(interp, argv[1], &slot) != TCL_OK) return TCL_ERROR;
 
  if (argc > 2) {
    if (Tcl_GetInt(interp, argv[2], &frame) != TCL_OK) return TCL_ERROR;
  }
  else frame = 0;
  
  if (slot >= OGL_NGROUPS(glist)) {
    Tcl_AppendResult(interp, argv[0], ": invalid slot specified", NULL);
    return TCL_ERROR;
  }
  g = OGL_GROUP(glist, slot);

  if (frame >= OG_NFRAMES(g)) {
    Tcl_AppendResult(interp, argv[0], ": invalid frame specified", NULL);
    return TCL_ERROR;
  }
  
  Tcl_DStringInit(&objlist);
  
  for (i = 0; i < OGF_NOBJS(g, frame); i++) {
    sprintf(ibuf, "%d", OGF_OBJID(g, i, frame));
    Tcl_DStringAppendElement(&objlist, ibuf);
  }

  Tcl_DStringResult(interp, &objlist);

  return TCL_OK;
}

static int glistGetCurObjectsCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  int i;
  int frame;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  OBJ_GROUP *g;
  Tcl_DString objlist;
  char ibuf[16];

  g = OGL_GROUP(glist, OGL_CURGROUP(glist));
  frame = OG_CURFRAME(g);

  Tcl_DStringInit(&objlist);

  for (i = 0; i < OGF_NOBJS(g, frame); i++) {
    sprintf(ibuf, "%d", OGF_OBJID(g, i, frame));
    Tcl_DStringAppendElement(&objlist, ibuf);
  }

  Tcl_DStringResult(interp, &objlist);

  return TCL_OK;
}

static int glistSetCurGroupCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  int group, frame;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  static char buf[8];

  if (argc < 2) {
    sprintf(buf, "%d", OGL_CURGROUP(glist));
    Tcl_SetResult(interp, buf, TCL_STATIC);
    return TCL_OK;
  }
  
  if (Tcl_GetInt(interp, argv[1], &group) != TCL_OK) return TCL_ERROR;

  /* If a frame was specified, then call glistSetCurGroupFrame */
  if (argc > 2) {
    if (Tcl_GetInt(interp, argv[2], &frame) != TCL_OK) return TCL_ERROR;
    if (!glistSetGroupFrame(glist, group, frame)) {
      Tcl_AppendResult(interp, argv[0], ": invalid group/frame specified", 
		       NULL);
      return TCL_ERROR;
    }
  }

  else if (!glistSetCurGroup(glist, group)) {
    Tcl_AppendResult(interp, argv[0], ": invalid group specified", NULL);
    return TCL_ERROR;
  }
  return TCL_OK;
}

static int glistSetRepeatModeCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  int group, mode;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  
  if (argc < 3) {
    Tcl_SetResult(interp, "usage: glistSetRepeatMode slot mode", TCL_STATIC);
    return TCL_ERROR;
  }
  if (Tcl_GetInt(interp, argv[1], &group) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &mode) != TCL_OK) {
    Tcl_ResetResult(interp);
    if (!strcasecmp(argv[2],"oneshot")) mode = G_ONESHOT;
    else if (!strcasecmp(argv[2],"normal")) mode = G_NORMAL;
    else if (!strcasecmp(argv[2],"single")) mode = G_SINGLE_FRAME;
    else {
      Tcl_SetResult(interp, "glistSetRepeatMode: bad mode specified", TCL_STATIC);
      return TCL_ERROR;
    }
  }
  
  if (glistSetRepeatMode(glist, group, mode)) return TCL_OK;
  else {
    Tcl_AppendResult(interp, argv[0], ": invalid group/mode specified", NULL);
    return TCL_ERROR;
  }
}

static int glistSetSwapModeCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  int group, mode;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  
  if (argc < 3) {
    Tcl_SetResult(interp, "usage: glistSetSwapMode slot mode", TCL_STATIC);
    return TCL_ERROR;
  }
  if (Tcl_GetInt(interp, argv[1], &group) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[2], &mode) != TCL_OK) {
    Tcl_ResetResult(interp);
    if (!strcasecmp(argv[2],"normal")) mode = SWAP_NORMAL;
    else if (!strcasecmp(argv[2],"noswap")) mode = NO_SWAP;
    else if (!strcasecmp(argv[2],"swaponly")) mode = SWAP_ONLY;
    else {
      Tcl_SetResult(interp, "glistSetSwapMode: bad mode specified", TCL_STATIC);
      return TCL_ERROR;
    }
  }
  
  if (glistSetSwapMode(glist, group, mode)) return TCL_OK;
  else {
    Tcl_AppendResult(interp, argv[0], ": invalid group/mode specified", NULL);
    return TCL_ERROR;
  }
}

static int glistNextFrameCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  if (OGL_VISIBLE(GList)) {
    glistNextGroupFrame(GList, OGL_CURGROUP(GList));
  }
  return TCL_OK;
}

static int glistOneShotActiveCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  static char buf[8];

  if (OGL_VISIBLE(GList)) {
    sprintf(buf, "%d", glistOneShotActive(GList, OGL_CURGROUP(GList)));
  }
  else {
    sprintf(buf, "0");
  }
  Tcl_SetResult(interp, buf, TCL_STATIC);
  return TCL_OK;
}

static int glistDumpCmd(ClientData clientData, Tcl_Interp *interp,
					 int argc, char *argv[])
{
  int i, j, k, m;
  GR_OBJ *obj;
  OBJ_GROUP_LIST *glist = (OBJ_GROUP_LIST *) clientData;
  OBJ_GROUP *g;
  char buf[1024];
  FILE *OutputConsole = stdout;

  fprintf(OutputConsole, "GLIST_BEGIN\n");
  for (i = 0; i < OGL_NGROUPS(glist); i++) {
    g = OGL_GROUP(glist, i);
    sprintf(buf, "GROUP\t%d\n", i);
    logMessage(buf);
    if (strcmp(OG_PARAMS(g),"")) {
      sprintf(buf, "PARAMS\t%s\n", OG_PARAMS(g));
      logMessage(buf);
    }
    if (OG_INITCMD(g)) {
      sprintf(buf, "INITCMD\t%s\n", OG_INITCMD(g));
      logMessage(buf);
    }
    for (j = 0; j < OG_NFRAMES(g); j++) {
      sprintf(buf, "FRAME\t%d\n", j);
      logMessage(buf);
      if (OGF_INITCMD(g,j)) {
	sprintf(buf, "FRAME INITCMD\t%s\n", OGF_INITCMD(g,j));
	logMessage(buf);
      }
      for (k = 0; k < OGF_NOBJS(g,j); k++) {
	obj = OL_OBJ(OBJList, OGF_OBJID(g,k,j));
	if (!obj) continue;
	sprintf(buf, "OBJECT\t%-2d\t%s\n", OGF_OBJID(g,k,j), GR_NAME(obj));
	logMessage(buf);
	for (m = 0; m < GR_N_PRE_SCRIPTS(obj); m++) {
	  sprintf(buf, " PRE [%d]\t%s\n", m, GR_PRE_SCRIPT(obj, m));
	  logMessage(buf);
	}
	sprintf(buf, " SCALE\t%-5.2f\t%-5.2f\t%-5.2f\n", 
		GR_SX(obj), GR_SY(obj), GR_SZ(obj));
	logMessage(buf);
	sprintf(buf, " TRANS\t%-5.2f\t%-5.2f\t%-5.2f\n", 
		GR_TX(obj), GR_TY(obj), GR_TZ(obj));
	logMessage(buf);
	for (m = 0; m < GR_N_POST_SCRIPTS(obj); m++) {
	  sprintf(buf, " POST [%d]\t%s\n", m, GR_POST_SCRIPT(obj, m));
	  logMessage(buf);
	}
	for (m = 0; m < GR_N_POSTFRAME_SCRIPTS(obj); m++) {
	  sprintf(buf, " POSTFRAME [%d]\t%s\n", m, GR_POSTFRAME_SCRIPT(obj, m));
	  logMessage(buf);
	}
      }
      if (OGF_POSTCMD(g,j)) {
	sprintf(buf, "FRAME POSTCMD\t%s\n", OGF_POSTCMD(g,j));
	logMessage(buf);
      }
      fprintf(OutputConsole, "\n");
    }
  }
  fprintf(OutputConsole, "GLIST_END\n");
  return TCL_OK;
}

/*****************************************************************/
/*                       Miscellaneous Funcs                     */
/*****************************************************************/

static int doutCmd(ClientData clientData, Tcl_Interp *interp,
		   int argc, char *argv[])
{
  return TCL_OK;
}

static int dpulseCmd(ClientData clientData, Tcl_Interp *interp,
		    int argc, char *argv[])
{
  return TCL_OK;
}

static int wakeupCmd(ClientData clientData, Tcl_Interp *interp,
		     int argc, char *argv[])
{
  if (argc != 2) {
    Tcl_SetResult(interp, "usage: wakeup ms", TCL_STATIC);
     return TCL_ERROR;
  }
  else {
    int ms;
    if (Tcl_GetInt(interp, argv[1], &ms) != TCL_OK) return TCL_ERROR;
    setWakeUp(ms);
    return TCL_OK;
  }
}

/*****************************************************************/
/*                       Obs Spec List Funcs                     */
/*****************************************************************/

static int olistInitCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  int ngroups;
  OBS_SPEC_LIST *olist = (OBS_SPEC_LIST *) clientData;
  
  if (argc < 2) {
    Tcl_SetResult(interp, "usage: olistInit ngroups", TCL_STATIC);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &ngroups) != TCL_OK) 
    return TCL_ERROR;
  
  olistInit(olist, ngroups);
  return TCL_OK;
}

static int olistAddSpecCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  int i, j, slot;
  OBS_SPEC_LIST *olist = (OBS_SPEC_LIST *) clientData;
  OBS_PERIOD_SPEC *ospec;
  Tcl_Size listArgc, sublistArgc;
  Tcl_Size ntimes = 0;
  char **listArgv, **sublistArgv;
  int *choices, *times;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage:", argv[0], "spec slot", NULL);
    return TCL_ERROR;
  }
  
  if (argc < 4) {
    if (Tcl_GetInt(interp, argv[2], &slot) != TCL_OK) return TCL_ERROR;
  }
  else {
    if (Tcl_GetInt(interp, argv[3], &slot) != TCL_OK) return TCL_ERROR;
  }
  
  if (slot >= OSL_N(olist)) {
    Tcl_AppendResult(interp, argv[0], ": invalid slot specified", NULL);
    return TCL_ERROR;
  }
  /* 
   * Right now just check the second list if it exists just to make sure
   * it's ok.
   */
  if (argc > 3) {
    if (Tcl_SplitList(interp, argv[2], &ntimes,
		      (const char ***) &listArgv) != TCL_OK) {
      return TCL_ERROR;
    }
    else free((void *) listArgv);
  }
  
  
  if (Tcl_SplitList(interp, argv[1], &listArgc,
		    (const char ***) &listArgv) != TCL_OK) {
    return TCL_ERROR;
  }
  if (ntimes && ntimes != listArgc) {
    free((void *) listArgv);
    Tcl_AppendResult(interp, argv[0], ": number of times and specs must be equal", 
		     NULL);
	return TCL_ERROR;
  }
  
  ospec = olistCreateSpec(olist, slot, listArgc);
  for (i = 0; i < listArgc; i++) {
    if (Tcl_SplitList(interp, listArgv[i], 
		      &sublistArgc, (const char ***) &sublistArgv) != TCL_OK) {
      free(listArgv);
      return TCL_ERROR;
    }
    choices = (int *) calloc(sublistArgc, sizeof(int));
    for (j = 0; j < sublistArgc; j++) {
      if (Tcl_GetInt(interp, sublistArgv[j], &choices[j]) != TCL_OK) {
	free(sublistArgv);
	free(listArgv);
	return TCL_ERROR;
      }
    }
    olistFillSpecSlot(ospec, i, sublistArgc, choices);
    free(choices);
    free(sublistArgv);
  }
  free(listArgv);
  
  if (argc > 3) {
    Tcl_SplitList(interp, argv[2], &listArgc, (const char ***) &listArgv);
    for (i = 0; i < listArgc; i++) {
      if (Tcl_SplitList(interp, listArgv[i], &sublistArgc,
			(const char ***) &sublistArgv) != TCL_OK) {
	free(listArgv);
	return TCL_ERROR;
      }
      times = (int *) calloc(sublistArgc, sizeof(int));
      for (j = 0; j < sublistArgc; j++) {
	if (Tcl_GetInt(interp, sublistArgv[j], &times[j]) != TCL_OK) {
	  free(sublistArgv);
	  free(listArgv);
	  return TCL_ERROR;
	}
      }
      olistFillSpecTime(ospec, i, sublistArgc, times);
      free(times);
      free(sublistArgv);
    }
    free(listArgv);
  }
  return TCL_OK;
}

static int olistDumpCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  int i, j, k;
  OBS_SPEC_LIST *olist = (OBS_SPEC_LIST *) clientData;
  OBS_PERIOD_SPEC *ospec;
  Tcl_Channel outChannel = Tcl_GetStdChannel(TCL_STDOUT);
  char buf[128];

  /* Optional first argument is the name of an open file descriptor */
  if (argc > 1) {
    int mode;
    if ((outChannel = Tcl_GetChannel(interp, argv[1], &mode)) == NULL) {
      return TCL_ERROR;
    }
  }

  Tcl_Write(outChannel, "OBS PERIOD SPECS:\n", -1);
  for (i = 0; i < OSL_N(olist); i++) {
    ospec = OSL_SPEC(olist, i);
    sprintf(buf, "SPEC\t%d\n", i);
    Tcl_Write(outChannel, buf, -1);
    if (!ospec) continue;
    for (j = 0; j < OP_N(ospec); j++) {
      sprintf(buf, "SLOT_GROUPS: %d { ", j);
      Tcl_Write(outChannel, buf, -1);
      for (k = 0; k < OP_NCHOICES(ospec, j); k++) {
	sprintf(buf, "%d ", OP_SLOT_ELT(ospec, j, k));
	Tcl_Write(outChannel, buf, -1);
      }
      Tcl_Write(outChannel, "}\n", -1);
      
      sprintf(buf, "SLOT_TIMES:  %d { ", j);
      Tcl_Write(outChannel, buf, -1);
      for (k = 0; k < OP_NTIMES(ospec, j); k++) {
	sprintf(buf, "%d ", OP_TIME_ELT(ospec, j, k));
	Tcl_Write(outChannel, buf, -1);
      }
      Tcl_Write(outChannel, "}\n", -1);
    }
  }
  return TCL_OK;
}

/***********************************************************************/

static int expGetSetCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  PARAM_ENTRY *configTable = (PARAM_ENTRY *) clientData;
  char *pnames;
  static char buf[96];

  if (Tcl_StringMatch(argv[0], "*_dump")) {
    int i;
    char *keys, **listArgv;
    Tcl_Size listArgc;
    Tcl_Channel outChannel = Tcl_GetStdChannel(TCL_STDOUT);

    /* Optional first argument is the name of an open file descriptor */
    if (argc > 1) {
      int mode;
      if ((outChannel = Tcl_GetChannel(interp, argv[1], &mode)) == NULL) {
	return TCL_ERROR;
      }
    }

    keys = puVarList(configTable);
    if (!keys) return TCL_OK;
    
    if (Tcl_SplitList(interp, keys, &listArgc,
		      (const char ***) &listArgv) != TCL_OK) {
      free(keys);
      return TCL_ERROR;
    }
    
    for (i = 0; i < listArgc; i++) {
      sprintf(buf, "%-20s %s\n", 
	      listArgv[i], puGetParamEntry(configTable, listArgv[i]));
      Tcl_Write(outChannel, buf, -1);
    }
    
    free(keys);
    Tcl_Free((char *) listArgv);
    
    return TCL_OK;
  }
  
  switch (argc) {
  case 1:
    pnames = puVarList(configTable);
    Tcl_SetResult(interp, pnames, TCL_VOLATILE);
    free(pnames);
    break;
  case 2:
    {
      char *result = puGetParamEntry(configTable, argv[1]);
      if (result)
	Tcl_SetResult(interp, result, TCL_VOLATILE);
      else {
	sprintf(buf, "eset: no such variable \"%s\"", argv[1]);
	Tcl_SetResult(interp, buf, TCL_STATIC);
	return TCL_ERROR;
      }
    }
    break;
  default:
    {
      int result = puSetParamEntry(configTable, argv[1], argc-2, &argv[2]);
      if (result)
	Tcl_SetResult(interp, puGetParamEntry(configTable, argv[1]),
		      TCL_STATIC);
      else {
	sprintf(buf, "eset: no such variable \"%s\"", argv[1]);
	Tcl_SetResult(interp, buf, TCL_STATIC);
	return TCL_ERROR;
      }
    }
    break;
  }
  return TCL_OK;
}

#ifdef EMBED_PYTHON
static int execPythonCmdCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  if (argc != 2) {
    Tcl_SetResult(interp, "usage: execPythonCmd script", TCL_STATIC);
    return TCL_ERROR;
  }

  execPythonCmd(argv[1]);

  return TCL_OK;
}
#endif

void addTclCommands(Tcl_Interp *interp)
{
  extern OBJ_LIST *OBJList;
  extern PARAM_ENTRY ScreenParamTable[];

  /* Init Commands */
  Tcl_CreateCommand(interp, "setsystem", (Tcl_CmdProc *) setsystemCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "ping", (Tcl_CmdProc *) pingCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

#ifndef NO_EXIT_COMMANDS
  /* Exit Commands */
  Tcl_CreateCommand(interp, "exit", (Tcl_CmdProc *) exitCmd, 
  	    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "quit", (Tcl_CmdProc *) exitCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
#endif

  Tcl_CreateCommand(interp, "resetGraphicsState", 
		    (Tcl_CmdProc *) resetGraphicsStateCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

 /* Dump Screen as Raw */

  Tcl_CreateCommand(interp, "dumpRaw", (Tcl_CmdProc *) dumpCmd, 
		    (ClientData) DUMP_RAW, (Tcl_CmdDeleteProc *) NULL);

 /* Dump Screen as PS */

  Tcl_CreateCommand(interp, "dumpPS", (Tcl_CmdProc *) dumpCmd, 
		    (ClientData) DUMP_PS, (Tcl_CmdDeleteProc *) NULL);

 /* Animation */

  Tcl_CreateCommand(interp, "toggleAnimation", 
		    (Tcl_CmdProc *) toggleAnimationCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "startAnimation", 
		    (Tcl_CmdProc *) startAnimationCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "stopAnimation", (Tcl_CmdProc *) stopAnimationCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "kickAnimation", (Tcl_CmdProc *) kickAnimationCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  /* Stereo Mode */
  Tcl_CreateCommand(interp, "setStereoMode", (Tcl_CmdProc *) setStereoModeCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  /* Set colors */

  Tcl_CreateCommand(interp, "setBackground", (Tcl_CmdProc *) setBackgroundCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);  

  /* Verbosity Level */
  Tcl_CreateCommand(interp, "setVerboseLevel",
		    (Tcl_CmdProc *) setVerboseLevelCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  
  /* Toggle gui */
  Tcl_CreateCommand(interp, "toggleImgui", (Tcl_CmdProc *) toggleImguiCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  /* Log to imgui */
  Tcl_CreateCommand(interp, "logMessage", (Tcl_CmdProc *) logMessageCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  
  /* Show/set cursor */

  Tcl_CreateCommand(interp, "showCursor", (Tcl_CmdProc *) showCursorCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "hideCursor", (Tcl_CmdProc *) hideCursorCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "setCursorPos", (Tcl_CmdProc *) setCursorPosCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  /* Load / Unload graphics objects */
  
  Tcl_CreateCommand(interp, "resetObjList", (Tcl_CmdProc *) resetObjListCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "unloadObj", (Tcl_CmdProc *) unloadObjCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "nullObj", (Tcl_CmdProc *) nullObjCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  /* List based commands */

  Tcl_CreateCommand(interp, "setTranslate", (Tcl_CmdProc *) translateObjListCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "setSpin", (Tcl_CmdProc *) setSpinCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "setRotation", (Tcl_CmdProc *) setRotationCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "setSpinRate", (Tcl_CmdProc *) setSpinRateCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  /* Object based commands */

  Tcl_CreateCommand(interp, "setVisible", (Tcl_CmdProc *) setVisibleCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "setEye", (Tcl_CmdProc *) setEyeCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "translateObj", (Tcl_CmdProc *) translateObjCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "scaleObj", (Tcl_CmdProc *) scaleObjCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "rotateObj", (Tcl_CmdProc *) rotateObjCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "resetObj", (Tcl_CmdProc *) resetObjCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "setProjMatrix", (Tcl_CmdProc *) setProjMatrixCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  
  Tcl_CreateCommand(interp, "setObjMatrix", (Tcl_CmdProc *) setObjMatrixCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "useObjMatrix", (Tcl_CmdProc *) useObjMatrixCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "setObjProp", (Tcl_CmdProc *) setObjPropCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  
  Tcl_CreateCommand(interp, "addPreScript", (Tcl_CmdProc *) addPreScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "addPostScript", (Tcl_CmdProc *) addPostScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "addThisFrameScript",
		    (Tcl_CmdProc *) addThisFrameScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "addPostFrameScript", 
		    (Tcl_CmdProc *) addPostFrameScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "activatePreScript",
		    (Tcl_CmdProc *) activateScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "activatePostScript",
		    (Tcl_CmdProc *) activateScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "activatePostFrameScript", 
		    (Tcl_CmdProc *) activateScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);  

  Tcl_CreateCommand(interp, "deactivatePreScript",
		    (Tcl_CmdProc *) deactivateScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "deactivatePostScript",
		    (Tcl_CmdProc *) deactivateScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "deactivatePostFrameScript", 
		    (Tcl_CmdProc *) deactivateScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);  

  Tcl_CreateCommand(interp, "replacePreScript",
		    (Tcl_CmdProc *) replaceScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "replacePostScript",
		    (Tcl_CmdProc *) replaceScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "replacePostFrameScript", 
		    (Tcl_CmdProc *) replaceScriptCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);  


  /* Object info commands */

  Tcl_CreateCommand(interp, "gobjName", (Tcl_CmdProc *) gobjNameCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "gobjType", (Tcl_CmdProc *) gobjTypeCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "gobjTypeName", (Tcl_CmdProc *) gobjTypeNameCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "gobjNameType", (Tcl_CmdProc *) gobjNameTypeCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  
  /* Group commands */
  
  Tcl_CreateCommand(interp, "glistInit", (Tcl_CmdProc *) glistInitCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistNGroups", (Tcl_CmdProc *) glistNGroupsCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistAddObject", 
		    (Tcl_CmdProc *) glistAddObjectCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistSetEye", (Tcl_CmdProc *) glistSetEyeCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistSetParams", 
		    (Tcl_CmdProc *) glistSetParamsCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistSetDynamic", 
		    (Tcl_CmdProc *) glistSetDynamicCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistSetFrameInitCmd", 
		    (Tcl_CmdProc *) glistSetFrameInitCmdCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistSetPostFrameCmd", 
		    (Tcl_CmdProc *) glistSetPostFrameCmdCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistSetFrameTime", 
		    (Tcl_CmdProc *) glistSetFrameTimeCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistSetInitCmd", 
		    (Tcl_CmdProc *) glistSetInitCmdCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistSetRepeatMode", 
		    (Tcl_CmdProc *) glistSetRepeatModeCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistSetSwapMode", 
		    (Tcl_CmdProc *) glistSetSwapModeCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistSetVisible", 
		    (Tcl_CmdProc *) glistSetVisibleCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistSetCurGroup", 
		    (Tcl_CmdProc *) glistSetCurGroupCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistGetCurObjects", 
		    (Tcl_CmdProc *) glistGetCurObjectsCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistGetObjects", 
		    (Tcl_CmdProc *) glistGetObjectsCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistNextFrame", 
		    (Tcl_CmdProc *) glistNextFrameCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistOneShotActive", 
		    (Tcl_CmdProc *) glistOneShotActiveCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "glistDump", (Tcl_CmdProc *) glistDumpCmd,
		    (ClientData) GList, (Tcl_CmdDeleteProc *) NULL);


  /* Obs Period Spec Commands */

  Tcl_CreateCommand(interp, "olistInit", (Tcl_CmdProc *) olistInitCmd,
		    (ClientData) OList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "olistAddSpec", (Tcl_CmdProc *) olistAddSpecCmd,
		    (ClientData) OList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "olistDump", (Tcl_CmdProc *) olistDumpCmd,
		    (ClientData) OList, (Tcl_CmdDeleteProc *) NULL);
  
  /* General commands */
  
  Tcl_CreateCommand(interp, "redraw", (Tcl_CmdProc *) redrawCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "reshape", (Tcl_CmdProc *) reshapeCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  /* Misc commands */

  Tcl_CreateCommand(interp, "dout", (Tcl_CmdProc *) doutCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "dpulse", (Tcl_CmdProc *) dpulseCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "wakeup", (Tcl_CmdProc *) wakeupCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

#ifdef EMBED_PYTHON
  Tcl_CreateCommand(interp, "exec_python", (Tcl_CmdProc *) execPythonCmdCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
#endif
  
  /* Linked global variables */
  
  Tcl_LinkVar(interp, "StimVersion", (char *) &StimVersion, TCL_LINK_INT);
  Tcl_LinkVar(interp, "StimTime", (char *) &StimTime, TCL_LINK_INT);
  Tcl_LinkVar(interp, "StimTicks", (char *) &StimTicks, TCL_LINK_INT);
  Tcl_LinkVar(interp, "StimVRetraceCount", (char *) &StimVRetraceCount, TCL_LINK_INT);
  Tcl_LinkVar(interp, "NextFrameTime", (char *) &NextFrameTime, TCL_LINK_INT);
  Tcl_LinkVar(interp, "SwapPulse", (char *) &SwapPulse, TCL_LINK_INT);
  Tcl_LinkVar(interp, "SwapAcknowledge", (char *) 
	      &SwapAcknowledge, TCL_LINK_INT);
  Tcl_LinkVar(interp, "SwapCount", (char *) 
	      &SwapCount, TCL_LINK_INT);
  Tcl_LinkVar(interp, "StereoMode", (char *) &StereoMode, TCL_LINK_INT);
  Tcl_LinkVar(interp, "BlockMode", (char *) &BlockMode, TCL_LINK_INT);


  Tcl_LinkVar(interp, "MouseXPos", (char *) &MouseXPos, TCL_LINK_INT);
  Tcl_LinkVar(interp, "MouseYPos", (char *) &MouseYPos, TCL_LINK_INT);

  tclAddParamTable(interp, ScreenParamTable, "screen");
}

/*********************************************************************/
/*                      Interface to Interp                          */
/*********************************************************************/

void tclAddParamTable(Tcl_Interp *interp, PARAM_ENTRY *table, char *name)
{
  char command_name[64];
  sprintf(command_name, "%s_set", name);
  Tcl_CreateCommand(interp, command_name, (Tcl_CmdProc *) expGetSetCmd, 
		    (ClientData) table, (Tcl_CmdDeleteProc *) NULL);
  sprintf(command_name, "%s_dump", name);
  Tcl_CreateCommand(interp, command_name, (Tcl_CmdProc *) expGetSetCmd, 
		    (ClientData) table, (Tcl_CmdDeleteProc *) NULL);
}

