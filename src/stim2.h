/*
 * FILE
 *   stim2.h
 *
 */

#ifndef STIM2_H
#define STIM2_H

#include "prmutil.h"

#if defined(WIN32)
#pragma warning (disable:4244)
#pragma warning (disable:4305)
#pragma warning (disable:4005)
#pragma warning (disable:4550)
#pragma warning (disable:4996)
#define _CRT_SECURE_NO_WARNINGS
#define DllEntryPoint DllMain
#define EXPORT(a,b) __declspec(dllexport) a b
#else
#define DllEntryPoint
#define EXPORT a b
#endif

#if !defined (WM_XBUTTONDOWN)
#define WM_XBUTTONDOWN                  0x020B
#define WM_XBUTTONUP                    0x020C
/* XButton values are WORD flags */
#define GET_XBUTTON_WPARAM(wParam)      (HIWORD(wParam))
#define XBUTTON1      0x0001
#define XBUTTON2      0x0002
#endif

/* Virtual Keys for Windows */
#define STIMK_LBUTTON        0x01
#define STIMK_RBUTTON        0x02
#define STIMK_CANCEL         0x03
#define STIMK_MBUTTON        0x04    /* NOT contiguous with L & RBUTTON */
#define STIMK_BACK           0x08
#define STIMK_TAB            0x09
#define STIMK_CLEAR          0x0C
#define STIMK_RETURN         0x0D
#define STIMK_SHIFT          0x10
#define STIMK_CONTROL        0x11
#define STIMK_MENU           0x12
#define STIMK_PAUSE          0x13
#define STIMK_CAPITAL        0x14
#define STIMK_ESCAPE         0x1B
#define STIMK_SPACE          0x20
#define STIMK_PRIOR          0x21
#define STIMK_NEXT           0x22
#define STIMK_END            0x23
#define STIMK_HOME           0x24
#define STIMK_LEFT           0x25
#define STIMK_UP             0x26
#define STIMK_RIGHT          0x27
#define STIMK_DOWN           0x28

#define STIM_PORT 4610		/* Pretty darn random */
#define SOCK_BUF_SIZE 65536

enum { STIM_MODELVIEW_MATRIX, STIM_PROJECTION_MATRIX, STIM_MVP_MATRIX, STIM_NORMAL_MATRIX };
enum { STIM_PRE_SCRIPT, STIM_POST_SCRIPT, STIM_POSTFRAME_SCRIPT, STIM_THISFRAME_SCRIPT };

enum { NO_SWAP,
       SWAP_NORMAL,
       SWAP_ONLY };

enum { MSG_SOCKET = 0x0400+1,
       MSG_DISPLAY,
       MSG_CLIENT,
       MSG_DATASERVER
};

enum { UPDATE_DISPLAY,
       UPDATE_DISPLAY_ACKNOWLEDGE,
       UPDATE_OVERLAY,
       SET_BACKGROUND,
       INIT_OBJECT,
       DELETE_OBJECT,
       SWAP_EVENT,
       DIGITAL_OUT,
       SHOW_CURSOR,
       HIDE_CURSOR,
       SET_CURSOR_POS,
       DUMP_RAW,
       DUMP_PS,
       SET_OVERLAY_COLORS,
       SET_OVERLAY_BACKGROUND,
       DRAW_TO_OFFSCREEN_BUFFER,
       CREATE_OFFSCREEN_BUFFER,
       DELETE_OFFSCREEN_BUFFER,
       RESHAPE_DISPLAY,
       TOGGLE_IMGUI,
};

enum {
  NOT_DYNAMIC = 0,
  DYNAMIC_FRAME_BASED,
  DYNAMIC_TIME_BASED,
  DYNAMIC_WAKEUP_BASED,
  DYNAMIC_ALWAYS_UPDATE };

typedef struct _dumpinfo { 
  char filename[128];
  int x;			/* left corner     */
  int y;			/* top corner      */
  int w;			/* width (pixels)  */
  int h;			/* height (pixels) */
} DUMP_INFO;

#ifdef __cplusplus
extern "C" {
#endif

extern int Argc;
extern char **Argv;

extern int ClearBackground;	/* Automatically clear on each update */
extern int ClearOverlay;	/* Automatically clear on each update */

/* Linked variables */
extern unsigned int StimVersion;/* Stim version                 */
extern unsigned int StimTime;	/* Stim time counter            */
extern unsigned int StimTicks;	/* Free running counter         */
extern unsigned int StimVRetraceCount;   /* Counts vretraces    */
extern unsigned int StimDeltaTime;  /* ms since last frame      */
extern int NextFrameTime;       /* When does next frame start   */
extern int SwapCount;	        /* Do a swap pulse every swap?  */
extern int SwapPulse;	        /* Do a swap pulse every swap?  */
extern int SwapAcknowledge;     /* Pulse at next swap?          */
extern int StereoMode;		/* 0 - none, 1 - split screen   */
extern int BlockMode;		/* 1, 2, or 3 - see WaitForSwap */

extern int MouseXPos;		/* current mouse X position     */
extern int MouseYPos;		/* current mouse Y position     */

extern int DebugTimerExec;	  /* for debugging */

extern unsigned char pulseOn;   /* For doing a pulse            */
extern unsigned char PulsePreset; /* Flag for pulse tracking    */
extern unsigned int OutBits;	/* Current state of dig out     */

extern float BackgroundColor[];
extern float GreenBackgroundColor[]; /* color for reward screen */
extern float RedBackgroundColor[];   /* color for punish screen */
extern int OverlayBackgroundColor; /* Index to clear overlay to */
extern int CurWidth, CurHeight;

extern DUMP_INFO DumpInfo;	/* info for screen dump */
  
#ifdef __cplusplus
}
#endif

typedef struct _ovinfo {
  int start;			/* start index     */
  int nentries;			/* number to set   */
  int entries[256];		/* color values    */
} OVINFO;

extern OVINFO OVInfo;		/* keep overlay colors here            */

/* This is a function that will be called when stim exits */
typedef void (*SHUTDOWN_FUNC)(void *);

#ifdef __cplusplus
extern "C" {
#endif

extern void postDispMsg(int event);
extern void postDispMsgWithData(int event, int data);
extern void sendDispMsg(int event);
extern void sendDispMsgWithData(int event, int data);
extern float *getBackgroundColor(void);
extern int getHWND(void);
extern int getSwapCount(void);
extern FILE *getConsoleFP(void);
extern char *getWGLExtensions(void);
extern int getOverlayStatus(void);
extern int getCurrentEye(void);
extern int getStimTime(void);
extern double getFrameDuration(void);
extern int getStereoMode(void);
extern int setStereoMode(int);
extern int *getParamTable(void);
extern int getScreenInfo(int *, int *, int *, int *, float *);
extern void resetGraphicsState(void);

void kill_window(void);
void add_shutdown_func(SHUTDOWN_FUNC func, void *clientdata);
void redraw(void);
void reshape(void);
void activate_window(void);
void do_overlay(void);
void draw_to_offscreen_buffer(int);

void sendAckProc(void *);
void getTclExec(void);
void releaseTclExec(void);

char *sendTclCommand(char *command);
int  evalTclCommand(const char *command, char **resultPtr);
int  sendTclKeyboardCommand(int key);
int  sendTclKeyboardUpCommand(int key);
int  sendTclMouseCommand(int key, int down);
int  sendTclMouseXCommand(int key, int down);
int  sendTclMouseMoveCommand(int x, int y);

extern int setWakeUp(int ms);
extern int digitalOut(int val, int mask);

int  stimMessage(char *, int);
void addLog(char *, ...);
int setVerboseLevel(int);
void logMessage(char *message);
void doToggleImgui(void);
  
int  toggleAnimation(void);
int  startAnimation(void);
int  stopAnimation(void);
int  kickAnimation(void);
void startAnimationProc(void *ClientData);
int setDynamicUpdate(int status);
int resetSwapInterval(int interval);
int setSwapInterval(int interval);
int setSwapIntervalVal(int interval);
int setAutoClear(int mode);
void resetStimTime(void);
void setBackgroundColor(void);
void setBackgroundColorVals(float, float, float, float);
void setOverlayBackgroundColor(int index);
void executeScripts(char **scripts, int *actives, int n);

extern PARAM_ENTRY ScreenParamTable[];

#ifdef EMBED_PYTHON
void execPythonCmd(char *cmd);
#endif
  
#ifdef __cplusplus
}
#endif


/*==================================================================*/

#define GR_DEFAULT_GROBJS      (10)

struct _grobj;

typedef void (*ACTION_FUNC)(struct _grobj *);
typedef void (*INIT_FUNC)(struct _grobj *);
typedef void (*UPDATE_FUNC)(struct _grobj *);
typedef void (*DELETE_FUNC)(struct _grobj *);
typedef void (*RESET_FUNC)(struct _grobj *);
typedef void (*TIMER_FUNC)(struct _grobj *);
typedef void (*IDLE_FUNC)(struct _grobj *);
typedef void (*OFF_FUNC)(struct _grobj *);

/********************************************************************
 * Structure: GR_OBJ
 * Purpose: Hold info pertaining to a single graphics object
 * Refers To: 
 * Contained In: OBJ_LIST
 ********************************************************************/

#define MAXSCRIPTS 32

typedef struct _grobj {
  char name[64];		/* handle for the object            */
  int visible;                  /* is the object visible?           */
  float position[3];		/* position of individual object    */
  float scale[3];		/* scale of individual object       */
  float rotation[3];            /* rotation axis for individual obj */
  float spin;                   /* amount of spin for indiviudal obj*/
  int eye[2];			/* which eyes are vis if in stereo  */
  ACTION_FUNC actionfunc;	/* user defined action function     */
  DELETE_FUNC deletefunc;	/* user defined delete function     */
  RESET_FUNC resetfunc;	        /* user defined reset function      */
  INIT_FUNC initfunc;	        /* init func for graphics thread    */
  UPDATE_FUNC updatefunc;	/* user defined update function     */
  TIMER_FUNC timerfunc;	        /* user defined timer function      */
  IDLE_FUNC idlefunc;		/* a function to run when idle      */
  OFF_FUNC offfunc;		/* run when stim is turned off      */
  void *clientData;		/* user defined client data         */
  char objtype;			/* object id type                   */
  char *pre_scripts[MAXSCRIPTS];/* scripts to run before each draw  */
  int   pre_script_active[MAXSCRIPTS];  /* is this slot active      */
  int n_pre_scripts;		/* number of pre_scripts            */
  char *post_scripts[MAXSCRIPTS];/* scripts to run after each draw  */
  int   post_script_active[MAXSCRIPTS];  /* is this slot active     */
  int n_post_scripts;		/* number of post_scripts           */
  char *postframe_scripts[MAXSCRIPTS];/* scripts to run after frame */
  int   postframe_script_active[MAXSCRIPTS];  /* is this slot active*/
  int n_postframe_scripts;	/* number of postframe_scripts      */
  char *thisframe_scripts[MAXSCRIPTS];/* scripts to run this frame  */
  int n_thisframe_scripts;	/* number of thisframe_scripts      */
  int flags;			/* reserved flags                   */
  void *reservedptr;		/* reserved pointer                 */
  void *property_table;		/* list of obj specific props/vals  */
  int   use_matrix;		/* use tmatrix instead of r/s/t?    */
  float matrix[16];		/* transformation matrix            */
  int drawcount;		/* number of draws since reset      */
  void *anim_state;             /* animation state pointer          */
} GR_OBJ;

#define GR_NAME(o)         ((o)->name)
#define GR_OBJTYPE(o)      ((o)->objtype)
#define GR_VISIBLE(o)      ((o)->visible)
#define GR_TX(o)           ((o)->position[0])
#define GR_TY(o)           ((o)->position[1])
#define GR_TZ(o)           ((o)->position[2])
#define GR_SX(o)           ((o)->scale[0])
#define GR_SY(o)           ((o)->scale[1])
#define GR_SZ(o)           ((o)->scale[2])
#define GR_AX1(o)          ((o)->rotation[0])
#define GR_AX2(o)          ((o)->rotation[1])
#define GR_AX3(o)          ((o)->rotation[2])
#define GR_SPIN(o)         ((o)->spin)

#define GR_USEMATRIX(o)    ((o)->use_matrix)
#define GR_MATRIX(o)       (&(o)->matrix[0])

#define GR_COUNT(o)        ((o)->drawcount)

#define GR_LEFT_EYE(o)     ((o)->eye[0])
#define GR_RIGHT_EYE(o)    ((o)->eye[1])

#define GR_ACTIONFUNC(o)   (*((o)->actionfunc))
#define GR_ACTIONFUNCP(o)  ((o)->actionfunc)
#define GR_UPDATEFUNC(o)   (*((o)->updatefunc))
#define GR_UPDATEFUNCP(o)  ((o)->updatefunc)
#define GR_DELETEFUNC(o)   (*((o)->deletefunc))
#define GR_DELETEFUNCP(o)  ((o)->deletefunc)
#define GR_INITFUNC(o)     (*((o)->initfunc))
#define GR_INITFUNCP(o)    ((o)->initfunc)
#define GR_RESETFUNC(o)    (*((o)->resetfunc))
#define GR_RESETFUNCP(o)   ((o)->resetfunc)
#define GR_TIMERFUNC(o)    (*((o)->timerfunc))
#define GR_TIMERFUNCP(o)   ((o)->timerfunc)
#define GR_IDLEFUNC(o)     (*((o)->idlefunc))
#define GR_IDLEFUNCP(o)    ((o)->idlefunc)
#define GR_OFFFUNC(o)      (*((o)->offfunc))
#define GR_OFFFUNCP(o)     ((o)->offfunc)
#define GR_CLIENTDATA(o)   ((o)->clientData)

#define GR_N_PRE_SCRIPTS(o) ((o)->n_pre_scripts)
#define GR_PRE_SCRIPT(o,i) ((o)->pre_scripts[i])
#define GR_PRE_SCRIPT_ACTIVE(o,i) ((o)->pre_script_active[i])
#define GR_PRE_SCRIPTS(o)  ((o)->pre_scripts)
#define GR_PRE_SCRIPT_ACTIVES(o)  ((o)->pre_script_active)
#define GR_N_POST_SCRIPTS(o) ((o)->n_post_scripts)
#define GR_POST_SCRIPT(o,i) ((o)->post_scripts[i])
#define GR_POST_SCRIPT_ACTIVE(o,i) ((o)->post_script_active[i])
#define GR_POST_SCRIPTS(o)  ((o)->post_scripts)
#define GR_POST_SCRIPT_ACTIVES(o)  ((o)->post_script_active)
#define GR_N_POSTFRAME_SCRIPTS(o) ((o)->n_postframe_scripts)
#define GR_POSTFRAME_SCRIPT(o,i) ((o)->postframe_scripts[i])
#define GR_POSTFRAME_SCRIPT_ACTIVE(o,i) ((o)->postframe_script_active[i])
#define GR_POSTFRAME_SCRIPTS(o)  ((o)->postframe_scripts)
#define GR_POSTFRAME_SCRIPT_ACTIVES(o)  ((o)->postframe_script_active)
#define GR_N_THISFRAME_SCRIPTS(o) ((o)->n_thisframe_scripts)
#define GR_THISFRAME_SCRIPT(o,i) ((o)->thisframe_scripts[i])
#define GR_THISFRAME_SCRIPTS(o)  ((o)->thisframe_scripts)
#define GR_PROPERTY_TABLE(o) ((o)->property_table)

/********************************************************************
 * Structure: OBJ_LIST
 * Purpose: Hold list of graphics objects
 * Refers To: GR_OBJ
 * Contained In: None
 ********************************************************************/

typedef struct {
  int visible;			/* are any stimuli visible?         */
  int dynamic;			/* is whole set dynamic updated     */
  int dynamic_stored;		/* stored value for animation       */
  float scale[3];		/* current overall scale            */
  float translate[3];		/* current overall position         */
  float axis[3];		/* axes used for rotate             */
  float spin;			/* amount to spin around axes       */
  float spinrate;               /* spin delta per update            */
  int nobj;			/* number of objects loaded         */
  GR_OBJ **objects;		/* array of object pointers         */
  int maxobj;			/* number objects allocated         */
  void *nameInfo;               /* ObjNameInfo* - opaque b/c Tcl    */
} OBJ_LIST;

#define OL_VISIBLE(m)           ((m)->visible)
#define OL_DYNAMIC(m)           ((m)->dynamic)
#define OL_DYNAMIC_STORED(m)    ((m)->dynamic_stored)
#define OL_SCALE(m)             ((m)->scale)
#define OL_TRANSLATE(m)         ((m)->translate)
#define OL_AXIS(m)              ((m)->axis)
#define OL_SPIN(m)              ((m)->spin)
#define OL_SPINRATE(m)          ((m)->spinrate)
#define OL_NOBJS(m)             ((m)->nobj)
#define OL_OBJS(m)              ((m)->objects)
#define OL_MAXOBJS(m)           ((m)->maxobj)
#define OL_OBJ(m,i)             (OL_OBJS(m)[i])
#define OL_NAMEINFO(m)          ((m)->nameInfo)

#define OL_SX(m)                (OL_SCALE(m)[0])
#define OL_SY(m)                (OL_SCALE(m)[1])
#define OL_SZ(m)                (OL_SCALE(m)[2])
#define OL_TX(m)                (OL_TRANSLATE(m)[0])
#define OL_TY(m)                (OL_TRANSLATE(m)[1])
#define OL_TZ(m)                (OL_TRANSLATE(m)[2])
#define OL_AX1(m)               (OL_AXIS(m)[0])
#define OL_AX2(m)               (OL_AXIS(m)[1])
#define OL_AX3(m)               (OL_AXIS(m)[2])

#ifdef __cplusplus
extern "C" {
#endif

OBJ_LIST *objListCreate(void);
void objListReset(OBJ_LIST *list);
void objListSetSpinRate(OBJ_LIST *list, float rate);
void objListSetSpin(OBJ_LIST *list, float spin);
void objListSetRotAxis(OBJ_LIST *list, float x, float y, float z);
void objListTranslate(OBJ_LIST *list, float x, float y, float z);

void objNameClearRegistry(OBJ_LIST *list);
  
int gobjFindObj(OBJ_LIST *objlist, char *name, int *id);
int gobjAppendNewObj(OBJ_LIST *olist, char *name);
int  gobjUnloadObj(OBJ_LIST *list, int id);
char *gobjTypeName(int type);

void gobjResetObj(GR_OBJ *gobj);
void gobjTranslateObj(GR_OBJ *gobj, float x, float y, float z);
void gobjScaleObj(GR_OBJ *gobj, float x, float y, float z);
void gobjRotateObj(GR_OBJ *obj, float spin, float x, float y, float z);

float *gobjSetMatrix(GR_OBJ *gobj, float *);
int  gobjUseMatrix(GR_OBJ *gobj, int);

void gobjSetEye(GR_OBJ *gobj, int l, int r);

int  gobjSetVisibility(GR_OBJ *obj, int status);
 
int  gobjAddPreScript(GR_OBJ *obj, char *script);
int  gobjAddPostScript(GR_OBJ *obj, char *script);
int  gobjAddPostFrameScript(GR_OBJ *obj, char *script);
int  gobjAddThisFrameScript(GR_OBJ *obj, char *script);
  
int  gobjActivatePreScript(GR_OBJ *obj, int slot);
int  gobjActivatePostScript(GR_OBJ *obj, int slot);
int  gobjActivatePostFrameScript(GR_OBJ *obj, int slot);

int  gobjDeactivatePreScript(GR_OBJ *obj, int slot);
int  gobjDeactivatePostScript(GR_OBJ *obj, int slot);
int  gobjDeactivatePostFrameScript(GR_OBJ *obj, int slot);

int  gobjReplacePreScript(GR_OBJ *obj, int slot, char *script);
int  gobjReplacePostScript(GR_OBJ *obj, int slot, char *script);
int  gobjReplacePostFrameScript(GR_OBJ *obj, int slot, char *script);
  
GR_OBJ *gobjCreateObj(void);
int gobjDestroyObj(GR_OBJ *);
int gobjAddObj(OBJ_LIST *olist, GR_OBJ *obj);
OBJ_LIST *getOBJList(void);
int gobjRegisterType(void);

/* From main.c */
extern OBJ_LIST *OBJList;
void gobjInit(GR_OBJ *obj);
void gobjDelete(GR_OBJ *obj);
void defaultProjection(void);
void drawObj(GR_OBJ *obj);

int stimGetMatrix(int type, float *vals);
int stimPutMatrix(int type, float *vals);  
int stimMultMatrix(int type, float t[3], float r[4], float s[3]);
int stimMultGrObjMatrix(int type, GR_OBJ *g);
  
/* from tclproc.c */
void delete_property_table(GR_OBJ *);

#ifdef __cplusplus
}
#endif

/*
 *    objgroup.h - header file for structure which holds objects
 */

enum { G_NORMAL, G_ONESHOT, G_SINGLE_FRAME, G_TIMESTAMPED, G_NREPEAT_MODES };

#define PARAM_SIZE      128

typedef struct _objframe {
  int nobjs;			/* Number of objs in the group   */
  int maxobjs;			/* Max number there's space for  */
  int *objids;			/* ID's into the global MList    */
  int starttime;		/* time first frame shown        */
  int stoptime;			/* time last frame shown         */
  char *initcmd;		/* Script to be run before dsp'd */
  char *postcmd;		/* Script to be run after dsp'd  */
} OBJ_FRAME;


#define OF_NOBJS(f)         ((f)->nobjs)
#define OF_MAXOBJS(f)       ((f)->maxobjs)
#define OF_OBJIDS(f)        ((f)->objids)
#define OF_OBJID(f,i)       (((f)->objids)[i])

#define OF_START(f)          ((f)->starttime)
#define OF_STOP(f)           ((f)->stoptime)
#define OF_INITCMD(f)        ((f)->initcmd)
#define OF_POSTCMD(f)        ((f)->postcmd)

typedef struct _objgroup {
  char name[128];
  char params[PARAM_SIZE];  	/* String containing params       */
  char *initcmd;		/* Command to be run before dsp'd */
  int dynamic;			/* Is this a dynamic stimgroup?   */
  int maxframes;                /* max frames allocated           */
  int nframes;			/* number of frames in this group */
  int curframe;                 /* current frame                  */
  OBJ_FRAME *frames;		/* pointer to list of frames      */
  int repeat_mode;		/* mode to cycle through frames   */
  int swapmode;   		/* how to handle swap buffers     */
  int eye[2];			/* which eyes are vis if in stereo  */
} OBJ_GROUP;

#define OG_NAME(o)           ((o)->name)
#define OG_PARAMS(o)         ((o)->params)
#define OG_INITCMD(o)        ((o)->initcmd)
#define OG_DYNAMIC(o)        ((o)->dynamic)
#define OG_SWAPMODE(o)       ((o)->swapmode)
#define OG_DOSWAP(o)         ((o)->swapmode == SWAP_NORMAL)
#define OG_FRAMES(o)         ((o)->frames)
#define OG_CURFRAME(o)       ((o)->curframe)
#define OG_FRAME(o,i)        (&(o)->frames[i])
#define OG_NFRAMES(o)        ((o)->nframes)
#define OG_MAXFRAMES(o)      ((o)->maxframes)
#define OG_REPEAT_MODE(o)    ((o)->repeat_mode)
#define OG_START(o)          (OGF_START(o,0))
#define OG_LEFT_EYE(o)       ((o)->eye[0])
#define OG_RIGHT_EYE(o)      ((o)->eye[1])

#define OGF_START(o,j)       (OF_START(OG_FRAME(o,j)))
#define OGF_NOBJS(o,j)       (OF_NOBJS(OG_FRAME(o,j)))  
#define OGF_MAXOBJS(o,j)     (OF_MAXOBJS(OG_FRAME(o,j)))
#define OGF_OBJIDLIST(o,j)   (OF_OBJIDS(OG_FRAME(o,j))) 
#define OGF_OBJID(o,i,j)     (OF_OBJID(OG_FRAME(o,j),i)) 

#define OGF_INITCMD(o,j)     (OF_INITCMD(OG_FRAME(o,j))) 
#define OGF_POSTCMD(o,j)     (OF_POSTCMD(OG_FRAME(o,j))) 

/*
 * These macros all operate on the "current frame"
 */
#define OG_NOBJS(o)         	(OF_NOBJS(OG_FRAME(o,OG_CURFRAME(o))))   
#define OG_MAXOBJS(o)       	(OF_MAXOBJS(OG_FRAME(o,OG_CURFRAME(o)))) 
#define OG_OBJIDLIST(o)     	(OF_OBJIDS(OG_FRAME(o,OG_CURFRAME(o))))  
#define OG_OBJID(o,i)       	(OF_OBJID(OG_FRAME(o,OG_CURFRAME(o)),i)) 

typedef struct _objgrouplist {
  int newly_visible;		/* just became visible           */
  int visible;			/* is the current group visible  */
  int curgroup;			/* current group being displayed */
  int ngroups;			/* number of groups in the list  */
  int maxgroups;		/* maximum there's space for     */
  OBJ_GROUP *groups;		/* pointers to the groups        */
} OBJ_GROUP_LIST;

#define OGL_NEWLY_VISIBLE(o) ((o)->newly_visible)
#define OGL_VISIBLE(o)       ((o)->visible)
#define OGL_CURGROUP(o)      ((o)->curgroup)
#define OGL_NGROUPS(o)       ((o)->ngroups)
#define OGL_MAXGROUPS(o)     ((o)->maxgroups)
#define OGL_GROUPS(o)        ((o)->groups)
#define OGL_GROUP(o,i)       (&((o)->groups)[i])


typedef struct _obsspec {
  int n;
  int **slots;
  int *nchoices;
  int **times;
  int *ntimes;
} OBS_PERIOD_SPEC;

#define OP_N(o)                 ((o)->n)
#define OP_SLOTS(o)             ((o)->slots)
#define OP_SLOT(o,i)            (((o)->slots)[i])
#define OP_SLOT_ELT(o,i,j)      ((((o)->slots)[i])[j])
#define OP_NCHOICES_LIST(o)     ((o)->nchoices)
#define OP_NCHOICES(o,i)        (((o)->nchoices)[i])

#define OP_TIMES(o)             ((o)->times)
#define OP_TIME(o,i)            (((o)->times)[i])
#define OP_TIME_ELT(o,i,j)      ((((o)->times)[i])[j])
#define OP_NTIMES_LIST(o)       ((o)->ntimes)
#define OP_NTIMES(o,i)          (((o)->ntimes)[i])

typedef struct _obs_spec_list {
  int n;
  int maxalloced;
  OBS_PERIOD_SPEC *specs;
} OBS_SPEC_LIST;

#define OSL_N(o)                ((o)->n)
#define OSL_SPECS(o)            ((o)->specs)
#define OSL_SPEC(o,i)           (&((o)->specs)[i])

#ifdef __cplusplus
extern "C" {
#endif

/********************************************************************/
/*                      Group List Functions                        */
/********************************************************************/

extern OBJ_GROUP_LIST *GList;	 /* pointer to the global grouplist */
extern OBJ_GROUP_LIST *OvList;	 /* pointer to the global overlay list */

extern void glistInit(OBJ_GROUP_LIST *, int ngroups);
extern void glistFree(OBJ_GROUP_LIST *);

void glistSetVisible(OBJ_GROUP_LIST *ogl, int status);
int glistSetCurGroup(OBJ_GROUP_LIST *ogl, int slot);

int glistSetRepeatMode(OBJ_GROUP_LIST *ogl, int slot, int mode);
int glistSetSwapMode(OBJ_GROUP_LIST *ogl, int slot, int mode);
int glistOneShotActive(OBJ_GROUP_LIST *ogl, int slot);

int glistSetEye(OBJ_GROUP_LIST *ogl, int slot, int left, int right);

int glistNFrames(OBJ_GROUP_LIST *ogl, int slot);
int glistNextTimeFrame(OBJ_GROUP *g, int time);
int glistNextGroupFrame(OBJ_GROUP_LIST *ogl, int slot);
int glistSetGroupFrame(OBJ_GROUP_LIST *ogl, int slot, int frame);
int glistPostFrameCmd(OBJ_GROUP *g);

extern int glistSetParams(OBJ_GROUP_LIST *ogl, char *paramstr, int slot);
extern int glistSetInitCmd(OBJ_GROUP_LIST *ogl, char *cmdstr, int slot);
extern int glistSetDynamic(OBJ_GROUP_LIST *ogl, int status, int slot);
extern int glistSetFrameInitCmd(OBJ_GROUP_LIST *ogl, char *cmdstr, 
				int slot, int frame);
extern int glistSetPostFrameCmd(OBJ_GROUP_LIST *ogl, char *cmdstr, 
			 int slot, int frame);
extern int glistSetFrameTime(OBJ_GROUP_LIST *ogl, int slot, int frame, 
			     int time);
extern int glistAddObject(OBJ_GROUP_LIST *, char *mobjname, 
			  int slot, int frame);

/********************************************************************/
/*                  Obs Period Spec List Functions                  */
/********************************************************************/

extern OBS_SPEC_LIST ObsSpecList;  /* global structure to specs     */
extern OBS_SPEC_LIST *OList;	   /* pointer to ObsSpecList        */

extern void olistInit(OBS_SPEC_LIST *, int ngroups);
extern void olistReset(OBS_SPEC_LIST *);
extern void olistFree(OBS_SPEC_LIST *);

extern int olistAddSpec(OBS_SPEC_LIST *, OBS_PERIOD_SPEC *);
extern OBS_PERIOD_SPEC *olistCreateSpec(OBS_SPEC_LIST *, int slot, int n);
extern int olistFillSpecSlot(OBS_PERIOD_SPEC *, int slot, int n, int *choices);
extern int olistFillSpecTime(OBS_PERIOD_SPEC *, int slot, int n, int *times);

#ifdef __cplusplus
}
#endif

#endif /* STIM2_H */
