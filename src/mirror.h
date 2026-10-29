/*
 * mirror.h -- `stim2 --mirror`: the live-mirror driver in a thread of
 * stim2 (mirror.cpp, docs/mirror.md).
 */
#ifndef MIRROR_H
#define MIRROR_H

#include <tcl.h>

/* Start the driver thread: spec is "HOST ?options?", paths a Tcl list
   {auto_path tm_paths} from stim2's interpreter, sendInit what adds
   stimSend to an interpreter.  Once per process; -1 if already started. */
int  mirrorStart(const char *spec, const char *paths, Tcl_LibraryInitProc *sendInit);

/* Stop it and join it (at most about a second): drain answers any request
   the thread has queued for the render thread.  Safe when not started. */
void mirrorStop(void (*drain)(void));

/* 1 once stopping: stimSend refuses from then on */
int  mirrorStopping(void);

#endif
