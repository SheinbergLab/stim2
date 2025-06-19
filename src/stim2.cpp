#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <queue>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#if defined(__APPLE__)
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

#include "timer.h"      // for periodic timer thread

#include <cxxopts.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

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
#include "rawapi.h"

#ifdef EMBED_PYTHON
#include <pybind11/embed.h> // everything needed for embedding
namespace py = pybind11;
using namespace pybind11::literals;
#endif

static int MainWin = 0;

static void do_wakeup(void);

static int MessageLoop(void);
static void drawGroup(OBJ_GROUP *g);
static void execTimerFuncs(OBJ_GROUP *g);

static void free_resources(void);

OBJ_LIST *OBJList = NULL;  /* main entry point to object stimuli  */
OBJ_LIST curobjlist;
OBJ_LIST *curobjlistp = &curobjlist;

float BackgroundColor[]      = { 0.0f,  0.0f, 0.0f, 1.0f };
float GreenBackgroundColor[] = { 0.0f,  .8f, .2f };
float RedBackgroundColor[]   = { .85f, .0f, .1f };
static float PIX_PER_DEG_X, PIX_PER_DEG_Y, 
  HALF_SCREEN_DEG_X, HALF_SCREEN_DEG_Y, HALF_SCREEN_DEG_Z = 1000.;
static int ScreenWidth;
static int ScreenWidth_2;
static int ScreenHeight;
static int RefreshRate;
static float FrameDuration;
int     winWidth = 640;
static int      winWidth_2;
int     winHeight = 480;
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

int NextFrameTime = -1;

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
  glReadBuffer(GL_FRONT);
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glReadBuffer(GL_BACK);
  
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

  glReadBuffer(GL_FRONT);
  glReadPixels(0, 0, winWidth, winHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glReadBuffer(GL_BACK);

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
  StimStart = glfwGetTime();
  StimTime = 0;
}

void updateTimes(void)
{
  double curtime = glfwGetTime();
  StimTicks = (int) (1000*curtime);
  StimTime = (int) (1000*(curtime-StimStart));
}


void executePostFrameScripts(OBJ_GROUP *g)
{
  GR_OBJ *o;
  int i;

  for (i = 0; i < OG_NOBJS(g); i++)  {
    o = OL_OBJ(OBJList, OG_OBJID(g, i));
    if (o) {
      executeScripts(GR_POSTFRAME_SCRIPTS(o),
             GR_POSTFRAME_SCRIPT_ACTIVES(o),
             GR_N_POSTFRAME_SCRIPTS(o));
    }
  }
}

/*
 * These are oneshot scripts
 * Ensure that they can be re-armed 
 */
void executeThisFrameScripts(OBJ_GROUP *g)
{
  GR_OBJ *o;
  int i, j, n;
  
  char *thisframe_scripts[MAXSCRIPTS];

  for (i = 0; i < OG_NOBJS(g); i++)  {
    o = OL_OBJ(OBJList, OG_OBJID(g, i));
    if (o) {
      for (j = 0, n = 0; j < GR_N_THISFRAME_SCRIPTS(o); j++) {
    thisframe_scripts[n++] = GR_THISFRAME_SCRIPT(o, j);
      }
      GR_N_THISFRAME_SCRIPTS(o) = 0;
      for (j = 0; j < n; j++) {
    sendTclCommand(thisframe_scripts[j]);
    free(thisframe_scripts[j]);
      }
    }
  }
}

void executeScripts(char **scripts, int *actives, int n)
{
  int i;
  for (i = 0; i < n; i++) {
    if (actives[i] && scripts[i]) sendTclCommand(scripts[i]);
  }
}

void drawObject(GR_OBJ *o)
{
  executeScripts(GR_PRE_SCRIPTS(o),
         GR_PRE_SCRIPT_ACTIVES(o),
         GR_N_PRE_SCRIPTS(o));
  setModelViewMatrix(o);
  drawObj(o);
  executeScripts(GR_POST_SCRIPTS(o),
         GR_POST_SCRIPT_ACTIVES(o),
         GR_N_POST_SCRIPTS(o));
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

static void drawGroup(OBJ_GROUP *g) 
{
  GR_OBJ *o;
  int i;

  /*
   * Stash the current ObjList away it doesn't get changed
   * during this update 
   */
  memcpy(curobjlistp, OBJList, sizeof(OBJ_LIST));

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

  if (g && OGL_VISIBLE(GList)) {
    switch (StereoMode) {
    case 0:
      currentEye = 0;
      for (i = 0; i < OG_NOBJS(g); i++)  {
    o = OL_OBJ(OBJList, OG_OBJID(g, i));
    if (o && GR_VISIBLE(o)) drawObject(o);
    else if (o) {
      executeScripts(GR_PRE_SCRIPTS(o), GR_PRE_SCRIPT_ACTIVES(o),
             GR_N_PRE_SCRIPTS(o));
      executeScripts(GR_POST_SCRIPTS(o), GR_POST_SCRIPT_ACTIVES(o),
             GR_N_POST_SCRIPTS(o));
    }
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
      else if (o) {
        executeScripts(GR_PRE_SCRIPTS(o), GR_PRE_SCRIPT_ACTIVES(o),
               GR_N_PRE_SCRIPTS(o));
        executeScripts(GR_POST_SCRIPTS(o), GR_POST_SCRIPT_ACTIVES(o),
               GR_N_POST_SCRIPTS(o));
      }
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
      else if (o) {
        executeScripts(GR_PRE_SCRIPTS(o), GR_PRE_SCRIPT_ACTIVES(o),
               GR_N_PRE_SCRIPTS(o));
        executeScripts(GR_POST_SCRIPTS(o), GR_POST_SCRIPT_ACTIVES(o),
               GR_N_POST_SCRIPTS(o));
      }
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
      else if (o) {
        executeScripts(GR_PRE_SCRIPTS(o), GR_PRE_SCRIPT_ACTIVES(o),
               GR_N_PRE_SCRIPTS(o));
        executeScripts(GR_POST_SCRIPTS(o), GR_POST_SCRIPT_ACTIVES(o),
               GR_N_POST_SCRIPTS(o));
      }
    }
      }
      if (OG_RIGHT_EYE(g)) {
    glDrawBuffer(GL_BACK_RIGHT);
    currentEye = 1;
    for (i = 0; i < OG_NOBJS(g); i++)  {
      o = OL_OBJ(OBJList, OG_OBJID(g, i));
      if (o && GR_VISIBLE(o) && GR_RIGHT_EYE(o)) drawObject(o);
      else if (o) {
        executeScripts(GR_PRE_SCRIPTS(o), GR_PRE_SCRIPT_ACTIVES(o),
               GR_N_PRE_SCRIPTS(o));
        executeScripts(GR_POST_SCRIPTS(o), GR_POST_SCRIPT_ACTIVES(o),
               GR_N_POST_SCRIPTS(o));
      }
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


template <typename T>
class SharedQueue
{
public:
  SharedQueue();
  ~SharedQueue();
  
  T& front();
  void pop_front();
  
  void push_back(const T& item);
  void push_back(T&& item);
  
  int size();
  bool empty();
  
private:
  std::deque<T> queue_;
  std::mutex mutex_;
  std::condition_variable cond_;
}; 

template <typename T>
SharedQueue<T>::SharedQueue(){}

template <typename T>
SharedQueue<T>::~SharedQueue(){}

template <typename T>
T& SharedQueue<T>::front()
{
  std::unique_lock<std::mutex> mlock(mutex_);
  while (queue_.empty())
    {
      cond_.wait(mlock);
    }
  mlock.unlock();     // unlock before notificiation to minimize mutex con
  return queue_.front();
}

template <typename T>
void SharedQueue<T>::pop_front()
{
  std::unique_lock<std::mutex> mlock(mutex_);
  while (queue_.empty())
    {
      cond_.wait(mlock);
    }
  mlock.unlock();     // unlock before notificiation to minimize mutex con
  queue_.pop_front();
}     

template <typename T>
void SharedQueue<T>::push_back(const T& item)
{
  std::unique_lock<std::mutex> mlock(mutex_);
  queue_.push_back(item);
  mlock.unlock();     // unlock before notificiation to minimize mutex con
  cond_.notify_one(); // notify one waiting thread
  
}

template <typename T>
void SharedQueue<T>::push_back(T&& item)
{
  std::unique_lock<std::mutex> mlock(mutex_);
  queue_.push_back(std::move(item));
  mlock.unlock();     // unlock before notificiation to minimize mutex con
  cond_.notify_one(); // notify one waiting thread  
}

template <typename T>
int SharedQueue<T>::size()
{
  std::unique_lock<std::mutex> mlock(mutex_);
  int size = queue_.size();
  mlock.unlock();
  return size;
}

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
} client_request_t;

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
};

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
  
  GLsync swap_sync;
  
  // for GPIO swap acknowledge
  int output_pin;
  
public:

  void wakeup(void) { wake_queue.push_back(0); }

  void wait_for_wakeup(void) {
    int req = wake_queue.front();
    wake_queue.pop_front();
    while (wake_queue.size()) {
      wake_queue.pop_front();
    }
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

  void start_tcp_server(void);	   // crlf oriented commands
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
  
  const char *title;
  int tcpport = 4610;
  int dsport = 4611;
  int messageport = 4612;

  ShutdownCmds shutdown_cmds;
  
  Application()
  {
#if defined(__APPLE__)
    // Set up sleep/wake callbacks
    sleepWakeHandler.setSleepCallback([this]() {
      onSystemSleep();
    });
    
    sleepWakeHandler.setWakeCallback([this]() {
      onSystemWake();
    });
    
    sleepWakeHandler.startMonitoring();
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
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);

#ifdef STIM2_USE_GLES
    const char* glsl_version = "#version 300 es";
#else
    const char* glsl_version = "#version 150";
#endif
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);
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

  void waitForSwap()
  {
    // Graphics drivers on different hardware will wait for the buffer
    // swap in different ways.  On the Jetson, using glfw3, the
    //   glfwSwapBuffers() will block, so we don't need to do anything
#ifdef JETSON_XAVIER
    // use a Sync Object to wait for the buffer swap
    // https://www.khronos.org/opengl/wiki/Sync_Object
    swap_sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glClientWaitSync(swap_sync, GL_SYNC_FLUSH_COMMANDS_BIT, 10000000);
#else
    glFinish();
#endif
  }

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
      }
      do_wakeup();
    });
  }
public:

  void startTimer() {
    if (!systemIsSleeping) {
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
  }
  
  void onSystemWake() {
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

    Tcl_VarEval(interp, 
        "proc load_local_packages {} {\n"
        " global auto_path\n"
        " set f [file dirname [info nameofexecutable]]\n"
        " if [file exists [file join $f dlsh.zip]] { set dlshzip [file join $f dlsh.zip] } {"
#ifdef _WIN32
        "   set dlshzip c:/usr/local/dlsh/dlsh.zip }\n"
#else
        "   set dlshzip /usr/local/dlsh/dlsh.zip }\n"
#endif
        " set dlshroot [file join [zipfs root] dlsh]\n"
        " zipfs unmount $dlshroot\n"
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

    /* New proc for loading dlls that looks in proper folders */
    Tcl_VarEval(interp, 
        "proc load_modules { args } {"
        " set f [file dirname [file dirname [info nameofexecutable]]]\n"
        " if { $::tcl_platform(os) == \"Darwin\" } {"
        "  foreach m $args { load $f/stimdlls/build_macos/$m.dylib }"
        " } elseif { $::tcl_platform(os) == \"Linux\" } {"
        "  foreach m $args { load $f/stimdlls/build_linux/$m.so }"
        " } else {"
        "  if  { $::tcl_platform(machine) == \"amd64\" } {"
        "   foreach m $args {load $f/stimdlls/build_win64/${m}.dll $m }"
        "  } else {"
        "    foreach m $args { load $f/stimdlls/build_win32/$m.dll }"
        "  }"
        " }"
        "}\n"
        "proc load_module { m } { return [load_modules $m] }",
        NULL);
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

      // std::cout << "processing tcl request: " << req->script << std::endl;
      
      if (req->wait_for_swap) {
	wait_for_swap = true;
	
	if (log_level) log.AddLog("[%.3f]: %s", glfwGetTime(), script);
#ifdef JETSON_NANO
	GPIO::output(output_pin, GPIO::HIGH);
#endif
      }

      retcode = Tcl_Eval(interp, script);

      const char *rcstr = Tcl_GetStringResult(interp);
      if (retcode == TCL_OK) {
	if (!req->wait_for_swap) {
	  if (rcstr) {
	    req->rqueue->push_back(std::string(rcstr));
	  }
	  else {
	    req->rqueue->push_back("");
	  }
	}
	else {
	  // put result back into the request and queue for after swap
	  req->script = std::string(rcstr);
	  reply_queue.push_back(req);
	}
      }
      else {
	if (!req->wait_for_swap) {
	  if (rcstr) {
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
      char *dsstring = strdup(req->datapoint_string.c_str());
      const int dslen = strlen(dsstring);
      const char *p, *cmd;
      static char varname[128];

      /* remove newline in send string */
      if (dsstring[dslen-1] == '\n') dsstring[dslen-1] = '\0';

      //std::cout << "processDSCommands: " << dsstring << std::endl;
      
      /* Parse dataserver string and process */
      p = strchr(dsstring, ' ');
      if (!p) continue;
      
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
      req->rqueue->push_back(req->script);
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

      switch (msg) {
      case SET_BACKGROUND:
    setBackgroundColor();
    updateDisplay(log_events);
    did_update = 1;
    do_wakeup();
    break;
      case UPDATE_DISPLAY:
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

static void do_wakeup(void)
{
  app.wakeup();
}

void Application::start_tcp_server(void)
{
  struct sockaddr_in address;
  struct sockaddr client_address;
  socklen_t client_address_len = sizeof(client_address);
  int socket_fd;		// accept socket
  int new_socket_fd;		// client socket
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
  int socket_fd;		// accept socket
  int new_socket_fd;		// client socket
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
  int socket_fd;		// accept socket
  int new_socket_fd;		// client socket
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
	  if (script[0] == '!') {
	    client_request.wait_for_swap = true;
	    client_request.script = script.substr(1);
	  }
	  else {
	    client_request.wait_for_swap = false;
	    client_request.script = std::string(script);
	  }
	  /* push request onto queue for main thread to retrieve */
	  queue->push_back(&client_request);
	  
	  /* get lock and wake up main thread to process */
	  do_wakeup();
	  
	  /* rqueue will be available after command has been processed */
	  std::string s(client_request.rqueue->front());
	  client_request.rqueue->pop_front();
	  
	  //	  std::cout << "TCL Result: " << s << std::endl;
	  
	  // Add a newline, and send the buffer including the null termination
	  s = s+"\n";
#ifndef _MSC_VER
	  wrval = write(sockfd, s.c_str(), s.size());
#else
	  wrval = send(sockfd, s.c_str(), s.size(), 0);
#endif
	  if (wrval < 0) {		// couldn't send to client
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
      if (buffer[0] == '!') {
	client_request.wait_for_swap = true;
	client_request.script = std::string(buffer+1, msgSize-1);
      }
      else {
	client_request.wait_for_swap = false;
	client_request.script = std::string(buffer, msgSize);
      }
      
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
	  client_request.datapoint_string = std::string(dpoint_str);
	  
	  /* push request onto queue for main thread to retrieve */
	  queue->push_back(&client_request);
	  
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
    if (!show_imgui) {
      show_imgui = true;
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
  // from imgui_console.cpp
  void ShowAppConsole(bool* p_open);
  
  // Start the Dear ImGui frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  
  // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
  if (show_demo_window)
    ImGui::ShowDemoWindow(&show_demo_window);
  
  // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
  {
    static float f = 0.0f;
    static int counter = 0;
    ImVec4 clear_color = ImVec4(BackgroundColor[0], BackgroundColor[1], BackgroundColor[2], BackgroundColor[3]);
    
    ImGui::Begin("Stim Info");                          // Create a window called "Hello, world!" and append into it.
    
    ImGui::Checkbox("Console", &show_console);              // Edit bools storing our window open/close state
    ImGui::Checkbox("Show Log", &show_log);                      // Edit bools storing our window open/close state
    ImGui::SliderInt("Verbosity", &log_level, 0, 4);     
    
    ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
    ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color
    setBackgroundColorVals(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    setBackgroundColor();
    
    if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
      counter++;
    ImGui::SameLine();
    ImGui::Text("counter = %d", counter);
    
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::End();
  }
  
  if (show_console) ShowAppConsole(&show_console);
  if (show_log) showAppLog(&show_log);
  
  // Rendering
  ImGui::Render();
}


void Application::updateDisplay(bool log_events)
{
  OBJ_GROUP *g = NULL;
  if (OGL_NGROUPS(GList)) g = OGL_GROUP(GList, OGL_CURGROUP(GList));
  if (!g) return;

  /* Guarantee at least one is bound...*/
  glBindVertexArray(vao);

  /* If the stimuli are not visible, clear, swap, and acknowledge */
  if (!OGL_VISIBLE(GList)) {
    if (ClearBackground) {
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    
    if (!show_imgui)
      stopAnimation();
    else
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());  

    if (log_level && log_events) log.AddLog("[%.3f]: %s\n", glfwGetTime(), "PreSwap");
    glfwSwapBuffers(window);
    waitForSwap();
    if (log_level && log_events) log.AddLog("[%.3f]: %s\n", glfwGetTime(), "PostSwap");
    SwapCount++;
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
    
    drawGroup(g); 

    if (show_imgui)
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());  
    
    if (log_level && log_events) log.AddLog("[%.3f]: %s\n", glfwGetTime(), "PreSwap");
    /* Swap front and back buffers */
    glfwSwapBuffers(window);
    /* Wait for swap */
    waitForSwap();
    if (log_level && log_events) log.AddLog("[%.3f]: %s\n", glfwGetTime(), "PostSwap");

    SwapCount++;

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
    glfwSwapBuffers(window);
    SwapCount++;
    waitForSwap();

    /* Call any installed post frame command */
    updateTimes();
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
    drawGroup(g);
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
  //  per platform
  glfwGetFramebufferSize(app->window, &width, &height);
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
  if (action == GLFW_PRESS) {
    char procname[64];
    switch(key) {
    case GLFW_KEY_UP:
      strcpy(procname,"onUpArrow");
      if (!Tcl_FindCommand(OurInterp, procname, NULL, 0)) return;
      break;
    case GLFW_KEY_DOWN:
      strcpy(procname,"onDownArrow");
      if (!Tcl_FindCommand(OurInterp, procname, NULL, 0)) return;
      break;
    case GLFW_KEY_LEFT:
      strcpy(procname,"onLeftArrow");
      if (!Tcl_FindCommand(OurInterp, procname, NULL, 0)) return;
      break;
    case GLFW_KEY_RIGHT:
      strcpy(procname,"onRightArrow");
      if (!Tcl_FindCommand(OurInterp, procname, NULL, 0)) return;
      break;
    default:
      if (!Tcl_FindCommand(OurInterp, "onKeyProess", NULL, 0)) return;
      snprintf(procname, 63, "onKeyPress %d", key);  break;
    }
    if (procname[0]) {
      sendTclCommand(procname);
      //      Tcl_Eval(OurInterp, procname);
      //      Tcl_ResetResult(OurInterp);
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


int
main(int argc, char *argv[]) {
  int frame = 0;

  bool verbose = false;
  bool fullscreen = false;
  bool help = false;
  bool borderless = false;
  int width = 640, height = 480;
  int xpos = 30, ypos = 30;
  float refresh = 60;
  int interval = 2;
  const char *startup_file = NULL;
  bool updated_display = false;

  // Use this GPIO pin for swap acknowledge on Jetson Hardware
  int gpio_output_pin = 13;
  
  if (! glfwInit ()) {
    std::cout << "ERROR: could not start GLFW3" << std::endl;
    return 0;
  }
  
  cxxopts::Options options("stim2","multiplatform OpenGL presentation program");

  options.add_options()
    ("v,verbose", "Verbose mode", cxxopts::value<bool>(verbose))
    ("b,borderless", "Borderless", cxxopts::value<bool>(borderless))
    ("w,width", "width", cxxopts::value<int>(width))
    ("h,height", "height", cxxopts::value<int>(height))
    ("x,xpos", "x_position", cxxopts::value<int>(xpos))
    ("y,ypos", "y_position", cxxopts::value<int>(ypos))
    ("r,refresh", "refresh_rate", cxxopts::value<float>(refresh))
    ("t,timer", "timer interval", cxxopts::value<int>(interval))
    ("F,fullscreen", "Fullscreen mode", cxxopts::value<bool>(fullscreen))
    ("f,file", "File name", cxxopts::value<std::string>())
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

  if (borderless) {
    glfwWindowHint( GLFW_VISIBLE, GL_FALSE );
    glfwWindowHint( GLFW_DECORATED, GL_FALSE );
  }
  
  app.title = ("Stim");
  
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
    std::cerr << "Error opening window" << std::endl;
    exit(0);
  }

  if (borderless && !fullscreen) {
    glfwShowWindow(app.window);
  }


  ScreenWidth = mode->width;
  ScreenHeight = mode->height;
  RefreshRate = mode->refreshRate;
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

  app.init();
  app.init_imgui();

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
  
  app.net_thread = std::thread(&Application::start_tcp_server, &app);
  app.ds_net_thread = std::thread(&Application::start_dstcp_server, &app);
  app.msg_thread = std::thread(&Application::start_msg_server, &app);

  redraw();

  while (!glfwWindowShouldClose(app.window)) {
    app.wait_for_wakeup();

    glfwPollEvents();
    
    app.processTclCommands();
    app.processDSCommands();
    app.processTimerFuncs();

    if (app.show_imgui)
      app.processImgui();

    updated_display = app.processMessages(app.wait_for_swap);
    animEventPending = 0;
    
    if (!updated_display && app.doUpdate()) {
      app.updateDisplay(app.wait_for_swap);
    }

    app.processReplies();
  }
  
  /* free allocated objects */
  setDynamicUpdate(0);
  objListReset(curobjlistp);
  
  /* shutdown timer thread */
  app.stopTimer();

  /* detach from TCP/IP threads */
  app.net_thread.detach();
  app.ds_net_thread.detach();
  app.msg_thread.detach();

#ifdef _MSC_VER
    WSACleanup();
#endif
  
  glfwTerminate();
  
  return 0;
}

