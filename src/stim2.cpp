#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <ctime>
#include <queue>
#include <algorithm>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/wait.h>
#endif
#include <cerrno>
#include <cstring>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <filesystem>
#include "SleepWakeHandler.h"
#endif

#ifdef _WIN32
#include <Ws2tcpip.h>
#include <Winsock2.h>
#include <Ws2ipdef.h>
#pragma comment(lib, "Ws2_32.lib")
#include <direct.h>
#include <io.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#endif

#include "SharedQueue.h"
#include "framelog.h"
#include "latedraw.h"
#include "WebSocketServer.h"
#include "TclCompletion.h"

#include "timer.h"      // for periodic timer thread

#include <cxxopts.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "diagnostics.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h> 

#include <glm/glm.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <signal.h>
#include <tcl.h>

#include "stim2.h"
#include "socket_keepalive.h"
#include "rawapi.h"
#include "objname.h"
#include "animate.h"
#include "swapsync.h"

static int MainWin = 0;

static void do_wakeup(void);

static int MessageLoop(void);
static void drawGroup(OBJ_GROUP *g);
static void execTimerFuncs(OBJ_GROUP *g);

static void free_resources(void);

void log_message(const char* level, const char* fmt, ...);

OBJ_LIST *OBJList = NULL;  /* main entry point to object stimuli  */
OBJ_LIST curobjlist;
OBJ_LIST *curobjlistp = &curobjlist;

// for z-sorting comparison
static OBJ_LIST *sort_objlist_context;

float BackgroundColor[]      = { 0.0f,  0.0f, 0.0f, 1.0f };
float GreenBackgroundColor[] = { 0.0f,  .8f, .2f };
float RedBackgroundColor[]   = { .85f, .0f, .1f };
static float PIX_PER_DEG_X, PIX_PER_DEG_Y, 
  HALF_SCREEN_DEG_X, HALF_SCREEN_DEG_Y, HALF_SCREEN_DEG_Z = 1000.;
static int ScreenWidth;
static int ScreenWidth_2;
static int ScreenHeight;
static float ScreenWidthCm;
static float ScreenHeightCm;
static float DistanceToMonitor = 40.0;

static int RefreshRate;
static float FrameDuration;
int     winWidth = 580;
static int      winWidth_2;
int     winHeight = 340;
static int      winX = 10;
static int      winY = 10;
static int      currentEye = 0; /* flag denoting which eye's being drawn */
static int      RunningWindowed = 0;
static int      animEventPending = 0;
float  XScale, YScale;

static int dummyInt;

PARAM_ENTRY ScreenParamTable[] = {
  (char *) "PixPerDegreeX",       &PIX_PER_DEG_X,         &dummyInt, PU_FLOAT,
  (char *) "PixPerDegreeY",       &PIX_PER_DEG_Y,         &dummyInt, PU_FLOAT,
  (char *) "HalfScreenDegreeX",   &HALF_SCREEN_DEG_X,     &dummyInt, PU_FLOAT,
  (char *) "HalfScreenDegreeY",   &HALF_SCREEN_DEG_Y,     &dummyInt, PU_FLOAT,
  (char *) "ScreenWidth",         &ScreenWidth,           &dummyInt, PU_INT,
  (char *) "ScreenHeight",        &ScreenHeight,          &dummyInt, PU_INT,
  (char *) "ScreenWidthCm",       &ScreenWidthCm,         &dummyInt, PU_FLOAT,
  (char *) "ScreenHeightCm",      &ScreenHeightCm,        &dummyInt, PU_FLOAT,
  (char *) "DistanceToMonitor",   &DistanceToMonitor,     &dummyInt, PU_FLOAT,
  (char *) "ScaleX",              &XScale,                &dummyInt, PU_FLOAT,
  (char *) "ScaleY",              &YScale,                &dummyInt, PU_FLOAT,
  (char *) "WinWidth",            &winWidth,              &dummyInt, PU_INT,
  (char *) "WinHeight",           &winHeight,             &dummyInt, PU_INT,
  (char *) "RefreshRate",         &RefreshRate,           &dummyInt, PU_INT,
  (char *) "FrameDuration",       &FrameDuration,         &dummyInt, PU_FLOAT,
  (char *) "", NULL, NULL, PU_NULL
};

int StereoMode = 0;     /* available from tcl */
int ChangeMode = 1;     /* use directx to change mode */
int BlockMode = 0;      /* should be 0, 1, 2, or 3 */
int UseHardware = 1;        /* use hardware DIO/Timer if present */
int MouseXPos = 0;      /* set on mousemove */
int MouseYPos = 0;      /* set on mousemove */

unsigned int StimVersion = 20;  /* Version 2.0 */
unsigned int StimTime = 0;  /* available from tcl */
unsigned int StimTicks = 0; /* available from tcl */
double StimStart = 0;           /* absolute time of last reset */
unsigned int StimVRetraceCount = 0;
unsigned int StimDeltaTime = 0;
double StimTimeF = 0.0;        /* float ms since reset (no int truncation) */
double StimTicksF = 0.0;       /* float free-running ms */
double StimDeltaTimeF = 0.0;   /* float ms since last frame */

int NextFrameTime = -1;

/* Headless rendering and the stepped clock (docs/headless.md).
   Headless (--headless): the display is an offscreen framebuffer of exactly
   -w x -h pixels, nothing is swapped or flipped, and frames are paced by a
   sleep to the next refresh period instead of vsync.  On Linux the context
   comes from GLFW's null platform over Mesa's surfaceless EGL, so no display
   server is needed; elsewhere from a hidden native window.
   Step mode (stepMode): the stim clock stops following glfwGetTime() and
   moves only when stepClock/stepFrame set it, the timer thread is stopped,
   and the main loop never draws on its own -- each stepFrame is one frame.
   Everything that reads stim time (StimTime/StimTicks and their F forms,
   getStimTime*, resetStimTime, the stim/frame ticks) goes through
   stimClockNow(); swapsync and late-draw keep the real clock. */
static int Headless = 0;
static std::atomic<int> StepMode{0};
static double StepClockSec = 0.0;  /* the stepped clock, glfwGetTime() domain */

static inline double stimClockNow(void)
{
  return StepMode ? StepClockSec : glfwGetTime();
}

/* the buffer holding the frame being drawn, and the one last shown */
static inline GLenum drawnBuffer(void) { return Headless ? GL_COLOR_ATTACHMENT0 : GL_BACK; }
static inline GLenum shownBuffer(void) { return Headless ? GL_COLOR_ATTACHMENT0 : GL_FRONT; }

int SwapPulse = 1;      /* Put out reg. pulse on line 1 at swap? */
int SwapAcknowledge = 0;    /* Put out onetime pulse on line at swap */
int SwapCount = 0;

int ClearBackground = 1;    /* Clear the background every frame?  */

DUMP_INFO DumpInfo;     /* specify filename/coords for frame dump */

glm::mat4 StimModelViewMatrix;
glm::mat3 StimNormalMatrix;
glm::mat4 StimMVPMatrix;
glm::mat4 StimProjMatrix;

Tcl_Interp *OurInterp = NULL;

/* Tcl command: maps the live cursor position to world coordinates.
 * Defined after the Application instance; forward-declared here so it
 * can be registered in Tcl_StimAppInit. */
static int getMouseWorldCmd(ClientData clientData, Tcl_Interp *interp,
                            int argc, char *argv[]);


const std::string WHITESPACE = " \n\r\t\f\v";
 
std::string ltrim(const std::string &s)
{
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}
 
std::string rtrim(const std::string &s)
{
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}
 
std::string trim(const std::string &s) {
    return rtrim(ltrim(s));
}

#ifdef JETSON_NANO
#include <JetsonGPIO.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
void addTclCommands(Tcl_Interp *interp);
void tclAddParamTable(Tcl_Interp *interp, PARAM_ENTRY *table, char *name);
#ifdef __cplusplus
}
#endif

void setModelViewMatrix(GR_OBJ *o)
{
  glm::mat4 M = glm::mat4(1.0f);

  if (!GR_USEMATRIX(o)) {
    M = glm::rotate(M, glm::radians(OL_SPIN(curobjlistp)), 
          glm::vec3(OL_AX1(curobjlistp),
                OL_AX2(curobjlistp),
                OL_AX3(curobjlistp)));
    M = glm::translate(M, glm::vec3(OL_TX(curobjlistp)+GR_TX(o), 
                    OL_TY(curobjlistp)+GR_TY(o), 
                    OL_TZ(curobjlistp)+GR_TZ(o)));
    M = glm::rotate(M, glm::radians(GR_SPIN(o)),
            glm::vec3(GR_AX1(o), GR_AX2(o), GR_AX3(o)));
    M = glm::scale(M, glm::vec3(OL_SX(curobjlistp)*GR_SX(o), 
                OL_SY(curobjlistp)*GR_SY(o), 
                OL_SZ(curobjlistp)*GR_SZ(o)));
  }
  else {
    M = glm::make_mat4(GR_MATRIX(o));
    M = glm::scale(M, glm::vec3(GR_SX(o), GR_SY(o), GR_SZ(o)));
  }
  StimModelViewMatrix = M;
  StimNormalMatrix = glm::transpose(glm::inverse(glm::mat3(StimModelViewMatrix)));
  //  StimNormalMatrix = glm::transpose(glm::mat3(StimModelViewMatrix));
}

int stimPutMatrix(int type, float *vals)
{
  int i;
  switch (type) {
  case STIM_MODELVIEW_MATRIX:
    {
      for (i = 0; i < 16; i++)
    glm::value_ptr(StimModelViewMatrix)[i] = vals[i];
    }
    break;
  case STIM_PROJECTION_MATRIX:
    {
      for (i = 0; i < 16; i++)
    glm::value_ptr(StimProjMatrix)[i] = vals[i];
    }
    break;
  case STIM_NORMAL_MATRIX:
    {
      for (i = 0; i < 9; i++)
    glm::value_ptr(StimNormalMatrix)[i] = vals[i];
    }
    break;
  default:
    return 0;
   }
  return 1;
}

int stimGetMatrix(int type, float *vals)
{
  int i;
  switch (type) {
  case STIM_PROJECTION_MATRIX:
    {
      for (i = 0; i < 16; i++) vals[i] = glm::value_ptr(StimProjMatrix)[i];
    }
    break;
  case STIM_MODELVIEW_MATRIX:
    {
      for (i = 0; i < 16; i++) vals[i] = glm::value_ptr(StimModelViewMatrix)[i];
    }
    break;
  case STIM_NORMAL_MATRIX:
    {
      for (i = 0; i < 9; i++) vals[i] = glm::value_ptr(StimNormalMatrix)[i];
    }
    break;
  default:
    return 0;
   }
  return 1;
}

int stimMultMatrix(int type, float t[3], float r[4], float s[3])
{
  switch (type) {
  case STIM_MODELVIEW_MATRIX:
    {
      StimModelViewMatrix = glm::translate(StimModelViewMatrix,
                     glm::vec3(t[0], t[1], t[2]));
      StimModelViewMatrix = glm::rotate(StimModelViewMatrix,
                  glm::radians(r[0]),
                  glm::vec3(r[1], r[2], r[3]));
      StimModelViewMatrix = glm::scale(StimModelViewMatrix,
                 glm::vec3(s[0], s[1], s[2]));
      }
    break;
  case STIM_PROJECTION_MATRIX:
    {
      StimProjMatrix = glm::translate(StimProjMatrix, glm::vec3(t[0], t[1], t[2]));
      StimProjMatrix = glm::rotate(StimProjMatrix, glm::radians(r[0]),
                 glm::vec3(r[1], r[2], r[3]));
      StimProjMatrix = glm::scale(StimProjMatrix, glm::vec3(s[0], s[1], s[2]));
    }
    break;
  default:
    return 0;
   }
  return 1;
}

int stimMultGrObjMatrix(int type, GR_OBJ *g)
{
  switch (type) {
  case STIM_MODELVIEW_MATRIX:
    {
      StimModelViewMatrix = glm::translate(StimModelViewMatrix,
                     glm::vec3(GR_TX(g), GR_TY(g), GR_TZ(g)));
      StimModelViewMatrix = glm::rotate(StimModelViewMatrix,
                  glm::radians(GR_SPIN(g)),
                  glm::vec3(GR_AX1(g), GR_AX2(g), GR_AX3(g)));
      StimModelViewMatrix = glm::scale(StimModelViewMatrix,
                 glm::vec3(GR_SX(g), GR_SY(g), GR_SZ(g)));
    }
    break;
  default:
    return 0;
   }
  return 1;
}

static int dumpWindowAsRaw(DUMP_INFO *dinfo)
{
  FILE *fp;
  unsigned char *pixels, *row;
  int x, y, w, h;
  unsigned int npixels, rowbytes;
  int i;
  
  x = dinfo->x;
  y = dinfo->y;
  w = dinfo->w;
  h = dinfo->h;
  if (!w) w = winWidth;
  if (!h) h = winHeight;
  npixels = w*h;
  pixels = (unsigned char *) calloc(npixels*4, 1);
  if (!pixels) return 0;

  fp = fopen(dinfo->filename, "wb");
  if (!fp) {
    free(pixels);
    return 0;
  }
  
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(shownBuffer());
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glReadBuffer(drawnBuffer());
  
  raw_writeHeader(w, h, 4, fp);

  rowbytes = 4*w;
  for (i = 0; i < h; i++) {
    row = &pixels[(h-i-1)*rowbytes];
    if (fwrite(row, 4, (unsigned) w, fp) != (unsigned) w) {
      fclose(fp);
      free(pixels);
      return 0;
    }
  }
  fclose(fp);
  free(pixels);
  return 1;
}

static int dumpWindowAsPS(char *filename)
{
  FILE *fp;
  unsigned char *pixels;
  unsigned int npixels = winWidth*winHeight;

  pixels = (unsigned char *) calloc(npixels*4, 1);
  if (!pixels) return 0;

  fp = fopen(filename, "wb");
  if (!fp) {
    free(pixels);
    return 0;
  }

  glReadBuffer(shownBuffer());
  glReadPixels(0, 0, winWidth, winHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glReadBuffer(drawnBuffer());

  raw_bufToPS(pixels, winWidth, winHeight, 4, fp, RAW_FLAGS_FLIP);

  fclose(fp);
  free(pixels);
  return 1;
}

FILE *getConsoleFP(void) { return stderr; }

int setWakeUp(int ms) {
  NextFrameTime = StimTime+ms;
  return NextFrameTime;
}

void resetStimTime(void)
{
  StimStart = stimClockNow();
  StimTime = 0;
  StimTimeF = 0.0;
}

void updateTimes(void)
{
  double curtime = stimClockNow();
  unsigned int prevTicks = StimTicks;
  double prevTicksF = StimTicksF;
  StimTicks = (int) (1000*curtime);
  StimTicksF = 1000.0*curtime;
  StimDeltaTime = StimTicks - prevTicks;
  StimDeltaTimeF = StimTicksF - prevTicksF;
  StimTime = (int) (1000*(curtime-StimStart));
  StimTimeF = 1000.0*(curtime-StimStart);
}


/*
 * Drain one object's frame-script queue for the given phase
 * (STIM_POSTFRAME_SCRIPT or STIM_THISFRAME_SCRIPT), then recurse into
 * any container members via the object's framescriptfunc hook.
 *
 * postframe queue is persistent (re-run every frame until removed);
 * thisframe queue is one-shot (drained, executed, freed, and the count
 * zeroed so it can be re-armed). Container objects (e.g. metagroup) set
 * GR_FRAMESCRIPTFUNCP so members' queues are drained too -- without that
 * hook the central per-group loop only sees top-level objects, and a
 * frame script queued onto a metagroup *member* would never fire.
 */
void executeObjFrameScripts(GR_OBJ *o, int phase)
{
  if (!o) return;

  switch (phase) {
  case STIM_PRE_SCRIPT:
    /* Per-frame pre-draw work: advance the object's animation, then run
       its pre-scripts. Driven from a dedicated traversal (executePreScripts)
       rather than the draw pass, so it runs for every object regardless of
       visibility or metagroup nesting -- the same guarantee the postframe/
       thisframe queues already had. animateUpdateObj stays paired with the
       pre-scripts here to preserve the historical "animate then pre" order. */
    animateUpdateObj(o, StimTicksF, StimDeltaTimeF);
    executeScripts(GR_PRE_SCRIPTS(o),
                   GR_PRE_SCRIPT_ACTIVES(o),
                   GR_N_PRE_SCRIPTS(o));
    break;
  case STIM_POST_SCRIPT:
    executeScripts(GR_POST_SCRIPTS(o),
                   GR_POST_SCRIPT_ACTIVES(o),
                   GR_N_POST_SCRIPTS(o));
    break;
  case STIM_POSTFRAME_SCRIPT:
    executeScripts(GR_POSTFRAME_SCRIPTS(o),
                   GR_POSTFRAME_SCRIPT_ACTIVES(o),
                   GR_N_POSTFRAME_SCRIPTS(o));
    break;
  case STIM_THISFRAME_SCRIPT: { /* one-shot: drain, execute, free */
    int j, n;
    char *thisframe_scripts[MAXSCRIPTS];
    for (j = 0, n = 0; j < GR_N_THISFRAME_SCRIPTS(o); j++) {
      thisframe_scripts[n++] = GR_THISFRAME_SCRIPT(o, j);
    }
    GR_N_THISFRAME_SCRIPTS(o) = 0;
    for (j = 0; j < n; j++) {
      sendTclCommand(thisframe_scripts[j]);
      free(thisframe_scripts[j]);
    }
    break;
  }
  }

  /* Recurse into container members (metagroup, etc.) */
  if (GR_FRAMESCRIPTFUNCP(o)) GR_FRAMESCRIPTFUNC(o)(o, phase);
}

void executePostFrameScripts(OBJ_GROUP *g)
{
  GR_OBJ *o;
  int i;

  for (i = 0; i < OG_NOBJS(g); i++)  {
    o = OL_OBJ(OBJList, OG_OBJID(g, i));
    if (o) executeObjFrameScripts(o, STIM_POSTFRAME_SCRIPT);
  }
}

/*
 * These are oneshot scripts
 * Ensure that they can be re-armed
 */
void executeThisFrameScripts(OBJ_GROUP *g)
{
  GR_OBJ *o;
  int i;

  for (i = 0; i < OG_NOBJS(g); i++)  {
    o = OL_OBJ(OBJList, OG_OBJID(g, i));
    if (o) executeObjFrameScripts(o, STIM_THISFRAME_SCRIPT);
  }
}

void executeScripts(char **scripts, int *actives, int n)
{
  int i;
  for (i = 0; i < n; i++) {
    if (actives[i] && scripts[i]) sendTclCommand(scripts[i]);
  }
}

/*
 * Pre-draw and post-draw script passes. Mirror executePostFrameScripts:
 * iterate the group's top-level objects and recurse into container members
 * via the framescript hook, independent of visibility. This decouples
 * pre/post scripts (and per-object animation advance) from the draw
 * traversal, so a per-frame driver attached to a metagroup MEMBER keeps
 * running even when the parent metagroup is hidden -- matching the behavior
 * hidden top-level objects already had.
 */
void executePreScripts(OBJ_GROUP *g)
{
  GR_OBJ *o;
  int i;
  for (i = 0; i < OG_NOBJS(g); i++)  {
    o = OL_OBJ(OBJList, OG_OBJID(g, i));
    if (o) executeObjFrameScripts(o, STIM_PRE_SCRIPT);
  }
}

void executePostScripts(OBJ_GROUP *g)
{
  GR_OBJ *o;
  int i;
  for (i = 0; i < OG_NOBJS(g); i++)  {
    o = OL_OBJ(OBJList, OG_OBJID(g, i));
    if (o) executeObjFrameScripts(o, STIM_POST_SCRIPT);
  }
}

/* Pure draw: transform + render. Animation advance and pre/post scripts
   are handled by executePreScripts/executePostScripts, not here, so this
   only runs for VISIBLE objects (callers gate on GR_VISIBLE). */
void drawObject(GR_OBJ *o)
{
  setModelViewMatrix(o);
  drawObj(o);
}

/********************************************************************
 * Function:     getSwapCount
 * Returns:      int swaps
 * Arguments:    none
 * Description:  Returns stream to write output to
 ********************************************************************/

int getSwapCount(void)
{
  return SwapCount;
}


/********************************************************************
 * Function:     getParamTable
 * Returns:      PARAM_ENTRY *
 * Arguments:    none
 * Description:  Returns pointer to screen param table
 ********************************************************************/

int *getParamTable(void)
{
  return (int *) ScreenParamTable;
}

/********************************************************************
 * Function:     getCurrentEye
 * Returns:      int
 * Arguments:    none
 * Description:  Returns 0 if left eye (or mono) is current, 1 for rt
 ********************************************************************/

int getCurrentEye(void)
{
  return currentEye;
}

/********************************************************************
 * Function:     getStereoMode
 * Returns:      int
 * Arguments:    none
 * Description:  Returns current stereo mode
 ********************************************************************/

int getStereoMode(void)
{
  return StereoMode;
}

/********************************************************************
 * Function:     setStereoMode
 * Returns:      int
 * Arguments:    mode
 * Description:  old mode
 ********************************************************************/

int setStereoMode(int mode)
{
  int old = StereoMode;
  StereoMode = mode;
  return old;
}

/********************************************************************
 * Function:     getStimTime
 * Returns:      int
 * Arguments:    none
 * Description:  Time (in ms) since stimulus became visible
 ********************************************************************/

int getStimTime(void)
{
  return StimTime;
}

/********************************************************************
 * Function:     getStimTicks
 * Returns:      int
 * Arguments:    none
 * Description:  Time (in ms) since stimulus became visible
 ********************************************************************/

int getStimTicks(void)
{
  return StimTicks;
}

/********************************************************************
 * Functions:    getStimTimeF / getStimTicksF
 * Returns:      double (ms)
 * Description:  Float-precision parallels of getStimTime/getStimTicks.
 *               No integer-ms truncation -- use for smooth per-frame
 *               motion (constant-velocity integration) so steps aren't
 *               quantized (e.g. 8/9 ms at 120 Hz). getStimTime stays int
 *               for event timestamps / printf %d callers.
 ********************************************************************/

double getStimTimeF(void)
{
  return StimTimeF;
}

double getStimTicksF(void)
{
  return StimTicksF;
}

/********************************************************************
 * Function:     stimClockSample
 * Returns:      1 on success, 0 if the realtime clock is unavailable
 * Description:  Sample the tick clock (1000.0*glfwGetTime(), the clock
 *               behind StimTicksF and the ACK1 header stamps) and
 *               CLOCK_REALTIME together, so a client can place tick
 *               stamps on the wall timeline.  Order: realtime, glfw,
 *               realtime; *wall_us is the midpoint of the two realtime
 *               reads and *span_us their difference (the error bound
 *               of the pairing).  Same realtime source as
 *               swapsync.cpp's last_flip_wall_us (timespec_get).
 *               Backs the stimClock Tcl command (tclproc.c).
 ********************************************************************/

int stimClockSample(double *ticks_ms, long long *wall_us, long long *span_us)
{
  struct timespec t0, t1;
  if (timespec_get(&t0, TIME_UTC) != TIME_UTC) return 0;
  double ticks = 1000.0 * stimClockNow();
  if (timespec_get(&t1, TIME_UTC) != TIME_UTC) return 0;
  long long w0 = (long long) t0.tv_sec * 1000000LL + t0.tv_nsec / 1000;
  long long w1 = (long long) t1.tv_sec * 1000000LL + t1.tv_nsec / 1000;
  if (ticks_ms) *ticks_ms = ticks;
  if (wall_us) *wall_us = w0 + (w1 - w0) / 2;
  if (span_us) *span_us = w1 - w0;
  return 1;
}

/********************************************************************
 * Function:     getFrameDuration
 * Returns:      double
 * Arguments:    none
 * Description:  Time (in ms) 
 ********************************************************************/

double getFrameDuration(void)
{
 return FrameDuration;
}

/********************************************************************
 * Function:     getScreenInfo
 * Returns:      int
 * Arguments:    int *screenwidth
 *               int *screenheight
 *               int *winwidth
 *               int *winheight
 *               float *framerate
 * Description:  Return screen info
 ********************************************************************/

int getScreenInfo(int *sw, int *sh, int *ww, int *wh, float *freq)
{
  if (sw) *sw = ScreenWidth;
  if (sh) *sh = ScreenHeight;
  if (ww) *ww = winWidth;
  if (wh) *wh = winHeight;
  if (freq) *freq = RefreshRate;
  return 1;
}

/********************************************************************
 * Function:     drawObj
 * Returns:      none
 * Arguments:    GR_OBJ *obj
 * Description:  Draw graphics object using draw callback
 ********************************************************************/

void drawObj(GR_OBJ *obj)
{
  if (GR_ACTIONFUNCP(obj)) {
    GR_ACTIONFUNC(obj)(obj);
    return;
  }
}


void defaultProjection(void)
{
  StimProjMatrix = glm::ortho(-HALF_SCREEN_DEG_X, HALF_SCREEN_DEG_X,
            -HALF_SCREEN_DEG_Y, HALF_SCREEN_DEG_Y,
            -HALF_SCREEN_DEG_Z, HALF_SCREEN_DEG_Z);
}


static int compareGroupPriority(const void *a, const void *b)
{
  int idA = *(const int *)a;
  int idB = *(const int *)b;
  GR_OBJ *objA = OL_OBJ(sort_objlist_context, idA);
  GR_OBJ *objB = OL_OBJ(sort_objlist_context, idB);
  float diff;

  /* Handle NULL objects - push to end */
  if (!objA && !objB) return 0;
  if (!objA) return 1;
  if (!objB) return -1;

  diff = GR_PRIORITY(objA) - GR_PRIORITY(objB);
  if (diff != 0.0f) return (diff > 0.0f) - (diff < 0.0f);

  /* Stable sort: preserve original order for equal priorities */
  return (a > b) - (a < b);
}

static void drawGroup(OBJ_GROUP *g) 
{
  GR_OBJ *o;
  int i;

  /*
   * Stash the current ObjList away so it doesn't get changed
   * during this update 
   */
  memcpy(curobjlistp, OBJList, sizeof(OBJ_LIST));

  /* Sort objects by priority
     (stable - preserves insertion order for equal priorities) */
  if (g && OG_NOBJS(g) > 1) {
    sort_objlist_context = OBJList;
    qsort(OG_OBJIDLIST(g), OG_NOBJS(g), sizeof(int), compareGroupPriority);
  }

  /* Kick the animation if we just turned on a dynamic stimulus */
  if (OGL_NEWLY_VISIBLE(GList) && g && 
      !OL_DYNAMIC(OBJList) && (OG_DYNAMIC(g) == DYNAMIC_FRAME_BASED))
    startAnimation();
  
  /* Turn off animation if the current group is not animated */
  /* and global animation is not on (OL_DYNAMIC_STORED)      */
  else if (g && 
       !OL_DYNAMIC_STORED(OBJList) && 
       OL_DYNAMIC(OBJList) &&
       (OG_DYNAMIC(g) != DYNAMIC_FRAME_BASED))
    stopAnimation();
  
  /* Turn off animation if stimuli are not visible */
  else if (OL_DYNAMIC(OBJList) && !OGL_VISIBLE(GList))
    stopAnimation();

  /* Draw pass: VISIBLE objects only. Per-object animation advance and
     pre/post scripts run in executePreScripts/executePostScripts (which
     traverse regardless of visibility), so hidden objects -- top-level or
     metagroup members -- are simply not drawn here. */
  if (g && OGL_VISIBLE(GList)) {
    switch (StereoMode) {
    case 0:
      currentEye = 0;
      for (i = 0; i < OG_NOBJS(g); i++)  {
    o = OL_OBJ(OBJList, OG_OBJID(g, i));
    if (o && GR_VISIBLE(o)) drawObject(o);
      }
      break;
    case 1:
    case 2:
      /* Draw left eye */
      if (OG_LEFT_EYE(g)) {
    currentEye = 0;
    for (i = 0; i < OG_NOBJS(g); i++)  {
      o = OL_OBJ(OBJList, OG_OBJID(g, i));
      if (o && GR_VISIBLE(o) && GR_LEFT_EYE(o)) drawObject(o);
    }
      }
      if (StereoMode == 2) break;
    case 3:
      /* Draw right eye */
      if (OG_RIGHT_EYE(g)) {
    currentEye = 1;
    for (i = 0; i < OG_NOBJS(g); i++)  {
      o = OL_OBJ(OBJList, OG_OBJID(g, i));
      if (o && GR_VISIBLE(o) && GR_RIGHT_EYE(o)) drawObject(o);
    }
      }
      break;
    case 4:         /* Hardware Stereo */
      if (OG_LEFT_EYE(g)) {
    glDrawBuffer(GL_BACK_LEFT);
    currentEye = 0;
    for (i = 0; i < OG_NOBJS(g); i++)  {
      o = OL_OBJ(OBJList, OG_OBJID(g, i));
      if (o && GR_VISIBLE(o) && GR_LEFT_EYE(o)) drawObject(o);
    }
      }
      if (OG_RIGHT_EYE(g)) {
    glDrawBuffer(GL_BACK_RIGHT);
    currentEye = 1;
    for (i = 0; i < OG_NOBJS(g); i++)  {
      o = OL_OBJ(OBJList, OG_OBJID(g, i));
      if (o && GR_VISIBLE(o) && GR_RIGHT_EYE(o)) drawObject(o);
    }
      }
      break;
    }
  }
}


static void execTimerFuncs(OBJ_GROUP *g) 
{
  GR_OBJ *o;
  int i;

  /*
   * Stash the current ObjList away it doesn't get changed
   * during this update 
   */
  memcpy(curobjlistp, OBJList, sizeof(OBJ_LIST));

  if (g && OGL_VISIBLE(GList)) {
    switch (StereoMode) {
    case 0:
      currentEye = 0;
      for (i = 0; i < OG_NOBJS(g); i++) {
    o = OL_OBJ(OBJList, OG_OBJID(g, i));
    if (o && GR_VISIBLE(o) && GR_TIMERFUNCP(o)) GR_TIMERFUNC(o)(o);
      }
      break;
    case 1:
    case 2:
      /* Draw left eye */
      if (OG_LEFT_EYE(g)) {
    currentEye = 0;
    for (i = 0; i < OG_NOBJS(g); i++)  {
      o = OL_OBJ(OBJList, OG_OBJID(g, i));
      if (o && GR_VISIBLE(o) && GR_LEFT_EYE(o) && GR_TIMERFUNCP(o)) 
        GR_TIMERFUNC(o)(o);
    }
      }
      if (StereoMode == 2) break;
    case 3:
      /* Draw right eye */
      if (OG_RIGHT_EYE(g)) {
    currentEye = 1;
    for (i = 0; i < OG_NOBJS(g); i++)  {
      o = OL_OBJ(OBJList, OG_OBJID(g, i));
      if (o && GR_VISIBLE(o) && GR_RIGHT_EYE(o) && GR_TIMERFUNCP(o)) 
        GR_TIMERFUNC(o)(o);
    }
      }
      break;
    case 4:         /* Hardware Stereo */
      if (OG_LEFT_EYE(g)) {
    currentEye = 0;
    for (i = 0; i < OG_NOBJS(g); i++)  {
      o = OL_OBJ(OBJList, OG_OBJID(g, i));
      if (o && GR_VISIBLE(o) && GR_LEFT_EYE(o) && GR_TIMERFUNCP(o)) 
        GR_TIMERFUNC(o)(o);
    }
      }
      if (OG_RIGHT_EYE(g)) {
    currentEye = 1;
    for (i = 0; i < OG_NOBJS(g); i++)  {
      o = OL_OBJ(OBJList, OG_OBJID(g, i));
      if (o && GR_VISIBLE(o) && GR_RIGHT_EYE(o) && GR_TIMERFUNCP(o)) 
        GR_TIMERFUNC(o)(o);
    }
      }
      break;
    }
  }
}

void gobjInit(GR_OBJ *obj)
{
  if (GR_INITFUNCP(obj)) GR_INITFUNC(obj)(obj);
  glFinish();
}

void gobjDelete(GR_OBJ *obj)
{
  if (GR_DELETEFUNCP(obj)) GR_DELETEFUNC(obj)(obj);
  glFinish();
}

float *getBackgroundColor(void)
{
  return BackgroundColor;
}

int setAutoClear(int mode)
{
  int old = ClearBackground;
  ClearBackground = mode;
  return old;
}

int kickAnimation(void)
{
  int retval = animEventPending;
  if (!animEventPending) {
    do_wakeup();
    animEventPending = 1;
  }
  return retval;
}

int startAnimation(void)
{
  int old = OL_DYNAMIC(OBJList);
  OL_DYNAMIC(OBJList) = 1;
  do_wakeup();
  return(old);
}

int stopAnimation(void)
{
  int old = OL_DYNAMIC(OBJList);
  OL_DYNAMIC(OBJList) = OL_DYNAMIC_STORED(OBJList) = 0;
  return(old);
}

int setDynamicUpdate(int status)
{
  int old = OL_DYNAMIC(OBJList);
  OL_DYNAMIC(OBJList) = status;
  return(old);
}

int toggleAnimation(void)
{
  if (OL_DYNAMIC(OBJList)) stopAnimation();
  else {
    startAnimation();
    OL_DYNAMIC_STORED(OBJList) = 1;
  }
  return(!OL_DYNAMIC(OBJList));
}

void objNameClearRegistry(OBJ_LIST *olist)
{
    if (OL_NAMEINFO(olist)) {
      objNameClear((ObjNameInfo *) OL_NAMEINFO(olist));
    }
}
/******************************************************************
 *                   Background Color Functions
 ******************************************************************/

void setBackgroundColorVals(float r, float g, float b, float a)
{
  BackgroundColor[0] = r;
  BackgroundColor[1] = g; 
  BackgroundColor[2] = b;
  BackgroundColor[3] = a;
}

void setBackgroundColor(void)
{
  glClearColor (BackgroundColor[0], BackgroundColor[1], 
        BackgroundColor[2], BackgroundColor[3]);
}

void redraw(void)
{
  sendDispMsg(UPDATE_DISPLAY);
}

void reshape(void)
{
  sendDispMsg(RESHAPE_DISPLAY);
}


using namespace std::placeholders;

SharedQueue<int> MessageQueue;

/*
 * ShutdownCmds
 *   provide callback mechanism for modules to close, return resources
 */

class ShutdownCmds
{
  std::vector<SHUTDOWN_FUNC>funcs;
  std::vector<void *>args;

public:
  ShutdownCmds()
  {
  }

  
  ~ShutdownCmds()
  {
    for (auto i = 0; i < funcs.size(); i++) {
      (*funcs[i])(args[i]);
    }
  }
  
  void add(SHUTDOWN_FUNC sfunc, void *arg)
  {
    funcs.push_back(sfunc);
    args.push_back(arg);
  }
};

typedef struct client_request_s {
  std::string script;
  SharedQueue<std::string> *rqueue;
  bool wait_for_swap;       // do we want swap ack?
  bool ack_header;          // '@' prefix: prepend an ACK1 header to the reply
  int swap_at_queue;        // SwapCount when the reply was deferred
  double ticks_dequeue;     // glfwGetTime()*1000 when the render thread
                            // dequeued the request (header stamp)
} client_request_t;

/* Reply header requested by a '@' command.  Fixed wire format -- the ESS
   side parses exactly this:
       ACK1 <ticks_dequeue> <ticks_done> <swapcount> <flipwall_us>\n<result>
   ticks are StimTicksF-domain ms (%.3f), swapcount the unsigned SwapCount,
   flipwall_us the CLOCK_REALTIME flip time of a swap-ack reply, exactly as
   swapsyncLastFlipWallUs() reports it (-1 when the strategy cannot time
   flips, e.g. glfinish), and 0 for an immediate (non-swap) reply.
   Longest line: 4+1+ 2x(20 digits + '.' + 3) + 1+10 + 1+20 + '\n' < 96.  */
#define ACK_HEADER_MAX 96

static inline std::string
ack_header_reply(double ticks_dequeue, double ticks_done,
                 int swapcount, long long flipwall_us,
                 const char *result, size_t result_len)
{
  char hdr[ACK_HEADER_MAX];
  int hn = snprintf(hdr, sizeof(hdr), "ACK1 %.3f %.3f %u %lld\n",
                    ticks_dequeue, ticks_done, (unsigned) swapcount,
                    flipwall_us);
  if (hn < 0) hn = 0;
  else if ((size_t) hn >= sizeof(hdr)) hn = sizeof(hdr) - 1;
  std::string reply;
  reply.reserve((size_t) hn + result_len);
  reply.append(hdr, (size_t) hn);
  reply.append(result, result_len);
  return reply;
}

/* Strip the optional '@' (ack header) and '!' (swap ack) prefixes, in that
   order, from an incoming command.  Shared by the CR/LF and the framed
   message ports so both accept the same prefixes.  Returns the number of
   prefix bytes to skip.  A bare '!' or an unprefixed command behaves
   exactly as before '@' existed. */
static inline size_t
parse_request_prefix(const char *buf, size_t len,
                     bool *ack_header, bool *wait_for_swap)
{
  size_t off = 0;
  *ack_header = false;
  *wait_for_swap = false;
  if (off < len && buf[off] == '@') { *ack_header = true; off++; }
  if (off < len && buf[off] == '!') { *wait_for_swap = true; off++; }
  return off;
}

typedef struct ds_client_request_s {
  std::string datapoint_string;
  SharedQueue<std::string> *rqueue;
} ds_client_request_t;


//-----------------------------------------------------------------------------
// [SECTION] Example App: Debug Log / ShowExampleAppLog()
//-----------------------------------------------------------------------------

// Usage:
//  static AppLog my_log;
//  my_log.AddLog("Hello %d world\n", 123);
//  my_log.Draw("title");
struct AppLog
{
    ImGuiTextBuffer     Buf;
    ImGuiTextFilter     Filter;
    ImVector<int>       LineOffsets; // Index to lines offset. We maintain this with AddLog() calls.
    bool                AutoScroll;  // Keep scrolling if already at the bottom.

    AppLog()
    {
        AutoScroll = true;
        Clear();
    }

    void    Clear()
    {
        Buf.clear();
        LineOffsets.clear();
        LineOffsets.push_back(0);
    }

    void    AddLog(const char* fmt, ...) IM_FMTARGS(2)
    {
        int old_size = Buf.size();
        va_list args;
        va_start(args, fmt);
        Buf.appendfv(fmt, args);
        va_end(args);
        for (int new_size = Buf.size(); old_size < new_size; old_size++)
            if (Buf[old_size] == '\n')
                LineOffsets.push_back(old_size + 1);
    }

    void    Draw(const char* title, bool* p_open = NULL)
    {
        if (!ImGui::Begin(title, p_open))
        {
            ImGui::End();
            return;
        }

        // Options menu
        if (ImGui::BeginPopup("Options"))
        {
            ImGui::Checkbox("Auto-scroll", &AutoScroll);
            ImGui::EndPopup();
        }

        // Main window
        if (ImGui::Button("Options"))
            ImGui::OpenPopup("Options");
        ImGui::SameLine();
        bool clear = ImGui::Button("Clear");
        ImGui::SameLine();
        bool copy = ImGui::Button("Copy");
        ImGui::SameLine();
        Filter.Draw("Filter", -100.0f);

        ImGui::Separator();
        ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        if (clear)
            Clear();
        if (copy)
            ImGui::LogToClipboard();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        const char* buf = Buf.begin();
        const char* buf_end = Buf.end();
        if (Filter.IsActive())
        {
            // In this example we don't use the clipper when Filter is enabled.
            // This is because we don't have a random access on the result on our filter.
            // A real application processing logs with ten of thousands of entries may want to store the result of
            // search/filter.. especially if the filtering function is not trivial (e.g. reg-exp).
            for (int line_no = 0; line_no < LineOffsets.Size; line_no++)
            {
                const char* line_start = buf + LineOffsets[line_no];
                const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                if (Filter.PassFilter(line_start, line_end))
                    ImGui::TextUnformatted(line_start, line_end);
            }
        }
        else
        {
            // The simplest and easy way to display the entire buffer:
            //   ImGui::TextUnformatted(buf_begin, buf_end);
            // And it'll just work. TextUnformatted() has specialization for large blob of text and will fast-forward
            // to skip non-visible lines. Here we instead demonstrate using the clipper to only process lines that are
            // within the visible area.
            // If you have tens of thousands of items and their processing cost is non-negligible, coarse clipping them
            // on your side is recommended. Using ImGuiListClipper requires
            // - A) random access into your data
            // - B) items all being the  same height,
            // both of which we can handle since we an array pointing to the beginning of each line of text.
            // When using the filter (in the block of code above) we don't have random access into the data to display
            // anymore, which is why we don't use the clipper. Storing or skimming through the search result would make
            // it possible (and would be recommended if you want to search through tens of thousands of entries).
            ImGuiListClipper clipper;
            clipper.Begin(LineOffsets.Size);
            while (clipper.Step())
            {
                for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
                {
                    const char* line_start = buf + LineOffsets[line_no];
                    const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                    ImGui::TextUnformatted(line_start, line_end);
                }
            }
            clipper.End();
        }
        ImGui::PopStyleVar();

        if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::End();
    }

    void    DrawEmbedded()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        const char* buf = Buf.begin();
        const char* buf_end = Buf.end();
        ImGuiListClipper clipper;
        clipper.Begin(LineOffsets.Size);
        while (clipper.Step())
        {
            for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
            {
                const char* line_start = buf + LineOffsets[line_no];
                const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                ImGui::TextUnformatted(line_start, line_end);
            }
        }
        clipper.End();
        ImGui::PopStyleVar();

        if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
};


// =============================================================================
// Tcl puts command override - captures script output to WebSocket
// =============================================================================
// Tcl puts command override - captures script output to WebSocket
static int tcl_puts_cmd(ClientData clientData, Tcl_Interp *interp,
                        int objc, Tcl_Obj *const objv[]) {
    bool nonewline = false;
    int argIdx = 1;
    const char* channelId = NULL;
    const char* message = NULL;
    
    // Parse arguments
    if (objc < 2 || objc > 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-nonewline? ?channelId? string");
        return TCL_ERROR;
    }
    
    // Check for -nonewline
    if (strcmp(Tcl_GetString(objv[argIdx]), "-nonewline") == 0) {
        nonewline = true;
        argIdx++;
    }
    
    // Two arguments left: channelId + string
    if (argIdx + 1 < objc) {
        channelId = Tcl_GetString(objv[argIdx]);
        argIdx++;
        message = Tcl_GetString(objv[argIdx]);
    }
    // One argument left: just string
    else if (argIdx < objc) {
        message = Tcl_GetString(objv[argIdx]);
    }
    else {
        Tcl_WrongNumArgs(interp, 1, objv, "?-nonewline? ?channelId? string");
        return TCL_ERROR;
    }
    
    // If writing to a file handle, write directly to the channel
    // rather than building a string command and Tcl_Eval'ing it.
    // The string-eval approach was brace-wrapping the message
    // ("_puts ... {<message>}") which broke whenever the message
    // contained a 0x7D ('}') byte -- e.g. binary payloads from
    // qpcs::dsSocketSendBinary -- producing the parser error
    // "extra characters after close-brace". Tcl_WriteObj is binary-
    // safe and respects the channel's translation/encoding settings
    // (so a channel configured -translation binary writes bytes
    // verbatim, while text channels still get newline translation).
    if (channelId != NULL &&
        strcmp(channelId, "stdout") != 0 &&
        strcmp(channelId, "stderr") != 0) {
        Tcl_Channel chan = Tcl_GetChannel(interp, channelId, NULL);
        if (chan == NULL) {
            return TCL_ERROR;  // Tcl_GetChannel sets the error message
        }
        // Use the original Tcl_Obj so binary content is preserved.
        // objv[argIdx] still points at the message arg.
        if (Tcl_WriteObj(chan, objv[argIdx]) < 0) {
            Tcl_SetObjResult(interp,
                Tcl_NewStringObj("puts: write failed", -1));
            return TCL_ERROR;
        }
        if (!nonewline) {
            Tcl_Obj *nl = Tcl_NewStringObj("\n", 1);
            Tcl_IncrRefCount(nl);
            int rc = Tcl_WriteObj(chan, nl);
            Tcl_DecrRefCount(nl);
            if (rc < 0) {
                Tcl_SetObjResult(interp,
                    Tcl_NewStringObj("puts: newline write failed", -1));
                return TCL_ERROR;
            }
        }
        return TCL_OK;
    }
    
    // Determine log level
    const char* level = "info";
    if (channelId && strcmp(channelId, "stderr") == 0) {
        level = "warn";
    }
    
    // Log to our system
    log_message(level, "%s", message);
    
    return TCL_OK;
}

class Application
{
  bool m_bDataLoaded;
  
  Tcl_Interp *interp;

  GLuint vao;           // gotta have at least one!  ARGH
  std::atomic<bool> done;
  std::atomic<bool> m_bDone;

  // wake up queue
  SharedQueue<int> wake_queue;
  
  Timer appTimer;
  SharedQueue<int> tqueue;

  Timer::timer_id timerID;
#if defined(__APPLE__)
  SleepWakeHandler sleepWakeHandler;
#endif
  std::atomic<bool> systemIsSleeping{false};  
  
  // for GPIO swap acknowledge
  int output_pin;
  
public:
  bool isSleeping() { return systemIsSleeping; }
  void wakeup(void) { wake_queue.push_back(0); }

  void wait_for_wakeup(void) {
#ifdef __APPLE__
    // On macOS, use timeout only during sleep/wake transitions
    if (systemIsSleeping) {
      // During sleep, use a longer timeout to avoid blocking indefinitely
      wake_queue.wait_with_timeout(1000);  // 1 second timeout during sleep
    } else {
      // Normal operation - pure event-driven, no timeout
      int req = wake_queue.front();
      wake_queue.pop_front();
    }
#else
    // Non-macOS: always use pure event-driven approach
    int req = wake_queue.front();
    wake_queue.pop_front();
#endif
    
    // Clean up any remaining signals
    wake_queue.clear_all();
  }

  /* Late-draw's timed wait: block at most ms (>= 1) for a wake-up, then
     drain the queue exactly as wait_for_wakeup does.  Only the deferral
     window uses it; every other iteration still blocks indefinitely. */
  void wait_for_wakeup_ms(int ms) {
    wake_queue.wait_with_timeout(ms);
    wake_queue.clear_all();
  }

  /* ---- draw-late scheduler (render thread only; see latedraw.h) -------- */
  latedraw_state_t latedraw = {};
  double latedraw_swap_submit = 0.0;  /* glfwGetTime() right after the last
                                         glfwSwapBuffers while enabled */
  double latedraw_gpu_done = 0.0;     /* glfwGetTime() when the GPU passed
                                         the fence placed before that swap;
                                         < submit when no fence was timed */
  long latedraw_governed_swap = -1;   /* SwapCount after the last swap the
                                         scheduler governed; -1 none yet */
  GLsync latedraw_fence = nullptr;    /* fence between draw and swap */
  int latedraw_gpu_timed = 0;         /* fence entry points available */

  /* What lateDrawDefer() learned about this iteration, for the accounting
     after the frame it allowed.  active: the scheduler governed this frame
     (enabled, dynamic, period and a flip timestamp known).  warmup: it was
     drawn at once rather than deferred, because the last swap was not one
     the scheduler produced (see latedraw.h). */
  struct LateDrawFrame {
    bool active = false;
    bool warmup = false;
    double t0 = 0.0;          /* clock just before the object step */
    double flip0 = 0.0;       /* flip the target was computed from */
    long missed0 = 0;         /* swapsyncMissedFrames() before the draw */
  };

  /* Decide whether the next dynamic frame must wait.  Returns true to skip
     the step+draw this iteration, with *wait_ms the whole milliseconds the
     next wait may block (0 = don't wait at all).  Costs one clock read and
     a comparison on the governed path; a few plain loads otherwise.

     The first frame after a swap the scheduler did not make -- the static
     redraw that ended an idle gap, a command's redraw, the first frame
     after enable -- is a warm-up: drawn now, not at target, so a fresh
     scene's first-frame GPU cost has the whole period in front of it. */
  bool lateDrawDefer(LateDrawFrame *f, int *wait_ms) {
    if (!latedraw.enabled || !OL_DYNAMIC(OBJList)) return false;
    double flip = swapsyncLastFlipTime();
    if (flip <= 0.0) return false;                 /* dormant: no flips */
    double period = latedrawPeriodMs(&latedraw, swapsyncLastRefreshMs());
    if (period <= 0.0) return false;               /* dormant: no period */
    double now = glfwGetTime();
    bool warmup = latedrawIsWarmup((long) SwapCount, latedraw_governed_swap) != 0;
    if (!warmup) {
      double target = latedrawTarget(flip, period, latedraw.effective_ms);
      if (now < target) {
        *wait_ms = latedrawWaitMs(now, target);
        return true;
      }
    }
    f->active = true;
    f->warmup = warmup;
    f->t0 = now;
    f->flip0 = flip;
    f->missed0 = swapsyncMissedFrames();
    return false;
  }

  /* Around every swap while the scheduler is enabled: a fence after the
     draw commands, and after glfwSwapBuffers() returns, wait for the GPU to
     pass it.  The flip cannot happen before the GPU is done with the frame,
     and the loop is about to block in waitForSwap() for that flip anyway,
     so the wait costs nothing while it measures the GPU tail the CPU submit
     time cannot see.  Bounded (one period of a slow display) so a stuck
     driver cannot hang the render thread here; a timeout just leaves the
     tail unmeasured for that frame.  Without the fence entry points (an
     ES2 context) gpu_ms is simply 0. */
  void lateDrawBeforeSwap() {
    if (!latedraw.enabled) return;
    if (glFenceSync && glClientWaitSync && glDeleteSync) {
      latedraw_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      latedraw_gpu_timed = latedraw_fence != nullptr;
    }
  }
  void lateDrawAfterSwap() {
    if (!latedraw.enabled) return;
    latedraw_swap_submit = glfwGetTime();
    latedraw_gpu_done = 0.0;
    if (latedraw_fence) {
      GLenum r = glClientWaitSync(latedraw_fence, GL_SYNC_FLUSH_COMMANDS_BIT,
                                  (GLuint64) 100000000);   /* 100 ms */
      if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED)
        latedraw_gpu_done = glfwGetTime();
      glDeleteSync(latedraw_fence);
      latedraw_fence = nullptr;
    }
  }

  /* After a frame the scheduler governed: draw time (step to swap submit),
     GPU tail (submit to fence), late detection (missed counter advanced or
     flip interval > 1.5 periods), and the budget adaptation.  A frame whose
     arm did not swap (no new submit stamp) is not counted.  The swap count
     this frame left behind is remembered so the next pass can tell a
     governed swap from a foreign one (a command's redraw, a static frame). */
  void lateDrawAccount(const LateDrawFrame &f) {
    if (!f.active || latedraw_swap_submit < f.t0) return;
    double flip1 = swapsyncLastFlipTime();
    double interval_ms = (flip1 - f.flip0) * 1000.0;
    latedrawNoteFlipInterval(&latedraw, interval_ms);
    double period = latedrawPeriodMs(&latedraw, swapsyncLastRefreshMs());
    double draw_ms = (latedraw_swap_submit - f.t0) * 1000.0;
    double gpu_ms = latedraw_gpu_done > latedraw_swap_submit
      ? (latedraw_gpu_done - latedraw_swap_submit) * 1000.0 : 0.0;
    int late = latedrawIsLate(f.missed0, swapsyncMissedFrames(),
                              interval_ms, period);
    latedrawOnFrame(&latedraw, draw_ms, gpu_ms, late, f.warmup ? 1 : 0,
                    period);
    latedraw_governed_swap = (long) SwapCount;
  }

  
  // for client requests over tcp
  SharedQueue<client_request_t *> queue;
  SharedQueue<client_request_t *> reply_queue;

  // for dataserver notifications
  SharedQueue<ds_client_request_t *> ds_queue;
  SharedQueue<ds_client_request_t *> ds_reply_queue;
  
  GLFWwindow* window;   /* main window pointer */
  GLFWcursor *hidden_cursor, *standard_cursor;

  std::thread net_thread;
  std::thread ds_net_thread;
  std::thread msg_thread;
  
  void updateDisplay(bool);
  void processImgui(void);
  void toggleImgui(void);
  void showAppLog(bool *);
  int doUpdate(void);
  int fullscreen;
  int width;
  int height;
  int xpos;
  int ypos;
  int verbose;
  int log_level = 0;
  int timer_interval = 1;

  bool wait_for_swap;       // flag to help check timing

  // imgui status
  bool show_demo_window = false;
  bool show_console = false;
  bool show_log = true;

  void start_tcp_server(void);     // crlf oriented commands
  void start_dstcp_server(void);   // data point receiver
  void start_msg_server(void); // frame oriented with size prefix

  static void
  tcp_client_process(int sockfd,
             SharedQueue<client_request_t *> *queue);
  static void
  ds_client_process(int sockfd,
            SharedQueue<ds_client_request_t *> *queue);
  static void
  message_client_process(int sockfd,
             SharedQueue<client_request_t *> *queue);
  
  bool show_imgui = false;
  AppLog log;

  // frame tagging: payloads bound to the next completed swap (main thread
  // only -- swapTag runs from Tcl, delivery from updateDisplay)
  std::vector<std::string> swap_tags;
  std::string swap_tag_callback;
  void deliverSwapTags(void);

  // frame-counted work (main thread only, like swap tags): scripts to run
  // after N swaps, and grabs of the next drawn frame
  struct FrameAfter { int at; std::string script; };
  struct FrameGrab {
    std::string script;
    int x, y, w, h, keep_alpha;
    bool captured = false;
    int gw = 0, gh = 0;
    std::vector<unsigned char> pixels;
  };
  std::vector<FrameAfter> frame_afters;
  std::vector<FrameGrab> frame_grabs;
  void captureFrameGrabs(void);
  void deliverFrameWork(void);
  bool frameWorkPending(void) {
    return !frame_grabs.empty() || !frame_afters.empty();
  }
  
  const char *title;
  int tcpport = 4610;
  int dsport = 4611;
  int messageport = 4612;

  // WebSocket server members
  int websocket_port = 4613;
  std::string www_path;
  WebSocketServer* ws_server = nullptr;
  
  unsigned int last_frame_time = 0;
  int frame_counter = 0;
  double current_fps = 0.0;
  int total_frames = 0;

  ShutdownCmds shutdown_cmds;
  
Application()
{
    // Determine www_path based on platform and install location
    #ifdef __APPLE__
        CFBundleRef mainBundle = CFBundleGetMainBundle();
        if (mainBundle) {
            CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
            char path[PATH_MAX];
            if (CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8 *)path, PATH_MAX)) {
                www_path = std::string(path) + "/www";
            }
            CFRelease(resourcesURL);
        }
        // Fallback for development
        if (www_path.empty()) {
            www_path = "./www";
        }
        
        // Set up sleep/wake callbacks (MOVED INSIDE)
        sleepWakeHandler.setSleepCallback([this]() {
            onSystemSleep();
        });
        
        sleepWakeHandler.setWakeCallback([this]() {
            onSystemWake();
        });
        
        sleepWakeHandler.startMonitoring();
    #else
        // Check if running from install location
        struct stat st;
        if (stat("/usr/local/stim2/www", &st) == 0) {
            www_path = "/usr/local/stim2/www";
        } else {
            www_path = "./www";
        }
    #endif
    
    m_bDone = false;
    done = false;
    fullscreen = 0;
}

  ~Application()
  {
#if defined(__APPLE__)
    sleepWakeHandler.stopMonitoring();
    stopTimer();
#endif
  }

  // Initialize WebSocket server (call after window creation)
  void init_websocket() {
      ws_server = new WebSocketServer(websocket_port, www_path);
      if (!ws_server->start()) {
          fprintf(stderr, "FATAL: Failed to start WebSocket server on port %d\n", websocket_port);
          exit(1);
      }
      
      log_message("info", "WebSocket console available at http://localhost:%d/console.html", websocket_port);
  }

void count_frame() {
    frame_counter++;
    total_frames++;  // ADD THIS
    unsigned int elapsed = StimTime - last_frame_time;
    if (elapsed >= 1000) {
        current_fps = frame_counter * 1000.0 / elapsed;
        frame_counter = 0;
        last_frame_time = StimTime;
    }
}

void processWebSocketCommands() {
    if (!ws_server) return;
    
    while (ws_server->has_command()) {
        CommandRequest req = ws_server->get_command();
        
        if (req.cmd == "eval") {
            int result = Tcl_Eval(interp, req.script.c_str());
            
            if (result == TCL_OK) {
                const char* resultStr = Tcl_GetStringResult(interp);
                ws_server->send_response(req.requestId, true, resultStr);
            } else {
                const char* errorStr = Tcl_GetStringResult(interp);
                ws_server->send_response(req.requestId, false, errorStr);
            }
        }
    }
}

void update_websocket_status(double fps_val, int frame_val, int elapsed_val, const char* state_val) {
    if (ws_server) {
        ws_server->update_status(fps_val, frame_val, elapsed_val, state_val);
    }
}

  // Shutdown WebSocket server
  void shutdown_websocket() {
      if (ws_server) {
          ws_server->stop();
          delete ws_server;
          ws_server = nullptr;
      }
  }

      // Process WebSocket commands (call in main loop)
    void process_websocket_commands(Tcl_Interp* interp) {
        if (!ws_server) return;
        
        while (ws_server->has_command()) {
            CommandRequest req = ws_server->get_command();
            
            if (req.cmd == "eval") {
                // Execute Tcl script
                int result = Tcl_Eval(interp, req.script.c_str());
                
                if (result == TCL_OK) {
                    const char* resultStr = Tcl_GetStringResult(interp);
                    ws_server->send_response(req.requestId, true, resultStr);
                } else {
                    const char* errorStr = Tcl_GetStringResult(interp);
                    ws_server->send_response(req.requestId, false, errorStr);
                }
            }
        }
    }
    

  void init_gpio(int pin)
  {
    output_pin = pin;
    
#ifdef JETSON_NANO
    // Pin Setup.
    GPIO::setmode(GPIO::BOARD);
    // set pin as an output pin with optional initial state of HIGH
    GPIO::setup(pin, GPIO::OUT, GPIO::LOW);
#endif

  }
  
  void init_imgui(void)
  {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;    // no .ini persistence

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);

#ifdef STIM2_USE_GLES
    const char* glsl_version = "#version 300 es";
#else
    const char* glsl_version = "#version 150";
#endif
    ImGui_ImplOpenGL3_Init(glsl_version);
  }
  
  
  void shutdown(void)
  {
    m_bDone = true;
  }

  void init(void)
  {

#if !defined(_WIN32)
    char hostname[128];
    gethostname(hostname, 128);
    setenv("COMPUTERNAME", hostname, 0);
#endif /* _WIN32 */

      OBJList = objListCreate();

      //      glfwGetFramebufferSize(window, &winWidth, &winHeight);

      glfwGetWindowContentScale(window, &XScale, &YScale);
      
      glfwSwapInterval(1);

      HALF_SCREEN_DEG_X = 9;
      HALF_SCREEN_DEG_Y =
    HALF_SCREEN_DEG_X*((float) winHeight / (float) winWidth);   
      PIX_PER_DEG_X = (winWidth/2)/HALF_SCREEN_DEG_X;
      PIX_PER_DEG_Y = (winHeight/2)/HALF_SCREEN_DEG_Y;

      /* Set default projection */
      defaultProjection();

      setBackgroundColor();
 
      /* a CORE OpenGL requirement is to have a bound VAO */
      glGenVertexArrays(1, &vao);
      /* Guarantee at least one is bound...*/
      glBindVertexArray(vao);

      /* Create a single group to start */
      glistInit(GList, 1);


      unsigned char pixels[2 * 2 * 4];
      memset(pixels, 0x00, sizeof(pixels));
      
      GLFWimage image;
      image.width = 2;
      image.height = 2;
      image.pixels = pixels;
      
      hidden_cursor = glfwCreateCursor(&image, 0, 0);
      standard_cursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
  }
  
  bool isDataLoaded()
  {
    return m_bDataLoaded;
  }
  
  bool isDone()
  {
    return m_bDone;
  }

  void hideCursor()
  {
    glfwSetCursor(window, hidden_cursor);
  }
  
  void showCursor()
  {
    glfwSetCursor(window, standard_cursor);
  }

  /* Put the drawn frame on the display.  Headless, the offscreen target
     is the display, so there is nothing to flip. */
  void presentFrame()
  {
    if (Headless) { glFlush(); return; }
    lateDrawBeforeSwap();
    swapsyncBeforeSwap();
    glfwSwapBuffers(window);
    lateDrawAfterSwap();
  }

  void waitForSwap()
  {
    if (Headless) {
      headlessPace();
    }
    else {
      // Strategy chosen at startup by swapsyncInit(): presentation feedback
      // (X11 OML sync control / Wayland wp_presentation) where the platform
      // can report the actual flip, glFinish() otherwise.  The old
      // JETSON_XAVIER fence variant is gone: it leaked one GLsync per frame
      // and, like glFinish, only proved GPU completion -- not the flip.
      swapsyncAfterSwap();
    }
    /* stepFrame's flip time: the clock reads it from here on, as the rig's
       clock read the flip once its swap wait returned */
    if (StepMode && step_flip_sec >= 0.0) {
      StepClockSec = step_flip_sec;
      step_flip_sec = -1.0;
    }
  }

  /* Headless and not stepped: stand in for vsync by holding each frame to
     one refresh period of the real clock, so a dynamic display runs at the
     rate it would on a monitor instead of as fast as the GPU allows.
     Stepped, frames come when stepFrame asks and are not held. */
  double headless_next_frame = 0.0;
  void headlessPace()
  {
    if (StepMode) return;
    double period = FrameDuration > 0.0f ? FrameDuration / 1000.0 : 1.0 / 60.0;
    double now = glfwGetTime();
    if (headless_next_frame > now) {
      std::this_thread::sleep_for(std::chrono::duration<double>(headless_next_frame - now));
      headless_next_frame += period;
    }
    else headless_next_frame = now + period;   /* late or idle: restart */
  }

  /* The headless display: a framebuffer of exactly w x h pixels (no
     Retina or desktop scaling), bound for the life of the process in place
     of the window's default framebuffer.  Code that binds its own FBO must
     restore the previous binding rather than 0. */
  GLuint headless_fbo = 0, headless_color = 0, headless_depth = 0;
  bool initHeadlessTarget(int w, int h)
  {
    glGenFramebuffers(1, &headless_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, headless_fbo);
    glGenRenderbuffers(1, &headless_color);
    glBindRenderbuffer(GL_RENDERBUFFER, headless_color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, headless_color);
    glGenRenderbuffers(1, &headless_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, headless_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, headless_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      return false;
    GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, bufs);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, w, h);
    return true;
  }

  /* Frame sink (frameSink in tclproc.c): every presented frame, as raw
     RGBA rows top-down with no header, written to a file or to a command's
     stdin -- for ffmpeg, -f rawvideo -pix_fmt rgba -s WxH -i - .  Read
     from the drawn buffer just before it is presented, like frameGrab.
     Manual (-manual): each presented frame is only held, and written when
     frameSink write n says, n times -- a frame-locked replay writes each
     rig frame once per movie tick it spans (0 to drop, >1 to hold). */
  struct FrameSink {
    FILE *fp = nullptr;
    bool is_pipe = false;
    bool manual = false;
    bool held = false;            /* rows holds a presented frame */
    std::string target;
    int w = 0, h = 0;
    long long frames = 0;
    bool failed = false;          /* a write failed; the sink stops writing */
    std::vector<unsigned char> buf, rows;
  } sink;

  bool sinkWrite(void)
  {
    if (fwrite(sink.rows.data(), 1, sink.rows.size(), sink.fp) != sink.rows.size()) {
      sink.failed = true;
      log_message("error", "frameSink: write to %s failed after %lld frames",
                  sink.target.c_str(), sink.frames);
      return false;
    }
    sink.frames++;
    return true;
  }

  void sinkFrame()
  {
    if (!sink.fp || sink.failed) return;
    size_t rowbytes = (size_t) sink.w * 4;
    sink.buf.resize(rowbytes * sink.h);
    sink.rows.resize(rowbytes * sink.h);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(drawnBuffer());
    glReadPixels(0, 0, sink.w, sink.h, GL_RGBA, GL_UNSIGNED_BYTE, sink.buf.data());
    for (int i = 0; i < sink.h; i++)      /* GL rows are bottom-up */
      memcpy(&sink.rows[i * rowbytes], &sink.buf[(size_t) (sink.h - i - 1) * rowbytes],
             rowbytes);
    for (size_t i = 3; i < sink.rows.size(); i += 4) sink.rows[i] = 255;
    sink.held = true;
    if (!sink.manual) sinkWrite();
  }

  /* stepFrame bookkeeping: a redraw asked for while stepped is held here
     for the next stepFrame instead of drawing at once, and the flip time
     that stepFrame gave is applied by waitForSwap */
  bool step_redraw = false;
  double step_flip_sec = -1.0;

private:  
  void
  startTimerImpl() {
    timerID = appTimer.create(0, timer_interval, [this]() {
      if (!systemIsSleeping) {
    updateTimes();
    if (NextFrameTime >= 0 && StimTime >=
        (unsigned int) NextFrameTime) {
      NextFrameTime = -1;
      kickAnimation();
    }
    tqueue.push_back(StimTime);
    do_wakeup();
      }
    });
  }
public:

  void startTimer() {
    /* stepped, the clock moves only by stepClock/stepFrame (stepAdvance
       does the timer's work); main starts the timer after the startup
       file, which may already have turned step mode on */
    if (!systemIsSleeping && !StepMode) {
      startTimerImpl();
    }    
  }
  
  void stopTimer() {
    if (timerID != 0) {
      appTimer.destroy(timerID);
      timerID = 0;
    }
  }
  
  void onSystemSleep() {
    systemIsSleeping = true;
    // Wake up any currently blocked wait_for_wakeup() with timeout behavior
    wakeup();
  }
  
  void onSystemWake() {
    // Wake up immediately, then mark as not sleeping
    wakeup();
    glfwPostEmptyEvent();
    systemIsSleeping = false;
  }  
  
  int Tcl_StimAppInit(Tcl_Interp *interp)
  {

    static char curdir[256], *cd;
#ifdef _WIN32
    cd = _getcwd(curdir, sizeof(curdir));
#else
    cd = getcwd(curdir, sizeof(curdir));
#endif
    
    if (Tcl_Init(interp) == TCL_ERROR) return TCL_ERROR;

    addTclCommands(interp);

    Tcl_CreateCommand(interp, "getMouseWorld",
                      (Tcl_CmdProc *) getMouseWorldCmd,
                      (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

    Tcl_VarEval(interp, 
        "proc load_local_packages {} {\n"
        " global auto_path\n"
        " set f [file dirname [info nameofexecutable]]\n"
#ifdef _WIN32
        " set sysdlsh c:/usr/local/dlsh/dlsh.zip\n"
#else
        " set sysdlsh /usr/local/dlsh/dlsh.zip\n"
#endif
        // Candidate dlsh.zip locations, in priority order, as the dlsh
        // interpreter finds them (dlsh_main.c): $DLSH_ZIP (the zip file),
        // $DLSH_LIBRARY/dlsh.zip (DLSH_LIBRARY is a DIRECTORY; one naming a
        // file is still taken as the zip, as stim2 used to read it), the
        // shared system install (so labs can update dlsh.zip without
        // rebuilding the app), then copies bundled with the app -- next to
        // the executable (Linux/Windows) or in the macOS .app
        // Contents/Resources dir. Data must live in Resources, not MacOS, or
        // macOS code signing/notarization rejects the bundle. Only a regular
        // file counts: a directory left in DLSH_LIBRARY (start-stim.sh's old
        // /usr/local/lib/tcltk/dlsh) used to be "found" and fail to mount.
        " set candidates {}\n"
        " if {[info exists ::env(DLSH_ZIP)]} { lappend candidates $::env(DLSH_ZIP) }\n"
        " if {[info exists ::env(DLSH_LIBRARY)]} {\n"
        "  set l $::env(DLSH_LIBRARY)\n"
        "  lappend candidates [expr {[file isfile $l] ? $l : [file join $l dlsh.zip]}]\n"
        " }\n"
        " lappend candidates $sysdlsh\n"
        " lappend candidates [file join $f dlsh.zip]\n"
        " lappend candidates [file normalize [file join $f .. Resources dlsh.zip]]\n"
        " set dlshzip [lindex $candidates end]\n"
        " foreach c $candidates { if {[file isfile $c]} { set dlshzip $c; break } }\n"
        " set dlshroot [file join [zipfs root] dlsh]\n"
        " catch { zipfs unmount $dlshroot }\n"
        " zipfs mount $dlshzip $dlshroot\n"
        " set auto_path [linsert $auto_path [set auto_path 0] $dlshroot/lib]\n"
        "package require dlsh; package require qpcs }\n"
        "load_local_packages",
        NULL);
#ifdef _WIN32
    Tcl_VarEval(interp, "set env(PATH) \"stimdlls;"
        "[file dir [info nameofexecutable]]/stimdlls;$env(PATH)\"",
        NULL);
#else
    Tcl_VarEval(interp, "lappend auto_path stimdlls", NULL);
    Tcl_VarEval(interp, "lappend auto_path [file dir [info nameofexecutable]]/stimdlls", NULL);

#endif
    Tcl_VarEval(interp, 
        "lappend auto_path [pwd]/packages",
        NULL);


    // Setup stim2 module path and load workspace
    Tcl_Eval(interp, R"(
proc setup_stim2_modules {} {
    set exe_dir [file dirname [info nameofexecutable]]
    
    # Platform-specific share directory
    if {$::tcl_platform(os) eq "Darwin"} {
        # macOS: check for app bundle Resources
        set bundle_resources [file normalize [file join $exe_dir .. Resources]]
        set share_dir /usr/local/stim2
    } elseif {$::tcl_platform(platform) eq "windows"} {
        set bundle_resources ""
        set share_dir c:/usr/local/stim2
    } else {
        set bundle_resources ""
        set share_dir /usr/local/stim2
    }
    
    # Build search path
    set search_dirs [list $exe_dir]
    if {$bundle_resources ne "" && [file isdirectory $bundle_resources]} {
        lappend search_dirs $bundle_resources
    }
    lappend search_dirs $share_dir [pwd]
    
    # Add module paths
    foreach dir $search_dirs {
        set moddir [file join $dir modules]
        if {[file isdirectory $moddir]} {
            ::tcl::tm::path add $moddir
        }
    }
    
    # Load workspace and find examples
    package require workspace
    
    foreach dir $search_dirs {
        set exdir [file join $dir examples]
        if {[file isdirectory $exdir]} {
            set ::workspace::system_examples_path $exdir
            break
        }
    }
}
setup_stim2_modules
)");


    Tcl_Eval(interp, R"(
proc setup_asset_paths {} {
    set exe_dir [file dirname [info nameofexecutable]]
    
    # Platform-specific defaults
    if {$::tcl_platform(os) eq "Darwin"} {
        set bundle_resources [file normalize [file join $exe_dir .. Resources]]
        set share_dir /usr/local/stim2
    } elseif {$::tcl_platform(platform) eq "windows"} {
        set bundle_resources ""
        set share_dir c:/usr/local/stim2
    } else {
        set bundle_resources ""
        set share_dir /usr/local/stim2
    }
    
    # Build search path list (in priority order)
    set search_dirs {}
    
    # 1. Environment variable (highest priority) - colon-separated
    if {[info exists ::env(STIM2_ASSETS)]} {
        foreach p [split $::env(STIM2_ASSETS) ":"] {
            set p [string trim $p]
            if {$p ne "" && [file isdirectory $p]} {
                lappend search_dirs $p
            }
        }
    }
    
    # 2. Current working directory assets/
    set cwd_assets [file join [pwd] assets]
    if {[file isdirectory $cwd_assets]} {
        lappend search_dirs $cwd_assets
    }
    
    # 3. Executable directory assets/
    set exe_assets [file join $exe_dir assets]
    if {[file isdirectory $exe_assets]} {
        lappend search_dirs $exe_assets
    }
    
    # 4. Bundle resources (macOS app bundle)
    if {$bundle_resources ne ""} {
        set bundle_assets [file join $bundle_resources assets]
        if {[file isdirectory $bundle_assets]} {
            lappend search_dirs $bundle_assets
        }
    }
    
    # 5. System share directory
    set share_assets [file join $share_dir assets]
    if {[file isdirectory $share_assets]} {
        lappend search_dirs $share_assets
    }
    
    # 6. Examples assets (for demos)
    if {[info exists ::workspace::system_examples_path]} {
        set examples_assets [file join $::workspace::system_examples_path assets]
        if {[file isdirectory $examples_assets]} {
            lappend search_dirs $examples_assets
        }
        # Also check for image-specific assets
        set image_assets [file join $::workspace::system_examples_path image assets]
        if {[file isdirectory $image_assets]} {
            lappend search_dirs $image_assets
        }
    }
    
    # Register all paths
    foreach dir $search_dirs {
        assetPath append $dir
    }
}
setup_asset_paths
)");

    Tcl_Eval(interp, R"(
proc screen_config {} {
    set monitor_hwidth  [expr {[screen_set ScreenWidthCm]/2.0}]
    set monitor_hheight [expr {[screen_set ScreenHeightCm]/2.0}]
    set distance_to_monitor [screen_set DistanceToMonitor]
    set deg_per_cm [expr atan2(1.0,$distance_to_monitor)*(180./$::pi)]
    
    screen_set HalfScreenDegreeX [expr $deg_per_cm*$monitor_hwidth]
    screen_set HalfScreenDegreeY [expr $deg_per_cm*$monitor_hheight]
    
    # Now reset pixels per degree variable
    set xres [screen_set ScreenWidth]
    set yres [screen_set ScreenHeight]
    set w [expr 2.*$monitor_hwidth]
    set h [expr 2.*$monitor_hheight]
    set ppdx [expr 1.0/(atan2($w,$distance_to_monitor)*(180./$::pi))*$xres]
    set ppdy [expr 1.0/(atan2($h,$distance_to_monitor)*(180./$::pi))*$yres]
    screen_set PixPerDegreeX $ppdx
    screen_set PixPerDegreeY $ppdy
    reshape
}
)");

    // Add objid <-> name table support
    OL_NAMEINFO(OBJList) = objNameInitCommands(interp, OBJList);
    

    // Override puts command
    Tcl_Eval(interp, "rename puts _puts");
    Tcl_CreateObjCommand(interp, "puts", tcl_puts_cmd, NULL, NULL);
  
    // Add command completion support
    TclCompletion::RegisterCompletionCommand(interp);

    // Add animation support
    Animate_Init(interp);
    
    return TCL_OK;
  }

  int sourceFile(const char *filename)
  {
    if (!interp) {
      std::cerr << "no tcl interpreter" << std::endl;
      return TCL_ERROR;
    }

    return Tcl_EvalFile(interp, filename);
  }
  
  int setupTcl(char *name, int argc, char **argv)
  {
    int exitCode = 0;

    Tcl_FindExecutable(name);

    interp = Tcl_CreateInterp();
    OurInterp = interp;

    if (!interp) {
      std::cerr << "Error initialializing tcl interpreter" << std::endl;
    }
#ifdef __APPLE__
    //    if (TclZipfs_Mount(interp, "/Applications/stim2.app/Contents/Resources/stim2.zip", "app", NULL) != TCL_OK) {
    //      std::cerr << "stim2: error mounting zipfs" << std::endl;
    //    }
#endif

#ifndef _MSC_VER
    TclZipfs_AppHook(&argc, &argv);
#else
    TclZipfs_AppHook(&argc, (wchar_t ***) &argv);
#endif    

    /*
     * Invoke application-specific initialization.
     */
    
    if (Tcl_StimAppInit(interp) != TCL_OK) {
      std::cerr << "application-specific initialization failed: ";
      std::cerr << Tcl_GetStringResult(interp) << std::endl;
    }
    else {
      Tcl_SourceRCFile(interp);
    }
    return TCL_OK;
  }

  int
  processTclCommands(void) {
    int n = 0;
    int retcode;
    client_request_t *req;
    
    wait_for_swap = false;
    
    while (queue.size()) {
      n++;
      req = queue.front();
      queue.pop_front();
      const char *script = req->script.c_str();

      /* Dequeue stamp for the ACK1 header: read the clock directly rather
         than StimTicksF, which the timer thread only refreshes every ms. */
      if (req->ack_header) req->ticks_dequeue = 1000.0 * stimClockNow();

      // std::cout << "processing tcl request: " << req->script << std::endl;

      if (req->wait_for_swap) {
    wait_for_swap = true;
    
    if (log_level) log.AddLog("[%.3f]: %s", glfwGetTime(), script);
#ifdef JETSON_NANO
    GPIO::output(output_pin, GPIO::HIGH);
#endif
      }

      retcode = Tcl_Eval(interp, script);
      FrameLogCmdsApplied++;      /* stim/frame: commands applied so far */

      const char *rcstr = Tcl_GetStringResult(interp);
      if (retcode == TCL_OK) {
    if (!req->wait_for_swap) {
      if (req->ack_header) {
        /* Immediate reply: done stamp right after Tcl_Eval, current
           SwapCount, no flip (0).  Pushing to rqueue hands the request
           back to its client thread, so nothing touches req after it. */
        const char *res = rcstr ? rcstr : "";
        req->rqueue->push_back(ack_header_reply(req->ticks_dequeue,
                                                1000.0 * stimClockNow(),
                                                SwapCount, 0,
                                                res, strlen(res)));
      }
      else if (rcstr) {
        req->rqueue->push_back(std::string(rcstr));
      }
      else {
        req->rqueue->push_back("");
      }
    }
    else {
      // put result back into the request and queue for after swap
      req->script = std::string(rcstr);
      req->swap_at_queue = SwapCount;
      reply_queue.push_back(req);
    }
      }
      else {
    if (!req->wait_for_swap) {
      if (req->ack_header) {
        std::string err = rcstr ? "!TCL_ERROR " + std::string(rcstr)
                                : std::string("Error:");
        if (rcstr) log.AddLog("[error]: %s\n", rcstr);
        req->rqueue->push_back(ack_header_reply(req->ticks_dequeue,
                                                1000.0 * stimClockNow(),
                                                SwapCount, 0,
                                                err.data(), err.size()));
      }
      else if (rcstr) {
        req->rqueue->push_back("!TCL_ERROR "+std::string(rcstr));
        //      std::cout << "Error: " + std::string(rcstr) << std::endl;
        log.AddLog("[error]: %s\n", rcstr);
      }
      else {
        req->rqueue->push_back("Error:");
      }
    }
    else {
      // put result back into the request and queue for after swap
      req->script = std::string("!TCL_ERROR "+std::string(rcstr));
      req->swap_at_queue = SwapCount;
      reply_queue.push_back(req);
    }
      }
    }

    /* Do this here? */
    while (Tcl_DoOneEvent(TCL_DONT_WAIT)) ;
    
    return n;
  }

  int
  processDSCommands(void) {
    int n = 0;
    int retcode;
    ds_client_request_t *req;

    while (ds_queue.size()) {
      n++;
      req = ds_queue.front();
      ds_queue.pop_front();
      FrameLogDsDispatched++;     /* stim/frame: every forwarded datapoint */
      char *dsstring = strdup(req->datapoint_string.c_str());
      /* The request is ours now (ds_client_process allocates one per
         datapoint) and the string is already copied out, so release it
         HERE -- that covers every exit below, including the early
         continue, without repeating the delete on each path. */
      delete req;
      const int dslen = strlen(dsstring);
      const char *p, *cmd;
      static char varname[128];

      /* remove newline in send string */
      if (dsstring[dslen-1] == '\n') dsstring[dslen-1] = '\0';

      //std::cout << "processDSCommands: " << dsstring << std::endl;

      /* Parse dataserver string and process */
      p = strchr(dsstring, ' ');
      if (!p) { free(dsstring); continue; }
      
      memcpy(varname, dsstring, p-dsstring);
      varname[p-dsstring] = '\0';
      Tcl_SetVar2(interp, "dsVals", varname, dsstring, TCL_GLOBAL_ONLY);
  
      /* Dispatch callback if it exists */
      /*  callback should include a proc name that takes one argument */
      if ((cmd = Tcl_GetVar2(interp, "dsCmds", varname, TCL_GLOBAL_ONLY))) {
    Tcl_VarEval(interp, cmd, " {*}", dsstring, (char *) NULL);
    //  Tcl_VarEval(interp, cmd, dsstring, (char *) NULL);  
    Tcl_ResetResult(interp);
      }
      
      /* clean up */
      free(dsstring);
    }
    return n;
  }
  
  int
  processReplies(void)
  {
    int n = 0;
    client_request_t *req;

    //    if (reply_queue.size()) glFinish();

    while (reply_queue.size()) {
      n++;
      req = reply_queue.front();
      reply_queue.pop_front();
      /* A '!' request promises "ack after the swap this command caused".
         If no swap happened this pass, that promise is broken -- the ack
         means only "end of loop pass".  Surface it when logging is on. */
      if (log_level && req->swap_at_queue == SwapCount)
	log.AddLog("[%.3f]: warning: swap-ack released with no intervening "
		   "swap: %s\n", glfwGetTime(), req->script.c_str());
      if (req->ack_header) {
	/* '@!' reply: done stamp, SwapCount and the hardware flip time are
	   read here, after the swap this command caused. */
	req->rqueue->push_back(ack_header_reply(req->ticks_dequeue,
						1000.0 * stimClockNow(),
						SwapCount,
						swapsyncLastFlipWallUs(),
						req->script.data(),
						req->script.size()));
      }
      else {
	req->rqueue->push_back(req->script);
      }
      if (log_level) log.AddLog("[%.3f]: %s\n", glfwGetTime(), req->script.c_str());
#ifdef JETSON_NANO
      GPIO::output(output_pin, GPIO::LOW);
#endif
      
    }
    return n;
  }
  
  int
  processTimerFuncs(void) {
    int n = 0;
    if (tqueue.size()) {
      OBJ_GROUP *g = NULL;
      if (OGL_NGROUPS(GList)) g = OGL_GROUP(GList, OGL_CURGROUP(GList));
      execTimerFuncs(g);
    }
    while (tqueue.size()) {
      n++;
      tqueue.pop_front();
    }
    return n;
  }
  
  bool
  processMessages(bool log_events) {
    int n = 0;
    bool did_update = 0;
    while (MessageQueue.size()) {
      n++;
      int msg = MessageQueue.front();
      MessageQueue.pop_front();

      /* Stepped, a frame is drawn only by stepFrame: what would draw now
         is applied and the redraw held for the next step. */
      switch (msg) {
      case SET_BACKGROUND:
    setBackgroundColor();
    if (StepMode) { step_redraw = true; break; }
    updateDisplay(log_events);
    did_update = 1;
    do_wakeup();
    break;
      case UPDATE_DISPLAY:
    if (StepMode) { step_redraw = true; break; }
    updateDisplay(log_events);
    did_update = 1;
    break;
      case RESHAPE_DISPLAY:
    winWidth = width;
    winWidth_2 = width/2;
    winHeight = height;
    defaultProjection();
    glViewport(0, 0, winWidth, winHeight);
    PIX_PER_DEG_X = (ScreenWidth/2)/HALF_SCREEN_DEG_X;
    PIX_PER_DEG_Y = (ScreenHeight/2)/HALF_SCREEN_DEG_Y;
    if (StepMode) { step_redraw = true; break; }
    updateDisplay(log_events);
    did_update = 1;
    do_wakeup();
    break;
      case SHOW_CURSOR:
    showCursor();
    break;
      case HIDE_CURSOR:
    hideCursor();
    break;
      case TOGGLE_IMGUI:
    toggleImgui();
    break;
      case DUMP_RAW:
    {
      dumpWindowAsRaw(&DumpInfo);
    }
      default:
    break;
      }
    }
    return did_update;      // did we update display
  }
};

// Allow other functions to access
Application app;

/* Websocket console port (--port-base + 3), for the diagnostics overlay */
int getWebSocketPort(void)
{
  return app.websocket_port;
}

/* C-linkage frame-tag glue for tclproc.c (declared in stim2.h) */
void swapTagAdd(const char *payload)
{
  app.swap_tags.push_back(payload ? payload : "");
}

void swapTagCallbackSet(const char *proc)
{
  app.swap_tag_callback = proc ? proc : "";
}

const char *swapTagCallbackGet(void)
{
  return app.swap_tag_callback.c_str();
}

/* C-linkage glue for afterFrames / frameGrab (declared in stim2.h).  While
   either is pending the main loop draws a frame on every pass (see
   frameWorkPending), so the work happens even when nothing is animating.
   No UPDATE_DISPLAY message is queued for it: a backlog of those would
   delay every other display message (setBackground, dumpRaw). */
void frameAfterAdd(int n, const char *script)
{
  app.frame_afters.push_back({SwapCount + n, script ? script : ""});
  do_wakeup();
}

int frameAfterPending(void)
{
  return (int) app.frame_afters.size();
}

void frameGrabAdd(const char *script, int x, int y, int w, int h,
		  int keep_alpha)
{
  Application::FrameGrab g;
  g.script = script ? script : "";
  g.x = x; g.y = y; g.w = w; g.h = h; g.keep_alpha = keep_alpha;
  app.frame_grabs.push_back(std::move(g));
  do_wakeup();
}

/*
 * Headless, step mode and the frame sink: the backing for stepMode,
 * stepClock, stepFrame and frameSink (tclproc.c).  All on the render
 * thread, where Tcl runs; the timer thread is stopped while stepped, so
 * nothing else moves the clock.
 */
int stimHeadless(void) { return Headless; }
int stimStepMode(void) { return StepMode; }
double stimStepClockMs(void) { return 1000.0 * stimClockNow(); }

/* What the timer thread does every tick, done on each move of the stepped
   clock instead: refresh the stim times, kick a time-based group whose
   next frame is due, run the objects' timer functions. */
static void stepAdvance(void)
{
  updateTimes();
  if (NextFrameTime >= 0 && StimTime >= (unsigned int) NextFrameTime) {
    NextFrameTime = -1;
    kickAnimation();
  }
  OBJ_GROUP *g = NULL;
  if (OGL_NGROUPS(GList)) g = OGL_GROUP(GList, OGL_CURGROUP(GList));
  execTimerFuncs(g);
}

/* The stepped clock only moves forward: StimTime is unsigned and the
   scripts' own frame logic assumes time does not run backwards. */
static int stepCheckForward(double t_ms, char *err, int errlen)
{
  if (t_ms < 1000.0 * StepClockSec - 1e-6) {
    snprintf(err, errlen, "the stepped clock is at %.3f ms and cannot go back to %.3f",
             1000.0 * StepClockSec, t_ms);
    return -1;
  }
  return 0;
}

int stimStepModeSet(int on, int have_t, double t_ms, char *err, int errlen)
{
  if (on) {
    if (!StepMode) {
      StepClockSec = have_t ? t_ms / 1000.0 : glfwGetTime();
      app.stopTimer();
      StepMode = 1;
      if (StimStart > StepClockSec) StimStart = StepClockSec;
      app.step_redraw = false;
      app.step_flip_sec = -1.0;
      stepAdvance();
    }
    else if (have_t) {
      if (stepCheckForward(t_ms, err, errlen)) return -1;
      StepClockSec = t_ms / 1000.0;
      stepAdvance();
    }
  }
  else if (StepMode) {
    bool pending = app.step_redraw;
    app.step_redraw = false;
    StepMode = 0;
    /* back on the real clock, which may be behind the stepped one */
    if (StimStart > glfwGetTime()) resetStimTime();
    updateTimes();
    app.startTimer();
    if (pending) redraw();
    do_wakeup();
  }
  return 0;
}

int stimStepClockSet(double t_ms, char *err, int errlen)
{
  if (!StepMode) { snprintf(err, errlen, "not in step mode (stepMode 1)"); return -1; }
  if (stepCheckForward(t_ms, err, errlen)) return -1;
  StepClockSec = t_ms / 1000.0;
  stepAdvance();
  return 0;
}

/* One frame, as the rig's loop would have drawn it at t_ms: move the clock
   there, apply what the messages queued (a held redraw draws as is; with
   none, a dynamic group steps its objects first, like the loop's
   doUpdate), draw, put the frame in the sink, and present.  flip_ms, when
   given (>= t_ms), is where the clock stands after the frame's swap, as
   the rig's clock stood at its flip. */
int stimStepFrame(double t_ms, double flip_ms, stim_step_result_t *r,
                  char *err, int errlen)
{
  if (!StepMode) { snprintf(err, errlen, "not in step mode (stepMode 1)"); return -1; }
  if (stepCheckForward(t_ms, err, errlen)) return -1;
  if (flip_ms >= 0.0 && flip_ms < t_ms) {
    snprintf(err, errlen, "flip time %.3f is before the frame time %.3f", flip_ms, t_ms);
    return -1;
  }
  StepClockSec = t_ms / 1000.0;
  stepAdvance();
  app.processMessages(false);       /* stepped: applies, holds the redraw */

  const char *kind;
  if (app.step_redraw) { app.step_redraw = false; kind = "redraw"; }
  else if (app.doUpdate()) kind = "update";
  else kind = "static";

  int swap0 = SwapCount;
  app.step_flip_sec = flip_ms >= 0.0 ? flip_ms / 1000.0 : -1.0;
  app.updateDisplay(false);
  app.count_frame();
  if (app.step_flip_sec >= 0.0) {   /* NO_SWAP: no swap wait applied it */
    StepClockSec = app.step_flip_sec;
    app.step_flip_sec = -1.0;
    updateTimes();
  }

  r->kind = kind;
  r->swapped = SwapCount != swap0;
  r->swapcount = SwapCount;
  r->ticks_ms = 1000.0 * StepClockSec;
  r->sink_frames = app.sink.frames;
  return 0;
}

int stimFrameSinkOpen(const char *target, int manual, char *err, int errlen)
{
  int status;
  if (app.sink.fp) stimFrameSinkClose(&status);
  if (!target || !*target) { snprintf(err, errlen, "empty target"); return -1; }
  FILE *fp;
  bool is_pipe = target[0] == '|';
  if (is_pipe) {
#ifdef _WIN32
    fp = _popen(target + 1, "wb");
#else
    /* a reader that exits (ffmpeg on a bad option) must fail the write,
       not kill stim2 */
    signal(SIGPIPE, SIG_IGN);
    fp = popen(target + 1, "w");
#endif
  }
  else fp = fopen(target, "wb");
  if (!fp) {
    snprintf(err, errlen, "cannot open %s: %s", target, strerror(errno));
    return -1;
  }
  app.sink.fp = fp;
  app.sink.is_pipe = is_pipe;
  app.sink.target = target;
  app.sink.w = winWidth;
  app.sink.h = winHeight;
  app.sink.frames = 0;
  app.sink.failed = false;
  app.sink.manual = manual != 0;
  app.sink.held = false;
  return 0;
}

/* Manual sink: write the last presented frame n times (0 is allowed). */
int stimFrameSinkWrite(int n, char *err, int errlen)
{
  if (!app.sink.fp) { snprintf(err, errlen, "no sink open"); return -1; }
  if (app.sink.failed) { snprintf(err, errlen, "an earlier write failed"); return -1; }
  if (!app.sink.held) { snprintf(err, errlen, "no frame presented since the sink opened"); return -1; }
  for (int i = 0; i < n; i++)
    if (!app.sinkWrite()) {
      snprintf(err, errlen, "write to %s failed", app.sink.target.c_str());
      return -1;
    }
  return 0;
}

/* Closing a pipe waits for the command to finish (ffmpeg writing the
   file's trailer); *status is its exit status, or fclose's result. */
int stimFrameSinkClose(int *status)
{
  if (!app.sink.fp) return 0;
  int st;
#ifdef _WIN32
  st = app.sink.is_pipe ? _pclose(app.sink.fp) : fclose(app.sink.fp);
#else
  st = app.sink.is_pipe ? pclose(app.sink.fp) : fclose(app.sink.fp);
  if (app.sink.is_pipe && st != -1 && WIFEXITED(st)) st = WEXITSTATUS(st);
#endif
  if (status) *status = st;
  app.sink.fp = nullptr;
  return 1;
}

void stimFrameSinkStatus(stim_sink_status_t *st)
{
  st->open = app.sink.fp != nullptr;
  st->target = app.sink.target.c_str();
  st->width = app.sink.w;
  st->height = app.sink.h;
  st->frames = app.sink.frames;
  st->failed = app.sink.failed;
  st->manual = app.sink.manual;
}


// Global logging helper
void log_message(const char* level, const char* fmt, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  
  // Always print to stderr
  fprintf(stderr, "[%s] %s\n", level, buffer);
  
  // Also send to WebSocket if available
  if (app.ws_server) {
    app.ws_server->log(level, buffer);
  }
}

static void do_wakeup(void)
{
  app.wakeup();
}

/*
 * stimLateDraw backing (tclproc.c).  All on the render thread: the Tcl
 * interpreter runs there, and the scheduler state is plain data.
 */
int stimLateDrawEnable(double budget_ms, char *err, int errlen)
{
  if (!(budget_ms > 0.0)) {
    snprintf(err, errlen, "budget must be a positive number of ms");
    return -1;
  }
  if (swapsyncLastFlipTime() <= 0.0 || swapsyncLastRefreshMs() <= 0.0) {
    snprintf(err, errlen,
             "late-draw needs hardware flip timestamps and a refresh period, "
             "but the \"%s\" swap-sync strategy has %s "
             "(flip timestamps are required; the oml and presentation "
             "strategies provide them once a frame has been swapped)",
             swapsyncStrategyName(),
             swapsyncLastFlipTime() <= 0.0 ? "reported no flip yet"
                                           : "no refresh period");
    return -1;
  }
  latedrawInit(&app.latedraw, budget_ms);
  app.latedraw_governed_swap = -1;       /* first governed frame: warm-up */
  app.latedraw.enabled = 1;
  do_wakeup();
  return 0;
}

void stimLateDrawDisable(void)
{
  /* keep the floor and stats for status; just stop governing frames */
  app.latedraw.enabled = 0;
}

void stimLateDrawStatus(latedraw_status_t *st)
{
  const latedraw_state_t *s = &app.latedraw;
  st->enabled = s->enabled;
  st->budget_ms = s->floor_ms;
  st->effective_ms = s->effective_ms;
  st->period_ms = latedrawPeriodMs(s, swapsyncLastRefreshMs());
  st->draws = s->draws;
  st->late = s->late;
  st->warmups = s->warmups;
  st->warmup_late = s->warmup_late;
  st->draw_ms_med = latedrawDrawMedianMs(s);
  st->draw_ms_max = latedrawDrawMaxMs(s);
  st->gpu_ms_med = latedrawGpuMedianMs(s);
  st->gpu_ms_max = latedrawGpuMaxMs(s);
  st->gpu_timed = app.latedraw_gpu_timed;
}

/* The window title, from --title or the default "Stim", changed at run time
   by the Tcl command setWindowTitle (tclproc.c).  A replay labels its
   window with the file and trial so it is never taken for a live display;
   toggleFullscreen reapplies app.title when leaving fullscreen, so the
   stored string is what must change, not just the GLFW title.  Tcl runs
   on the render thread, which is the main thread, where GLFW wants
   window calls. */
void stimSetWindowTitle(const char *title)
{
  char *t = strdup(title ? title : "");
  if (!t) return;
  free((void *) app.title);
  app.title = t;
  if (app.window) glfwSetWindowTitle(app.window, app.title);
}

const char *stimGetWindowTitle(void)
{
  return app.title ? app.title : "";
}

void Application::start_tcp_server(void)
{
  struct sockaddr_in address;
  struct sockaddr client_address;
  socklen_t client_address_len = sizeof(client_address);
  int socket_fd;        // accept socket
  int new_socket_fd;        // client socket
  int on = 1;
  
  //    std::cout << "opening server on port " << std::to_string(tcpport) << std::endl;
  
  /* Initialise IPv4 address. */
  memset(&address, 0, sizeof(struct sockaddr_in));
  address.sin_family = AF_INET;
  address.sin_port = htons(tcpport);
  address.sin_addr.s_addr = INADDR_ANY;
  
  
  /* Create TCP socket. */
  if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    return;
  }
  
  /* Allow this server to reuse the port immediately */
  setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on));
  
  /* Bind address to socket. */
  if (bind(socket_fd, (const struct sockaddr *) &address,
       sizeof (struct sockaddr)) == -1) {
    perror("bind");
    return;
  }
  
  /* Listen on socket. */
  if (listen(socket_fd, 20) == -1) {
    perror("listen");
    return;
  }
  
  while (1) {
    /* Accept connection to client. */
    new_socket_fd = accept(socket_fd, &client_address, &client_address_len);
    if (new_socket_fd == -1) {
      perror("accept");
      continue;
    }
    
    setsockopt(new_socket_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &on, sizeof(on));

    /* The client thread below blocks in read() with no timeout, so a peer that
       vanishes WITHOUT closing -- a rig host rebooting, dserv's machine losing
       power -- would leak the socket and the thread forever. Keepalive is what
       makes that read() eventually fail. See socket_keepalive.h. */
    dserv_set_keepalive(new_socket_fd);
    
    // Create a thread and transfer the new stream to it.
    std::thread thr(tcp_client_process, new_socket_fd, &queue);
    thr.detach();
  }
}

void Application::start_msg_server(void)
{
  struct sockaddr_in address;
  struct sockaddr client_address;
  socklen_t client_address_len = sizeof(client_address);
  int socket_fd;        // accept socket
  int new_socket_fd;        // client socket
  int on = 1;
  
  //    std::cout << "opening server on port " << std::to_string(tcpport) << std::endl;
  
  /* Initialise IPv4 address. */
  memset(&address, 0, sizeof(struct sockaddr_in));
  address.sin_family = AF_INET;
  address.sin_port = htons(messageport);
  address.sin_addr.s_addr = INADDR_ANY;
  
  
  /* Create TCP socket. */
  if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    return;
  }
  
  /* Allow this server to reuse the port immediately */
  setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on));
  
  /* Bind address to socket. */
  if (bind(socket_fd, (const struct sockaddr *) &address,
       sizeof (struct sockaddr)) == -1) {
    perror("bind");
    return;
  }
  
  /* Listen on socket. */
  if (listen(socket_fd, 20) == -1) {
    perror("listen");
    return;
  }
  
  while (1) {
    /* Accept connection to client. */
    new_socket_fd = accept(socket_fd, &client_address, &client_address_len);
    if (new_socket_fd == -1) {
      perror("accept");
      continue;
    }
    
    setsockopt(new_socket_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &on, sizeof(on));

    /* The client thread below blocks in read() with no timeout, so a peer that
       vanishes WITHOUT closing -- a rig host rebooting, dserv's machine losing
       power -- would leak the socket and the thread forever. Keepalive is what
       makes that read() eventually fail. See socket_keepalive.h. */
    dserv_set_keepalive(new_socket_fd);
    
    // Create a thread and transfer the new stream to it.
    std::thread thr(message_client_process, new_socket_fd, &queue);
    thr.detach();
  }
}

void Application::start_dstcp_server(void)
{
  struct sockaddr_in address;
  struct sockaddr client_address;
  socklen_t client_address_len = sizeof(client_address);
  int socket_fd;        // accept socket
  int new_socket_fd;        // client socket
  int on = 1;
  
  //    std::cout << "opening server on port " << std::to_string(tcpport) << std::endl;
  
  /* Initialise IPv4 address. */
  memset(&address, 0, sizeof(struct sockaddr_in));
  address.sin_family = AF_INET;
  address.sin_port = htons(dsport);
  address.sin_addr.s_addr = INADDR_ANY;
  
  
  /* Create TCP socket. */
  if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    return;
  }
  
  /* Allow this server to reuse the port immediately */
  setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on));
  
  /* Bind address to socket. */
  if (bind(socket_fd, (const struct sockaddr *) &address,
       sizeof (struct sockaddr)) == -1) {
    perror("bind");
    return;
  }
  
  /* Listen on socket. */
  if (listen(socket_fd, 20) == -1) {
    perror("listen");
    return;
  }
  
  while (1) {
    /* Accept connection to client. */
    new_socket_fd = accept(socket_fd, &client_address, &client_address_len);
    if (new_socket_fd == -1) {
      perror("accept");
      continue;
    }
    
    setsockopt(new_socket_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &on, sizeof(on));

    /* The client thread below blocks in read() with no timeout, so a peer that
       vanishes WITHOUT closing -- a rig host rebooting, dserv's machine losing
       power -- would leak the socket and the thread forever. Keepalive is what
       makes that read() eventually fail. See socket_keepalive.h. */
    dserv_set_keepalive(new_socket_fd);
    
    // Create a thread and transfer the new stream to it.
    std::thread thr(ds_client_process, new_socket_fd, &ds_queue);
    thr.detach();
  }
}

/*
 * tcp_client_process is CR/LF oriented
 *  incoming messages are terminated by newlines and responses append these
 */
void
Application::tcp_client_process(int sockfd,
                SharedQueue<client_request_t *> *queue)
{
  int rval;
  int wrval;
  char buf[1024];
  
  // each client has its own request structure and reply queue
  SharedQueue<std::string> rqueue;
  client_request_t client_request;
  client_request.rqueue = &rqueue;
  
  std::string script;  
  
  while ((rval = recv(sockfd, buf, sizeof(buf), 0)) > 0) {
    for (int i = 0; i < rval; i++) {
      char c = buf[i];
      if (c == '\n') {
    
    if (script.length() > 0) {
      size_t off = parse_request_prefix(script.data(), script.size(),
                                        &client_request.ack_header,
                                        &client_request.wait_for_swap);
      client_request.script = script.substr(off);
      /* push request onto queue for main thread to retrieve */
      queue->push_back(&client_request);
      
      /* get lock and wake up main thread to process */
      do_wakeup();
      
      /* rqueue will be available after command has been processed */
      std::string s(client_request.rqueue->front());
      client_request.rqueue->pop_front();
      
      //      std::cout << "TCL Result: " << s << std::endl;
      
      // Add a newline, and send the buffer including the null termination
      s = s+"\n";
#ifndef _MSC_VER
      wrval = write(sockfd, s.c_str(), s.size());
#else
      wrval = send(sockfd, s.c_str(), s.size(), 0);
#endif
      if (wrval < 0) {      // couldn't send to client
        break;
      }
      
    }
    script = "";
      }
      else {
    script += c;
      }
    }
  }
  //    std::cout << "Connection closed from " << sock.peer_address() << std::endl;
#ifndef _MSC_VER
  close(sockfd);
#else
  closesocket(sockfd);
#endif
}
  
static  void sendMessage(int socket, const std::string& message) {
  uint32_t msgSize = htonl(message.size()); // Convert size to network byte order
  send(socket, (char *) &msgSize, sizeof(msgSize), 0);
  send(socket, message.c_str(), message.size(), 0);
}

static std::pair<char*, size_t> receiveMessage(int socket) {
    uint32_t msgSize;
    // Receive the size of the message
    int bytesReceived = recv(socket, (char *) &msgSize, sizeof(msgSize), 0);
    if (bytesReceived <= 0) return {nullptr, 0}; // Connection closed or error

    msgSize = ntohl(msgSize);

    // Allocate buffer for the message (+1 for null termination)
    char* buffer = new char[msgSize+1]{};
    size_t totalBytesReceived = 0;
    while (totalBytesReceived < msgSize) {
        bytesReceived = recv(socket,
                 buffer + totalBytesReceived,
                 msgSize - totalBytesReceived, 0);
        if (bytesReceived <= 0) {
            delete[] buffer;
            return {nullptr, 0}; // Connection closed or error
        }
        totalBytesReceived += bytesReceived;
    }

    return {buffer, msgSize};
}

/*
 * message_client_process is frame oriented with 32 size following by bytes
 *  response is similarly organized
 */
void
Application::message_client_process(int sockfd,
                    SharedQueue<client_request_t *> *queue)
{
  int rval;
  int wrval;
  
  // each client has its own request structure and reply queue
  SharedQueue<std::string> rqueue;
  client_request_t client_request;
  client_request.rqueue = &rqueue;
  
  std::string script;  

  while (true) {
    auto [buffer, msgSize] = receiveMessage(sockfd);
    if (buffer == nullptr) break;
    if (msgSize) {
      size_t off = parse_request_prefix(buffer, msgSize,
                                        &client_request.ack_header,
                                        &client_request.wait_for_swap);
      client_request.script = std::string(buffer + off, msgSize - off);

      /* push request onto queue for main thread to retrieve */
      queue->push_back(&client_request);
      
      /* get lock and wake up main thread to process */
      do_wakeup();
      
      /* rqueue will be available after command has been processed */
      std::string s(client_request.rqueue->front());
      client_request.rqueue->pop_front();
      
      // Send a response back to the client
      sendMessage(sockfd, s);
      
      delete[] buffer;
    }
  }
#ifndef _MSC_VER
  close(sockfd);
#else
  closesocket(sockfd);
#endif
}    

  
void
Application::ds_client_process(int sockfd,
                   SharedQueue<ds_client_request_t *> *queue)
{
  int rval;
  char buf[1024];
  double start;
  
  // each client has its own request structure and reply queue
  SharedQueue<std::string> rqueue;
  ds_client_request_t client_request;
  
  std::string dpoint_str;  

#ifndef _MSC_VER
  while ((rval = read(sockfd, buf, sizeof(buf))) > 0) {
#else
    while ((rval = recv(sockfd, buf, sizeof(buf), 0)) > 0) {
#endif
    for (int i = 0; i < rval; i++) {
      char c = buf[i];
      if (c == '\n') {

        //std::cout << "received ds input" << std::endl;

    if (dpoint_str.length() > 0) {
      /* One request PER DATAPOINT, owned by the queue and freed by the
         consumer (processDSCommands).

         This used to reuse a single per-connection struct and push its
         address for every datapoint. Whenever two arrived before the main
         loop drained the queue -- which is what any two datapoints
         published back-to-back produce, whether or not they share a TCP
         segment -- the second assignment overwrote the first, both queued
         pointers then read the same latest string, and the FIRST datapoint
         was silently lost while the second was processed twice.

         It was also a data race: this thread reassigned that std::string
         while the main thread could be reading it.

         Symptom it caused: a publisher writing two datapoints in
         succession would see one of them intermittently never reach the
         stim -- no error anywhere, since dserv's table and the match
         registration were both correct. */
      ds_client_request_t *req = new ds_client_request_t;
      req->datapoint_string = dpoint_str;
      req->rqueue = nullptr;    /* unused on the datapoint path */

      /* push request onto queue for main thread to retrieve */
      queue->push_back(req);

      /* get lock and wake up main thread to process */
      do_wakeup();
    }
    dpoint_str = "";
      }
      else {
    dpoint_str += c;
      }
    }
  }
  //    std::cout << "Connection closed from " << sock.peer_address() << std::endl;
#ifndef _MSC_VER
    close(sockfd);
#else
    closesocket(sockfd);
#endif
}


// Demonstrate creating a simple log window with basic filtering.
void Application::showAppLog(bool* p_open)
{
  ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
  log.Draw("Example: Log", p_open);
}

void Application::toggleImgui(void)
{
    if (Headless) return;       /* no imgui context, and no one to see it */
    if (!show_imgui) {
      show_imgui = true;
      diagResetRefresh();
      glistSetVisible(GList, 1);
      startAnimation();
      OL_DYNAMIC_STORED(OBJList) = 1;
      redraw();
      if (fullscreen)
    showCursor();
    }
    else {
      if (!OL_DYNAMIC_STORED(OBJList))
    redraw();
      show_imgui = false;
      if (fullscreen)
    hideCursor();
    }

}
void Application::processImgui(void)
{
  void ShowAppConsole(bool* p_open);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGuiIO &io = ImGui::GetIO();
  ImVec2 ds = io.DisplaySize;
  float margin = 40.0f;

  ImGui::SetNextWindowPos(ImVec2(margin, margin), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(ds.x - margin * 2, ds.y - margin * 2), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.90f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoSavedSettings;

  if (ImGui::Begin("Stim2 Diagnostics", nullptr, flags)) {
    diagDrawSections();

    ImGui::Separator();
    ImGui::Text("%.1f FPS  |  %d frames", io.Framerate, total_frames);

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen)) {
      float remaining = ImGui::GetContentRegionAvail().y;
      if (remaining < 60.0f) remaining = 60.0f;
      ImGui::BeginChild("logregion", ImVec2(0, remaining), false,
                         ImGuiWindowFlags_HorizontalScrollbar);
      log.DrawEmbedded();
      ImGui::EndChild();
    }
  }
  ImGui::End();

  ImGui::Render();
}


/* Deliver any pending frame tags for the swap that just completed.  Each
   tag becomes a callback invocation:
       $callback payload flipwall_us counter swapcount flip_stimtimef_ms
   built as a Tcl list, so payloads pass through verbatim.  With no
   callback registered the same list is stored in ::swapTagLast. */
void Application::deliverSwapTags(void)
{
  if (swap_tags.empty()) return;
  std::vector<std::string> tags = std::move(swap_tags);
  swap_tags.clear();            /* callbacks may tag the next frame */

  double flip = swapsyncLastFlipTime();
  double flip_ms = flip > 0.0 ? 1000.0 * (flip - StimStart) : -1.0;

  for (auto &payload : tags) {
    Tcl_Obj *items[6];
    int n = 0;
    if (!swap_tag_callback.empty())
      items[n++] = Tcl_NewStringObj(swap_tag_callback.c_str(), -1);
    items[n++] = Tcl_NewStringObj(payload.c_str(), -1);
    items[n++] = Tcl_NewWideIntObj((Tcl_WideInt) swapsyncLastFlipWallUs());
    items[n++] = Tcl_NewWideIntObj((Tcl_WideInt) swapsyncLastCounter());
    items[n++] = Tcl_NewIntObj(SwapCount);
    items[n++] = Tcl_NewDoubleObj(flip_ms);
    Tcl_Obj *cmd = Tcl_NewListObj(n, items);
    Tcl_IncrRefCount(cmd);
    if (!swap_tag_callback.empty()) {
      if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL) != TCL_OK) {
	log.AddLog("[error]: swapTag callback: %s\n",
		   Tcl_GetStringResult(interp));
	Tcl_ResetResult(interp);
      }
    }
    else {
      Tcl_SetVar2Ex(interp, "swapTagLast", NULL, cmd, TCL_GLOBAL_ONLY);
    }
    Tcl_DecrRefCount(cmd);
  }
}

/* Read pending grabs from the back buffer: called after the frame is drawn
   and before the swap (and before any imgui overlay), so the pixels are
   exactly the frame about to be shown.  Rows are stored top-down; alpha is
   forced to 255 unless the grab asked to keep it (the framebuffer's alpha
   is a blending by-product, < 1 wherever anything was blended). */
void Application::captureFrameGrabs(void)
{
  for (auto &g : frame_grabs) {
    if (g.captured) continue;
    int w = g.w > 0 ? g.w : winWidth;
    int h = g.h > 0 ? g.h : winHeight;
    int x = g.x, y = g.y;           /* top-left origin, window pixels */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > winWidth) w = winWidth - x;
    if (y + h > winHeight) h = winHeight - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    std::vector<unsigned char> buf((size_t) w * h * 4);
    if (w && h) {
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadBuffer(drawnBuffer());
      glReadPixels(x, winHeight - y - h, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
		   buf.data());
    }
    size_t rowbytes = (size_t) w * 4;
    g.pixels.resize(buf.size());
    for (int i = 0; i < h; i++)     /* GL rows are bottom-up */
      memcpy(&g.pixels[i * rowbytes], &buf[(size_t) (h - i - 1) * rowbytes],
	     rowbytes);
    if (!g.keep_alpha)
      for (size_t i = 3; i < g.pixels.size(); i += 4) g.pixels[i] = 255;
    g.gw = w; g.gh = h;
    g.captured = true;
  }
}

/* Post-swap: deliver captured grabs as  script w h bytes swapcount  and
   run afterFrames scripts that are due.  While anything is still pending,
   wake the main loop, which then draws another frame (frameWorkPending),
   so a static display keeps swapping: one per vsync, never a busy loop. */
void Application::deliverFrameWork(void)
{
  if (frame_grabs.empty() && frame_afters.empty()) return;

  std::vector<FrameGrab> grabs;
  std::vector<FrameGrab> waiting;
  for (auto &g : frame_grabs) (g.captured ? grabs : waiting).push_back(std::move(g));
  frame_grabs = std::move(waiting);   /* scripts may queue more */

  for (auto &g : grabs) {
    Tcl_Obj *cmd = Tcl_NewStringObj(g.script.c_str(), -1);
    Tcl_IncrRefCount(cmd);
    Tcl_ListObjAppendElement(interp, cmd, Tcl_NewIntObj(g.gw));
    Tcl_ListObjAppendElement(interp, cmd, Tcl_NewIntObj(g.gh));
    Tcl_ListObjAppendElement(interp, cmd,
			     Tcl_NewByteArrayObj(g.pixels.data(),
						 (Tcl_Size) g.pixels.size()));
    Tcl_ListObjAppendElement(interp, cmd, Tcl_NewIntObj(SwapCount));
    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL) != TCL_OK) {
      log.AddLog("[error]: frameGrab: %s\n", Tcl_GetStringResult(interp));
      Tcl_ResetResult(interp);
    }
    Tcl_DecrRefCount(cmd);
  }

  std::vector<FrameAfter> due;
  std::vector<FrameAfter> later;
  for (auto &a : frame_afters) (a.at <= SwapCount ? due : later).push_back(std::move(a));
  frame_afters = std::move(later);    /* scripts may queue more */
  for (auto &a : due) {
    if (Tcl_EvalEx(interp, a.script.c_str(), -1, TCL_EVAL_GLOBAL) != TCL_OK) {
      log.AddLog("[error]: afterFrames: %s\n", Tcl_GetStringResult(interp));
      Tcl_ResetResult(interp);
    }
  }

  if (frameWorkPending()) do_wakeup();
}

/* stim/frame record for the swap just completed (see framelog.h).  Called
   on the render thread right after SwapCount++ in every arm that really
   swapped; ticks_us was read the moment waitForSwap returned.  Costs an
   atomic load when the stream is off. */
static inline void frameLogSwap(long long ticks_us)
{
  if (!frameLogEnabled()) return;
  frameLogPush(SwapCount, swapsyncLastFlipWallUs(), ticks_us,
               swapsyncMissedFrames(), FrameLogCmdsApplied,
               FrameLogDsDispatched);
}

void Application::updateDisplay(bool log_events)
{
  OBJ_GROUP *g = NULL;
  long long frame_ticks_us;
  if (OGL_NGROUPS(GList)) g = OGL_GROUP(GList, OGL_CURGROUP(GList));
  if (!g) return;

  /* Guarantee at least one is bound...*/
  glBindVertexArray(vao);

  /* If the stimuli are not visible, clear, swap, and acknowledge */
  if (!OGL_VISIBLE(GList)) {
    if (ClearBackground) {
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    captureFrameGrabs();
    sinkFrame();                /* like the grabs: before any overlay */
    
    if (!show_imgui)
      stopAnimation();
    else
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());  

    if (log_level && log_events) log.AddLog("[%.3f]: %s\n", glfwGetTime(), "PreSwap");
    presentFrame();
    waitForSwap();
    frame_ticks_us = (long long) (1e6 * stimClockNow());
    if (log_level && log_events) log.AddLog("[%.3f]: %s\n", glfwGetTime(), "PostSwap");
    SwapCount++;
    frameLogSwap(frame_ticks_us);
    deliverSwapTags();
    deliverFrameWork();
    return;
  }

  switch(OG_SWAPMODE(g)) {
  case SWAP_NORMAL:
    /* Here we clear, draw, swap, and wait */
    if (ClearBackground) {
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    
    /*
     * This is to allow any update functions within the current group 
     * to access the timing facilities
     */
    if (OGL_NEWLY_VISIBLE(GList) && OG_START(g) == -1) {
      resetStimTime();
    }

    executePreScripts(g);
    drawGroup(g);
    executePostScripts(g);
    captureFrameGrabs();
    sinkFrame();                /* like the grabs: before any overlay */

    if (show_imgui)
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (log_level && log_events) log.AddLog("[%.3f]: %s\n", glfwGetTime(), "PreSwap");
    /* Swap front and back buffers */
    presentFrame();
    /* Wait for swap */
    waitForSwap();
    frame_ticks_us = (long long) (1e6 * stimClockNow());
    if (log_level && log_events) log.AddLog("[%.3f]: %s\n", glfwGetTime(), "PostSwap");

    SwapCount++;
    frameLogSwap(frame_ticks_us);

    if (OGL_NEWLY_VISIBLE(GList)) {
      OGL_NEWLY_VISIBLE(GList) = 0;
      if (OG_START(g) == -1) {
    if (NextFrameTime != -1) NextFrameTime += StimTime;
    resetStimTime();
    OG_START(g) = 0;
      }
    }

    /* Call any installed post frame command */
    updateTimes();
    deliverSwapTags();
    deliverFrameWork();
    executePostFrameScripts(g);
    executeThisFrameScripts(g);
    glistPostFrameCmd(g);

    /* Kick the animation event if we're dynamic or time based */
    if (OL_DYNAMIC(OBJList) || OG_DYNAMIC(g) == DYNAMIC_TIME_BASED) {
      kickAnimation();
    }
    break;
  case SWAP_ONLY:
    /* Only swap and acknowledge -- no clear, no draw */
    sinkFrame();
    presentFrame();
    SwapCount++;
    waitForSwap();
    frame_ticks_us = (long long) (1e6 * stimClockNow());
    frameLogSwap(frame_ticks_us);

    /* Call any installed post frame command */
    updateTimes();
    deliverSwapTags();
    deliverFrameWork();
    executePostFrameScripts(g);
    executeThisFrameScripts(g);
    glistPostFrameCmd(g);
    
    /* Kick the animation event if we're dynamic or time based */
    if (OL_DYNAMIC(OBJList) || OG_DYNAMIC(g) == DYNAMIC_TIME_BASED) {
      kickAnimation();
    }
    break;
  case NO_SWAP:
    /* Only clear and draw -- no swap, no acknowledge */
    if (ClearBackground) {
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    if (OGL_NEWLY_VISIBLE(GList) && OG_START(g) == -1)
      resetStimTime();
    executePreScripts(g);
    drawGroup(g);
    executePostScripts(g);
    glFlush();

    updateTimes();
    executePostFrameScripts(g);
    executeThisFrameScripts(g);
    glistPostFrameCmd(g);

    if (OGL_NEWLY_VISIBLE(GList)) {
      OGL_NEWLY_VISIBLE(GList) = 0;
      if (OG_START(g) == -1) {
    if (NextFrameTime != -1) NextFrameTime += StimTime;
    resetStimTime();
    OG_START(g) = 0;
      }
    }
    if (OL_DYNAMIC(OBJList) || OG_DYNAMIC(g) == DYNAMIC_TIME_BASED) {
      kickAnimation();
    }
    break;
  }
}

/* Returns 1 if need to redisplay */
int Application::doUpdate(void)
{
  int status;
  OBJ_GROUP *g = NULL;
  int i;

  /* If there are any groups initialized, then set g to the current one */
  if (OGL_NGROUPS(GList)) 
    g = OGL_GROUP(GList, OGL_CURGROUP(GList));
  if (!g) return 0;

  /* If dynamic, update graphics objects in current group and draw again */
  if (OL_DYNAMIC(OBJList)) {
    if (OGL_VISIBLE(GList)) {
      status = glistNextGroupFrame(GList, OGL_CURGROUP(GList));
      if (status == 0 && OG_REPEAT_MODE(g) != G_NORMAL) return 0;
    }

    if (OGL_VISIBLE(GList) && OGL_NGROUPS(GList)) {
      for (i = 0; i < OG_NOBJS(g); i++) {
    GR_OBJ *obj = OL_OBJ(OBJList, OG_OBJID(g, i));
    /* Call object defined update function */
    if (obj && GR_UPDATEFUNCP(obj)) {
      GR_UPDATEFUNC(obj)(obj);
    }
      }
    }
    return 1;
  }

  /* For time-based (not frame-based) update, see if time for next frame */
  else if (OG_DYNAMIC(g) == DYNAMIC_TIME_BASED) {
    int status;
    /* A zero from glistNextTimeFrame means we advanced */
    if (!(status = glistNextTimeFrame(g, StimTime))) 
      return 1;
    else {
      NextFrameTime = status;
    }
  }
  else if (OG_DYNAMIC(g) == DYNAMIC_WAKEUP_BASED) {
    glistNextGroupFrame(GList, OGL_CURGROUP(GList));
    return 1;
  }
  else if (OG_DYNAMIC(g) == DYNAMIC_ALWAYS_UPDATE) {
    return 1;
  }

  else NextFrameTime = -1;
  return 0;
}

static void Reshape(Application *app, int width, int height)
{
  // this helps with High DPI displays, but might need to be adjusted
  //  per platform.  Headless, the display is the offscreen target, whose
  //  size was fixed at startup whatever the (hidden) window reports.
  if (Headless) { width = app->width; height = app->height; }
  else glfwGetFramebufferSize(app->window, &width, &height);
  //  glfwGetWindowSize(app->window, &width, &height);
  app->width = winWidth = width;
  winWidth_2 = width/2;
  app->height = winHeight = height;

  defaultProjection();

  glViewport(0, 0, winWidth, winHeight);
  PIX_PER_DEG_X = (ScreenWidth/2)/HALF_SCREEN_DEG_X;
  PIX_PER_DEG_Y = (ScreenHeight/2)/HALF_SCREEN_DEG_Y;
}

void add_shutdown_func(SHUTDOWN_FUNC func, void *clientdata)
{
  app.shutdown_cmds.add(func, clientdata);
}

void doToggleImgui(void)
{
  app.toggleImgui();
}

int isImguiVisible(void)
{
  return app.show_imgui ? 1 : 0;
}

void kill_window(void)
{
  return;
}

// evaluate Tcl command from main thread
int evalTclCommand(const char *command, char **resultPtr)
{
  int retcode;
  retcode = Tcl_Eval(OurInterp, command);
  if (resultPtr)
    *resultPtr = (char *) Tcl_GetStringResult(OurInterp);
  return retcode;
}

char *sendTclCommand(char *command)
{
  int retcode;
  retcode = Tcl_Eval(OurInterp, command);
  if (retcode == TCL_ERROR) {
    std::cerr << command << ":\n" <<
      Tcl_GetStringResult(OurInterp) << std::endl;
  }
  return NULL;
}

void sendDispMsg(int msg) {
  MessageQueue.push_back(msg);
}

void window_size_callback(GLFWwindow *window, int width, int height)
{
  //  std::cout << "window_size_callback" << std::endl;
  
  Application *app = (Application *) glfwGetWindowUserPointer(window);


  Reshape(app, width, height);
  //  redraw();

  glfwGetWindowPos(window, &app->xpos, &app->ypos);
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
  //  std::cout << "framebuffer_size_callback" << std::endl;
  Application *app = (Application *) glfwGetWindowUserPointer(window);
  Reshape(app, width, height);
  redraw();
}

void window_pos_callback(GLFWwindow *window, int x, int y)
{
  //  std::cout << "window_pos_callback" << std::endl;
  Application *app = (Application *) glfwGetWindowUserPointer(window);
  app->xpos = x;
  app->ypos = y;
}

void window_refresh_callback(GLFWwindow *window)
{
  int xpos, ypos;
  glfwGetWindowPos(window, &xpos, &ypos);
  //  std::cout << "refresh_callback" << xpos << "," << ypos << std::endl;

 redraw();
}


void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
  static double x, y;
  if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS)
    glfwSetWindowShouldClose (window, 1);

  if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
    app.toggleImgui();

  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    if (action == GLFW_PRESS) {
      const char *proc = "onMousePress";
      if (!Tcl_FindCommand(OurInterp, proc, NULL, 0)) return;
      glfwGetCursorPos(window, &x, &y);
      MouseXPos = x;  MouseYPos = y;
      sendTclCommand((char *) proc);
    }
    if (action == GLFW_RELEASE) {
      const char *proc = "onMouseRelease";
      if (!Tcl_FindCommand(OurInterp, proc, NULL, 0)) return;
      glfwGetCursorPos(window, &x, &y);
      MouseXPos = x;  MouseYPos = y;
      sendTclCommand((char *) proc);
    }
  }

}

/*
 * Cursor motion -> the Tcl onMouseMove hook: `onMouseMove x y`, only if a
 * script has defined the proc (one Tcl_FindCommand per motion event
 * otherwise). Dispatched inline like the press/release handlers above --
 * tclproc.c's sendTclMouseMoveCommand is inside an #if 0 block along with
 * the rest of that dead dispatcher family. Same window-coordinate space
 * as glfwGetCursorPos, so a script can pair the three. This is what lets
 * the stim window's mouse stand in for a touchscreen's DRAG events
 * (ess-2.0.tm's configure_stim bridge) on a machine with no input module.
 */
void cursor_pos_callback(GLFWwindow *window, double x, double y)
{
  static char cmd[64];
  MouseXPos = (int) x;  MouseYPos = (int) y;
  if (!Tcl_FindCommand(OurInterp, "onMouseMove", NULL, 0)) return;
  snprintf(cmd, sizeof(cmd), "onMouseMove %d %d", (int) x, (int) y);
  sendTclCommand(cmd);
}

/*
 * getMouseWorld
 *
 * Tcl command. Returns the current cursor position mapped to world
 * coordinates (degrees of visual angle) as "wx wy". Uses
 * glfwGetWindowSize rather than the framebuffer size so the mapping is
 * correct under framebuffer/window scaling (e.g. Retina): glfwGetCursorPos
 * and glfwGetWindowSize share the same coordinate space, while winWidth/
 * winHeight track the framebuffer and would be 2x off on a Retina display.
 */
static int getMouseWorldCmd(ClientData clientData, Tcl_Interp *interp,
                            int argc, char *argv[])
{
  double cx, cy;
  int ww, wh;
  glfwGetCursorPos(app.window, &cx, &cy);
  glfwGetWindowSize(app.window, &ww, &wh);
  if (ww <= 0 || wh <= 0) {
    Tcl_SetResult(interp, (char *) "0.0 0.0", TCL_STATIC);
    return TCL_OK;
  }
  double wx =  (cx / (double) ww - 0.5) * 2.0 * HALF_SCREEN_DEG_X;
  double wy = -(cy / (double) wh - 0.5) * 2.0 * HALF_SCREEN_DEG_Y;
  char buf[64];
  snprintf(buf, sizeof(buf), "%f %f", wx, wy);
  Tcl_SetResult(interp, buf, TCL_VOLATILE);
  return TCL_OK;
}

void toggleFullscreen(GLFWwindow *window)
{
  Application *app = (Application *) glfwGetWindowUserPointer(window);
  if (app->fullscreen) {
    glfwSetWindowMonitor(window, NULL, app->xpos, app->ypos,
             app->width, app->height, 0);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetWindowTitle(window, app->title);
  }
  else {
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primary);
    glfwSetWindowMonitor(window, primary, 0, 0,
             mode->width, mode->height, RefreshRate);
    FrameDuration = 1000./RefreshRate;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    Reshape(app, mode->width, mode->height);
  }
  app->fullscreen = 1-app->fullscreen;
}

void key_callback(GLFWwindow *window, int key,
               int scancode, int action, int mods)
{
  Application *app = (Application *) glfwGetWindowUserPointer(window);

  // if the console is open, don't process these keys
  if (app->show_console) return;
  
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    glfwSetWindowShouldClose (window, 1);
  }
  else if (key == GLFW_KEY_V && action == GLFW_PRESS) {
    if (OGL_VISIBLE(GList)) glistSetVisible(GList, 0);
    else glistSetVisible(GList, 1);
    redraw();
  }
  else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
      toggleAnimation();
  }
  else if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) {
    app->toggleImgui();
  }
  else if (key == GLFW_KEY_F && action == GLFW_PRESS) {
    toggleFullscreen(window);
  }
  if (action == GLFW_PRESS || action == GLFW_RELEASE) {
    char procname[64];
    const char *suffix = (action == GLFW_RELEASE) ? "Release" : "";
    switch(key) {
    case GLFW_KEY_UP:
      snprintf(procname, 63, "onUpArrow%s", suffix);
      if (!Tcl_FindCommand(OurInterp, procname, NULL, 0)) return;
      break;
    case GLFW_KEY_DOWN:
      snprintf(procname, 63, "onDownArrow%s", suffix);
      if (!Tcl_FindCommand(OurInterp, procname, NULL, 0)) return;
      break;
    case GLFW_KEY_LEFT:
      snprintf(procname, 63, "onLeftArrow%s", suffix);
      if (!Tcl_FindCommand(OurInterp, procname, NULL, 0)) return;
      break;
    case GLFW_KEY_RIGHT:
      snprintf(procname, 63, "onRightArrow%s", suffix);
      if (!Tcl_FindCommand(OurInterp, procname, NULL, 0)) return;
      break;
    default: {
      const char *cmd = (action == GLFW_RELEASE) ? "onKeyRelease" : "onKeyPress";
      if (!Tcl_FindCommand(OurInterp, cmd, NULL, 0)) return;
      snprintf(procname, 63, "%s %d", cmd, key);  break;
    }
    }
    if (procname[0]) {
      sendTclCommand(procname);
    }
  }
}

void resetGraphicsState(void)
{
  return;
}

void addLog(char *format, ...)
{
  va_list arglist;
  va_start(arglist, format);
  app.log.AddLog(format, arglist);
  va_end(arglist);
}

void logMessage(char *message)
{
  app.log.AddLog("%s\n", message);
}

int setVerboseLevel(int level)
{
  int old = app.log_level;
  app.log_level = level;
  return old;
}


#ifdef EMBED_PYTHON
void execPythonCmd(char *cmd)
{
  try {
    py::exec(cmd);
  }
  catch (const std::exception& e) { 
   std::cout << "python error: " << e.what() << std::endl;
  }
}

static void stim_setBackground(float r, float g, float b)
{
  setBackgroundColorVals(r/255.0, g/255.0, b/255.0, 1.0);
  sendDispMsg(SET_BACKGROUND);
}

PYBIND11_EMBEDDED_MODULE(stim, m) {

  m.def("setBackground", &stim_setBackground, "r"_a, "g"_a, "b"_a,
    R"pbdoc(
        set background color
    )pbdoc");
}

#endif

// On windows, make this a windows, not console, app
#ifdef _MSC_VER
#ifdef HIDE_CONSOLE
# pragma comment(linker, "/subsystem:windows /ENTRY:mainCRTStartup")
#endif
#endif


int
main(int argc, char *argv[]) {
  int frame = 0;

  bool verbose = false;
  bool fullscreen = false;
  bool help = false;
  bool borderless = false;
  bool headless = false;
  int width = 580, height = 340;
  int xpos = 30, ypos = 30;
  float refresh = 60;
  int interval = 2;
  int port_base = 4610;
  const char *startup_file = NULL;
  bool updated_display = false;

  char resolved_exe[PATH_MAX];
  char *abs_path = realpath(argv[0], resolved_exe);
  if (abs_path) {
      argv[0] = resolved_exe;
  }

#ifdef _MSC_VER
  // Set DPI awareness
  SetProcessDPIAware();
  //  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
  
  // Use this GPIO pin for swap acknowledge on Jetson Hardware
  int gpio_output_pin = 13;
  
  std::string title_opt = "Stim";

  cxxopts::Options options("stim2","multiplatform OpenGL presentation program");

  options.add_options()
    ("v,verbose", "Verbose mode", cxxopts::value<bool>(verbose))
    ("title", "Window title (also settable at run time with setWindowTitle)",
     cxxopts::value<std::string>(title_opt)->default_value("Stim"))
    ("b,borderless", "Borderless", cxxopts::value<bool>(borderless))
    ("w,width", "width", cxxopts::value<int>(width))
    ("h,height", "height", cxxopts::value<int>(height))
    ("x,xpos", "x_position", cxxopts::value<int>(xpos))
    ("y,ypos", "y_position", cxxopts::value<int>(ypos))
    ("r,refresh", "refresh_rate", cxxopts::value<float>(refresh))
    ("t,timer", "timer interval", cxxopts::value<int>(interval))
    ("F,fullscreen", "Fullscreen mode", cxxopts::value<bool>(fullscreen))
    ("headless", "No window: draw into an offscreen -w x -h framebuffer "
     "(Linux: no display server, via EGL); see stepMode and frameSink",
     cxxopts::value<bool>(headless))
    ("f,file", "File name", cxxopts::value<std::string>())
    ("port-base", "First of the four listener ports: command (N), "
     "dataserver (N+1), message (N+2), websocket (N+3)",
     cxxopts::value<int>(port_base)->default_value("4610"))
    ("help", "Print help", cxxopts::value<bool>(help))
    ;

  try {
    auto result = options.parse(argc, argv);

    if (result.count("file")) {
      startup_file = strdup((result["file"].as<std::string>()).c_str());
    }
  }
  catch (const std::exception& e) {
    std::cout << "error parsing options: " << e.what() << std::endl;
    exit(1);
  }    

  if (help) {
    std::cout << options.help({"", "Group"}) << std::endl;
    exit(0);
  }

  /* Headless on Linux: GLFW's null platform, whose EGL context is backed
     by Mesa's surfaceless platform -- a GPU render node (or llvmpipe), no
     X or Wayland.  Elsewhere the native platform with a hidden window
     supplies the context (macOS has no EGL); either way the drawing goes
     to an offscreen framebuffer (initHeadlessTarget). */
  Headless = headless;
#if defined(GLFW_PLATFORM_NULL) && !defined(__APPLE__) && !defined(_WIN32)
  if (headless) glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
#endif
  if (! glfwInit ()) {
    std::cout << "ERROR: could not start GLFW3" << std::endl;
    return 0;
  }

  /* All four listeners move together so a second stim2 (a test build,
     say) can run beside one on the default ports.  Set before any server
     or the websocket console starts. */
  if (port_base < 1024 || port_base + 3 > 65535) {
    std::cout << "error: --port-base must be between 1024 and 65532 (got "
              << port_base << ")" << std::endl;
    exit(1);
  }
  app.tcpport = port_base;
  app.dsport = port_base + 1;
  app.messageport = port_base + 2;
  app.websocket_port = port_base + 3;

#ifdef _WIN32
  glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else

#ifndef STIM2_USE_GLES
  glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else 
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);  
#endif

  glfwWindowHint (GLFW_REFRESH_RATE, refresh);
  

  /*
  glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 1);  
  */
#endif

  glfwWindowHint (GLFW_SAMPLES, 0);

  if (headless) {
    fullscreen = false;
    glfwWindowHint(GLFW_VISIBLE, GL_FALSE);
#if defined(GLFW_PLATFORM_NULL) && !defined(__APPLE__) && !defined(_WIN32)
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
#endif
    if (title_opt == "Stim") title_opt = "Stim (headless)";
  }
  else if (borderless) {
    glfwWindowHint( GLFW_VISIBLE, GL_FALSE );
    glfwWindowHint( GLFW_DECORATED, GL_FALSE );
  }
  
  /* strdup'd so setWindowTitle can free and replace it later */
  app.title = strdup(title_opt.c_str());

  GLFWmonitor* primary = glfwGetPrimaryMonitor();
  const GLFWvidmode* mode = glfwGetVideoMode(primary);
  
  if (!fullscreen) {
    winWidth = app.width = width;
    winHeight = app.height = height;
    app.window = glfwCreateWindow(app.width, app.height,
                  app.title, NULL, NULL);
  }
  else {
    winWidth = app.width = mode->width;
    winHeight = app.height = mode->height;
    app.window = glfwCreateWindow(mode->width, mode->height,
                  app.title, primary, NULL);
  }
  
  if (!app.window) {
    const char *desc = NULL;
    glfwGetError(&desc);
    std::cerr << "Error opening window" << (desc ? ": " : "") << (desc ? desc : "")
              << std::endl;
    if (headless)
      std::cerr << "  (headless: needs EGL with EGL_MESA_platform_surfaceless and access "
                   "to a render node, /dev/dri/renderD*, or Mesa's llvmpipe)" << std::endl;
    exit(1);
  }

  if (borderless && !fullscreen && !headless) {
    glfwShowWindow(app.window);
  }

  int width_mm, height_mm;
  glfwGetMonitorPhysicalSize(primary, &width_mm, &height_mm);

  ScreenWidthCm = width_mm / 10.0f;
  ScreenHeightCm = height_mm / 10.0f;  

  ScreenWidth = mode->width;
  ScreenHeight = mode->height;
  RefreshRate = mode->refreshRate;
  /* Headless, the "screen" is the offscreen target and its refresh is
     whatever -r says (60 by default): it paces the frames when not
     stepped, and FrameDuration is what scripts read. */
  if (headless) {
    ScreenWidth = width;
    ScreenHeight = height;
    RefreshRate = (int) (refresh + 0.5f);
    /* screen_config derives degrees from the physical size; keep the
       pixels square, whatever monitor (or none) the machine has */
    if (ScreenWidthCm <= 0.0f) ScreenWidthCm = 50.0f;
    ScreenHeightCm = ScreenWidthCm * height / (float) width;
  }
  FrameDuration = 1000./RefreshRate;

  app.xpos = xpos;
  app.ypos = ypos;
  glfwSetWindowPos(app.window, app.xpos, app.ypos);

  glfwSetWindowUserPointer(app.window, &app);
  glfwMakeContextCurrent( app.window );
  if (!fullscreen) glfwSetWindowSize(app.window, width, height);

  glfwSetWindowSizeCallback(app.window, window_size_callback);
  glfwSetWindowRefreshCallback(app.window, window_refresh_callback);
  glfwSetKeyCallback(app.window, key_callback);
  glfwSetMouseButtonCallback(app.window, mouse_button_callback);
  glfwSetCursorPosCallback(app.window, cursor_pos_callback);
  glfwSetWindowPosCallback(app.window, window_pos_callback);

  app.verbose = verbose;
  
  if (app.verbose) {
    std::cout << "Video Mode: ";
    std::cout << mode->width << "x" << mode->height;
    std::cout << "@" << mode->refreshRate << "Hz " << std::endl;
  }
  
  if (!gladLoadGL()) {
    std::cout << "failed to intialize OpenGL functions via glad" << std::endl;
    return 0;
  }

  if (app.verbose) {
    // get version info
    const GLubyte* renderer = glGetString (GL_RENDERER);
    const GLubyte* version = glGetString (GL_VERSION); 
    std::cout << " Renderer: " << renderer << std::endl;
    std::cout << " OpenGL version supported: " <<  version << std::endl;
  }

  // Ensure tightly packed pixel data for all texture uploads
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);

  if (headless) {
    winWidth = app.width = width;
    winHeight = app.height = height;
    if (!app.initHeadlessTarget(width, height)) {
      std::cerr << "headless: could not create a " << width << "x" << height
                << " offscreen framebuffer" << std::endl;
      exit(1);
    }
    std::cerr << "[info] headless " << width << "x" << height << " @ "
              << RefreshRate << " Hz, renderer: " << glGetString(GL_RENDERER)
              << std::endl;
  }
  
  app.init();
  if (headless) XScale = YScale = 1.0f;   /* no desktop scaling offscreen */
  else app.init_imgui();

  app.init_gpio(gpio_output_pin);

  if (fullscreen) {
    glfwSetWindowMonitor(app.window, primary, 0, 0,
             mode->width, mode->height, RefreshRate);
    glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    winWidth = mode->width;
    winHeight = mode->height;
    Reshape(&app, mode->width, mode->height);
    app.fullscreen = 1;
  }
  else {
    Reshape(&app, winWidth, winHeight);
  }

  // Choose how waitForSwap() blocks (and timestamps) on this platform;
  // headless there is no flip to wait for (headlessPace stands in)
  if (!headless) swapsyncInit(app.window, RefreshRate, verbose);

  // Initialize WebSocket server
  app.init_websocket();

  app.setupTcl(argv[0], argc, argv);

#ifdef __APPLE__
  const char *cfg_path = "/Applications/stim2.app/Contents/Resources/stim2.cfg";
  int res = access(cfg_path, R_OK);
  if (res >= 0) {
    app.sourceFile(cfg_path);
  }
#endif
  
  
  if (startup_file) {
    if (app.sourceFile(startup_file) != TCL_OK) {
      std::cerr << Tcl_GetStringResult(OurInterp) << std::endl;
    }
  }

#ifdef EMBED_PYTHON
  py::scoped_interpreter guard{}; // start the interpreter and keep it alive
  if (verbose) py::print("Python initialized");
  py::exec("import stim");
#endif

  app.timer_interval = interval;
  app.startTimer();

#ifdef _MSC_VER
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return -1;
    }
#endif
  
  /* Detach at birth, not at the bottom of main.
   *
   * These are while(1) accept() loops with no exit condition, so they can
   * never be joined and holding them joinable buys nothing.  It cost
   * something, though: `app` is a global, and the Tcl "exit"/"quit" command
   * calls exit(0) from inside Tcl_DoOneEvent, which never reaches the bottom
   * of main.  Static destruction then ran ~Application() on three STILL
   * JOINABLE std::threads, and destroying a joinable thread calls
   * std::terminate -- so every scripted exit aborted with a crash dialog,
   * while closing the window quit cleanly.  Detaching here makes the
   * invariant hold on every exit path rather than on one of them.
   */
  app.net_thread = std::thread(&Application::start_tcp_server, &app);
  app.net_thread.detach();
  app.ds_net_thread = std::thread(&Application::start_dstcp_server, &app);
  app.ds_net_thread.detach();
  app.msg_thread = std::thread(&Application::start_msg_server, &app);
  app.msg_thread.detach();

  redraw();

  /* Late-draw: how the NEXT iteration waits.  -1 = block for a wake-up as
     always; N > 0 = block at most N ms (the time left to the draw target);
     0 = the target is under 1 ms away, don't block at all. */
  int late_wait_ms = -1;

  while (!glfwWindowShouldClose(app.window)) {
    if (late_wait_ms < 0) app.wait_for_wakeup();
    else if (late_wait_ms > 0) app.wait_for_wakeup_ms(late_wait_ms);
    late_wait_ms = -1;

    glfwPollEvents();
    
    app.processTclCommands();
    app.processDSCommands();
    app.processWebSocketCommands(); 
    app.processTimerFuncs();

    if (app.show_imgui)
      app.processImgui();

    updated_display = app.processMessages(app.wait_for_swap);
    animEventPending = 0;

    /* Draw-late: with the scheduler on and the display in continuous
       dynamic mode, the next frame's step+draw waits until
       lastFlip + period - budget; until then this pass skips the draw and
       the loop keeps answering commands and datapoints.  Off, not dynamic,
       or with no flip timestamps, lateDrawDefer() is false and this is the
       loop as it always was. */
    Application::LateDrawFrame ldf;
    bool deferred = false;
    if (!updated_display && !StepMode && app.lateDrawDefer(&ldf, &late_wait_ms)) {
      deferred = true;          /* nothing to draw this pass */
    }
    else if (!updated_display && !StepMode &&
             (app.doUpdate() || app.frameWorkPending())) {
      app.updateDisplay(app.wait_for_swap);
      app.count_frame();
      app.lateDrawAccount(ldf);
    }

    if (updated_display) {
        app.count_frame();
    }

    /* A '!' command dequeued during a deferral pass keeps its promise of
       "ack after the swap": its reply stays queued until the pass that
       draws.  Without deferral this is the unconditional call it was. */
    if (!deferred) app.processReplies();

    // track status, FPS, and frame count
    static unsigned int last_ws_update = 0;
    if (StimTime - last_ws_update >= 1000) {
        const char* state = app.isSleeping() ? "sleeping" : "running";
        app.update_websocket_status(app.current_fps, app.total_frames, StimTime / 1000, state);
        
        if (app.ws_server) {
            app.ws_server->flush_messages();
        }
        
        last_ws_update = StimTime;
    }

  }
  
  /* a sink still open gets its end of file (ffmpeg finishes the movie) */
  { int st; stimFrameSinkClose(&st); }

  /* free allocated objects */
  setDynamicUpdate(0);
  objListReset(curobjlistp);
  
  // Cleanup WebSocket
  app.shutdown_websocket();

  /* shutdown timer thread */
  app.stopTimer();

  /* TCP/IP threads were detached at creation -- nothing to do here */

#ifdef _MSC_VER
    WSACleanup();
#endif
  
  glfwTerminate();
  
  return 0;
}

