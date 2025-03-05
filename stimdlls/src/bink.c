/*
 * bink.c
 *  Use the bink library to render animations using shaders
 */


#ifdef WIN32
#include <windows.h>
#define __arm__
#endif

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <bink.h>
#include "binktextures.h"
#include <tcl.h>

#include <stim2.h>
#include <prmutil.h>

typedef struct _bink_video {
  HBINK bink;
  int width;
  int height;
  unsigned int frame_count;
  int repeat_mode;
  int visible;
  int paused;
  int redraw;
  int start_frame;
  int cur_frame;
  int stop_frame;
  float x0, y0, x1, y1;
  char *timer_script;
  BINKSHADERS *Shaders;
  BINKTEXTURES *Textures;
} BINK_VIDEO;

static int BinkID = -1;	/* unique granny object id */

void videoOff(GR_OBJ *gobj) 
{
  BINK_VIDEO *b = (BINK_VIDEO *) GR_CLIENTDATA(gobj);
  BinkPause(b->bink, 1);
}


void videoShow(GR_OBJ *gobj) 
{
  BINK_VIDEO *b = (BINK_VIDEO *) GR_CLIENTDATA(gobj);

  float HALF_SCREEN_DEG_X;
  float HALF_SCREEN_DEG_Y;
  char *valstr;
  float dx, dy, txx, tyy;
  float sx = GR_SX(gobj);
  float sy = GR_SY(gobj);
  float tx = GR_TX(gobj);
  float ty = GR_TY(gobj);
  /* rotation variable could be implemented here */
  float aspect;

  /* if we've passed the end of the current stop frame, just return */
  if (b->stop_frame && (b->cur_frame > b->stop_frame)) {
    b->redraw = 0;
    return;
  }

  if (b->redraw) {
    valstr = puGetParamEntry((PARAM_ENTRY *) getParamTable(), 
			     "HalfScreenDegreeX");
    HALF_SCREEN_DEG_X = (float) atof(valstr);
    valstr = puGetParamEntry((PARAM_ENTRY *) getParamTable(), 
			     "HalfScreenDegreeY");
    HALF_SCREEN_DEG_Y = (float) atof(valstr);
    
    aspect = HALF_SCREEN_DEG_Y/HALF_SCREEN_DEG_X;
    
    dx = sx/(HALF_SCREEN_DEG_X*2.);
    dy = (sy/(HALF_SCREEN_DEG_Y*2.))*((float) (b->height)/b->width);
    
    txx = tx/(HALF_SCREEN_DEG_X*2);
    tyy = ty/(HALF_SCREEN_DEG_Y*2);
    
    b->x0 = 0.5-(dx/2)+txx;
    b->x1 = 0.5+(dx/2)+txx;
    b->y0 = 0.5-(dy/2)-tyy;
    b->y1 = 0.5+(dy/2)-tyy;
    
    Set_Bink_draw_position(b->Textures, b->x0, b->y0, b->x1, b->y1);
    Draw_Bink_textures(b->Textures, b->Shaders, 0);

    b->redraw = 0;
  }
}

void videoOnTimer(GR_OBJ *gobj) 
{
  BINK_VIDEO *b = (BINK_VIDEO *) GR_CLIENTDATA(gobj);
  
  if (b->timer_script) sendTclCommand(b->timer_script);
  
  // Is it time for a new Bink frame?
  if ( ! BinkWait( b->bink ) ) {
    Start_Bink_texture_update( b->Textures );
    BinkDoFrame( b->bink );
    Finish_Bink_texture_update( b->Textures );
    
    // Keep playing the movie.
    BinkNextFrame( b->bink );
    b->cur_frame++;
    
    // do we need to skip a frame?
    while ( BinkShouldSkip( b->bink ) )
      {
	Start_Bink_texture_update( b->Textures );
	BinkDoFrame( b->bink );
	Finish_Bink_texture_update( b->Textures );
	BinkNextFrame( b->bink );
	b->cur_frame++;
      }
    // we have finished a frame, so set a flag to draw it
    b->redraw = 1;
  }
  else {
    /* always redraw when running with vsync on */
    b->redraw = 1;
  }
  kickAnimation();
}

void videoDelete(GR_OBJ *gobj) 
{
  BINK_VIDEO *b = (BINK_VIDEO *) GR_CLIENTDATA(gobj);

  if (b->Textures) Free_Bink_textures( b->Textures );
  if (b->Shaders) Free_Bink_shaders( b->Shaders );
  if (b->bink) BinkClose( b->bink );
  if (b->timer_script) free(b->timer_script);
  
  free((void *) b);
}

void videoReset(GR_OBJ *gobj) 
{
//  BINK_VIDEO *b = (BINK_VIDEO *) GR_CLIENTDATA(gobj);
//  BinkGoto(b->bink, 1, 0);
//  BinkPause(b->bink, 0);
//  b->frame_count = 0;
}


int videoCreate(OBJ_LIST *objlist, char *filename, double rate, int play_audio)
{
  const char *name = "Bink";
  GR_OBJ *obj;
  BINK_VIDEO *b;
  BINKSUMMARY summary;  

  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = BinkID;

  GR_TIMERFUNCP(obj) = videoOnTimer;
  GR_DELETEFUNCP(obj) = videoDelete;
  GR_RESETFUNCP(obj) = videoReset;
  GR_OFFFUNCP(obj) = videoOff;
  GR_ACTIONFUNCP(obj) = videoShow;

  b = (BINK_VIDEO *) calloc(1, sizeof(BINK_VIDEO));
  GR_CLIENTDATA(obj) = b;

  b->x0 = 0.0;
  b->y0 = 0.0;
  b->x1 = 1.0;
  b->y1 = 1.0;

  if (!play_audio) {
    BinkSetSoundTrack(0,0);
    b->bink = BinkOpen( filename, 
			BINKALPHA | BINKNOFRAMEBUFFERS | BINKSNDTRACK);
  }
  else {
    b->bink = BinkOpen( filename, BINKALPHA | BINKNOFRAMEBUFFERS );
  }

  if ( !b->bink ) {
    fprintf(getConsoleFP(), "error opening bink file\n");
    free(b);
    return -1;
  }

  BinkGetSummary(b->bink, &summary);
  b->width = summary.Width;
  b->height = summary.Height;
  b->cur_frame = 1;
  b->start_frame = 1;
  b->stop_frame = 0;

  // Create the Bink shaders to use

  b->Shaders = Create_Bink_shaders(0);

  if ( !b->Shaders )
  {
    fprintf(getConsoleFP(), "error creating shaders\n");
    BinkClose( b->bink );
    return -1;
  }

  // Try to create textures for Bink to use.
  b->Textures = Create_Bink_textures(b->Shaders, b->bink, 0);
  if ( !b->Textures )
  {
    fprintf(getConsoleFP(), "error creating textures\n");
    BinkClose( b->bink );
    return -1;
  }

  b->visible = 1;
  return(gobjAddObj(objlist, obj));
}



static int videoCmd(ClientData clientData, Tcl_Interp *interp,
			  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;
  double rate = 0;
  int play_audio = 1;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " binkfile ?play_audio? ?rate?", NULL);
    return TCL_ERROR;
  }

  if (argc > 2) {
    if (Tcl_GetInt(interp, argv[2], &play_audio) != TCL_OK)
      return TCL_ERROR;
  }

  if (argc > 3) {
    if (Tcl_GetDouble(interp, argv[3], &rate) != TCL_OK)
      return TCL_ERROR;
  }

  if ((id = videoCreate(olist, argv[1], rate, play_audio)) < 0) {
    Tcl_SetResult(interp, "error loading bink video", TCL_STATIC);
    return(TCL_ERROR);
  }

  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));

  return(TCL_OK);
}

static int videoSetRepeatModeCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BINK_VIDEO *b;
  int id;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " bink_object NORMAL|ONESHOT", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a bink object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != BinkID) {
    Tcl_AppendResult(interp, argv[0], ": object not a video object", NULL);
    return TCL_ERROR;
  }
  b = (BINK_VIDEO *) GR_CLIENTDATA(OL_OBJ(olist,id));

  if (!strcmp(argv[2],"oneshot") || !strcmp(argv[2],"ONESHOT")) {
    b->repeat_mode = G_ONESHOT;
  }
  else if (!strcmp(argv[2],"normal") || !strcmp(argv[2],"NORMAL")) {
    b->repeat_mode = G_NORMAL;
  }

  return(TCL_OK);
}

static int videoSetCoordsCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BINK_VIDEO *b;
  int id;
  double x0, y0, x1, y1;
  
  if (argc != 2 && argc < 5) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " bink_object x0 y0 x1 y1", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a bink object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != BinkID) {
    Tcl_AppendResult(interp, argv[0], ": object not a video object", NULL);
    return TCL_ERROR;
  }
  b = (BINK_VIDEO *) GR_CLIENTDATA(OL_OBJ(olist,id));

  if (argc == 2) {
    Tcl_Obj *listPtr = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewDoubleObj(b->x0));
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewDoubleObj(b->y0));
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewDoubleObj(b->x1));
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewDoubleObj(b->y1));
    Tcl_SetObjResult(interp, listPtr);
    return TCL_OK;
  }

  if (Tcl_GetDouble(interp, argv[2], &x0) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &y0) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &x1) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &y1) != TCL_OK) return TCL_ERROR;
  
  if (x0 < 0) b->x0 = 0.0; else if (x0 > 1.0) b->x0 = 1.0; else b->x0 = x0; 
  if (y0 < 0) b->y0 = 0.0; else if (y0 > 1.0) b->y0 = 1.0; else b->y0 = y0; 
  if (x1 < 0) b->x1 = 0.0; else if (x1 > 1.0) b->x1 = 1.0; else b->x1 = x1; 
  if (y1 < 0) b->y1 = 0.0; else if (y1 > 1.0) b->y1 = 1.0; else b->y1 = y1; 

  return(TCL_OK);
}

#ifdef GRAYSCALE
static int videoSetGrayscaleCmd(ClientData clientData, Tcl_Interp *interp,
				int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BINK_VIDEO *b;
  int id;
  int grayscale;
  
  if (argc != 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " bink_object grayscale", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a bink object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != BinkID) {
    Tcl_AppendResult(interp, argv[0], ": object not a video object", NULL);
    return TCL_ERROR;
  }
  b = (BINK_VIDEO *) GR_CLIENTDATA(OL_OBJ(olist,id));
  if (Tcl_GetInt(interp, argv[2], &grayscale) != TCL_OK) return TCL_ERROR;

  Set_Bink_grayscale_settings(b->Textures, grayscale);
  
  return(TCL_OK);
}
#endif

static int videoPauseCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BINK_VIDEO *b;
  int id;
  int pause;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " bink_object 0|1", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a bink object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != BinkID) {
    Tcl_AppendResult(interp, argv[0], ": object not a bink object", NULL);
    return TCL_ERROR;
  }
  b = (BINK_VIDEO *) GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &pause) != TCL_OK) return TCL_ERROR;
  b->paused = pause;

  return(TCL_OK);
}

static int videoSetFrameLimitsCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BINK_VIDEO *b;
  int id;
  int start, stop;
  
  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " bink_object start stop", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a bink object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != BinkID) {
    Tcl_AppendResult(interp, argv[0], ": object not a bink object", NULL);
    return TCL_ERROR;
  }
  b = (BINK_VIDEO *) GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &start) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &stop) != TCL_OK) return TCL_ERROR;

  b->start_frame = start;
  BinkGoto(b->bink, b->start_frame, 0);
  
  b->stop_frame = stop;

  return(TCL_OK);
}

static int videoTimerScriptCmd(ClientData clientData, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BINK_VIDEO *b;
  int id;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " bink_object script", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a bink object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != BinkID) {
    Tcl_AppendResult(interp, argv[0], ": object not a bink object", NULL);
    return TCL_ERROR;
  }
  b = (BINK_VIDEO *) GR_CLIENTDATA(OL_OBJ(olist,id));
  
  if (b->timer_script) free(b->timer_script);
  b->timer_script = strdup(argv[2]);

  return(TCL_OK);
}

static int videoGetInfoCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BINK_VIDEO *b;
  BINKSUMMARY summary;  
  int id;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " bink_object", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a bink object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != BinkID) {
    Tcl_AppendResult(interp, argv[0], ": object not a bink object", NULL);
    return TCL_ERROR;
  }
  b = (BINK_VIDEO *) GR_CLIENTDATA(OL_OBJ(olist,id));
  BinkGetSummary(b->bink, &summary);

  char resultstr[256];
  sprintf(resultstr, "%d %d %f %f %f %f %.0f %d", 
	  summary.Width, summary.Height,
	  b->x0, b->y0, b->x1, b->y1,
	  1000.*((float) (summary.TotalFrames) /
		 ((float) (summary.FrameRate)/summary.FrameRateDiv)),
	  b->cur_frame);
  Tcl_SetObjResult(interp, Tcl_NewStringObj(resultstr, -1));
  return(TCL_OK);
}


static int videoFileInfoCmd(ClientData clientData, Tcl_Interp *interp,
			   int argc, char *argv[])
{
  HBINK bink;
  BINKSUMMARY summary;  

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " filename", NULL);
    return TCL_ERROR;
  }

  bink = BinkOpen( argv[1], BINKNOFILLIOBUF | BINKNOTHREADEDIO );
  if (!bink) {
    Tcl_AppendResult(interp, "video_fileInfo: unable to open file \"",
		    argv[1], "\"", NULL);
    return TCL_ERROR;
  }
  BinkGetSummary(bink, &summary);
  BinkClose(bink);
  
  Tcl_Obj *listPtr = Tcl_NewListObj(0, NULL);
  Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewIntObj(summary.Width));
  Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewIntObj(summary.Height));
  Tcl_ListObjAppendElement(interp, listPtr,
			   Tcl_NewDoubleObj((summary.FileFrameRate/
					     (float) summary.FileFrameRateDiv)));
  Tcl_ListObjAppendElement(interp, listPtr,
			   Tcl_NewDoubleObj(1000.*((float) (summary.TotalFrames) /
						   ((float) (summary.FileFrameRate)/
						    summary.FileFrameRateDiv))));
  Tcl_SetObjResult(interp, listPtr);

  return(TCL_OK);
}


static int videoGetSummaryCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  BINK_VIDEO *b;
  int id;
  BINKSUMMARY summary;  
  Tcl_DString s;
  char buf[64];

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " bink_object", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a bink object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != BinkID) {
    Tcl_AppendResult(interp, argv[0], ": object not a bink object", NULL);
    return TCL_ERROR;
  }
  b = (BINK_VIDEO *) GR_CLIENTDATA(OL_OBJ(olist,id));

  BinkGetSummary(b->bink, &summary);
  
  Tcl_DStringInit(&s);

  sprintf(buf, "Width %d", summary.Width);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "Height %d", summary.Height);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalTime %d", summary.TotalTime);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "FileFrameRate %d", summary.FileFrameRate);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "FileFrameRateDiv %d", summary.FileFrameRateDiv);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "FrameRate %d", summary.FrameRate);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "FrameRateDiv %d", summary.FrameRateDiv);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalOpenTime %d", summary.TotalOpenTime);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalFrames %d", summary.TotalFrames);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalPlayedFrames %d", summary.TotalPlayedFrames);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "SkippedFrames %d", summary.SkippedFrames);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "SkippedBlits %d", summary.SkippedBlits);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "SoundSkips %d", summary.SoundSkips);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalBlitTime %d", summary.TotalBlitTime);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalReadTime %d", summary.TotalReadTime);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalVideoDecompTime %d", summary.TotalVideoDecompTime);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalAudioDecompTime %d", summary.TotalAudioDecompTime);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalIdleReadTime %d", summary.TotalIdleReadTime);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalBackReadTime %d", summary.TotalBackReadTime);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalReadSpeed %d", (int) summary.TotalReadSpeed);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "SlowestFrameTime %d", (int) summary.SlowestFrameTime);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "Slowest2FrameTime %d", (int) summary.Slowest2FrameTime);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "SlowestFrameNum %d", summary.SlowestFrameNum);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "Slowest2FrameNum %d", summary.Slowest2FrameNum);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "AverageDataRate %d", summary.AverageDataRate);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "AverageFrameSize %d", summary.AverageFrameSize);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "HighestMemAmount %d", (int) summary.HighestMemAmount);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "TotalIOMemory %d", (int) summary.TotalIOMemory);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "HighestIOUsed %d", (int) summary.HighestIOUsed);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "Highest1SecRate %d", (int) summary.Highest1SecRate);
  Tcl_DStringAppendElement(&s, buf);
  sprintf(buf, "Highest1SecFrame %d", (int) summary.Highest1SecFrame);
  Tcl_DStringAppendElement(&s, buf);

  Tcl_DStringResult(interp, &s);

  return(TCL_OK);
}


#ifdef _WIN32
EXPORT(int, Bink_Init) (Tcl_Interp *interp)
#else
#ifdef __cplusplus
extern "C" {
#endif
extern int Bink_Init(Tcl_Interp *interp);
#ifdef __cplusplus
}
#endif
int Bink_Init(Tcl_Interp *interp)
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
  
  if (BinkID < 0) {
    BinkID = gobjRegisterType();
  }
  else return TCL_OK;

#ifdef _WIN32
#ifndef __arm__
  BinkSoundUseDirectSound( 0 );
#endif
#else
  extern void init_coreaudio( void );
  //  init_coreaudio();
#endif

#ifdef LINUX
  //BinkSoundUseOpenAL();
  BinkSoundUsePulseAudio( 48000, 2 );
#endif
  
  Tcl_CreateCommand(interp, "video",  (Tcl_CmdProc *) videoCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "video_pause",  (Tcl_CmdProc *) videoPauseCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "video_timerScript",  
		    (Tcl_CmdProc *) videoTimerScriptCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "video_setRepeatMode",  
		    (Tcl_CmdProc *) videoSetRepeatModeCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "video_setCoords",  
		    (Tcl_CmdProc *) videoSetCoordsCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
#ifdef GRAYSCALE
  Tcl_CreateCommand(interp, "video_setGrayscale",  
		    (Tcl_CmdProc *) videoSetGrayscaleCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
#endif  
  Tcl_CreateCommand(interp, "video_setFrameLimits",  
		    (Tcl_CmdProc *) videoSetFrameLimitsCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "video_getInfo", 
		    (Tcl_CmdProc *) videoGetInfoCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "video_getSummary",  
		    (Tcl_CmdProc *) videoGetSummaryCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "video_fileInfo",  
		    (Tcl_CmdProc *) videoFileInfoCmd, 
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
