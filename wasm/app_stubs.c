/* Symbols tclproc.c and the graphics modules expect from stim2.cpp.
   The window, the sockets, and the imgui console stay out. The refresh
   itself is frame.cpp. */
#include <stdio.h>
#include <tcl.h>
#include <string.h>
#include "prmutil.h"
#include "stim2.h"

float BackgroundColor[4] = { 0.f, 0.f, 0.f, 1.f };
float GreenBackgroundColor[3] = { 0.f, 0.8f, 0.2f };
float RedBackgroundColor[3] = { 0.8f, 0.f, 0.f };
int OverlayBackgroundColor = 0;
int SwapCount = 0;
int SwapPulse = 0;
int SwapAcknowledge = 0;
int StereoMode = 0;
int BlockMode = 0;
int MouseXPos = 0;
int MouseYPos = 0;
unsigned int StimVersion = 0;
DUMP_INFO DumpInfo;

void reshape(void) {}

int diagSetCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    (void)cd; (void)interp; (void)argc; (void)argv;
    return TCL_OK;
}
int diagClearCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    (void)cd; (void)interp; (void)argc; (void)argv;
    return TCL_OK;
}
int diagShowCmd(ClientData cd, Tcl_Interp *interp, int argc, char *argv[]) {
    (void)cd; (void)interp; (void)argc; (void)argv;
    Tcl_SetResult(interp, "", TCL_STATIC);
    return TCL_OK;
}

void kill_window(void) {}
void resetGraphicsState(void) {}
extern int stim_dump_raw(void);
void sendDispMsg(int event) {
    if (event == DUMP_RAW) stim_dump_raw();
}
void sendDispMsgWithData(int event, int data) { (void)event; (void)data; }
int getStereoMode(void) { return StereoMode; }
int setStereoMode(int mode) { StereoMode = mode; return mode; }
int setVerboseLevel(int level) { (void)level; return 0; }
void doToggleImgui(void) {}
void logMessage(char *message) { (void)message; }
FILE *getConsoleFP(void) { return stderr; }
