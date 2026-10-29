/*
 * mirror.h -- `stim2 --mirror`: the live-mirror driver in a thread of
 * stim2 (mirror.cpp, docs/mirror.md).
 */
#ifndef MIRROR_H
#define MIRROR_H

#include <tcl.h>

/* Start the driver thread: spec is "HOST ?options?", paths a Tcl list
   {auto_path tm_paths} from stim2's interpreter, sendInit what adds
   stimSend to an interpreter.  -1 if one is running, -2 if an earlier one
   would not stop (it may still be running: no restart then). */
int  mirrorStart(const char *spec, const char *paths, Tcl_LibraryInitProc *sendInit);

/* Stop it and join it (at most about a second): drain answers any request
   the thread has queued for the render thread.  Safe when not running;
   after it, mirrorStart may run again. */
void mirrorStop(void (*drain)(void));

/* 1 once stopping: stimSend refuses from then on */
int  mirrorStopping(void);

/* 1 between a start and its stop */
int  mirrorRunning(void);

/* tools/mirror/discover.tcl as embedded: mirror::discover, ::discover_async */
const char *mirrorDiscoverScript(void);

#endif
