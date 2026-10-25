/*
 * framelog.h -- per-swap frame record published to dserv as "stim/frame"
 *
 * Off by default.  `stimFrameLog host 1` starts a writer thread with its
 * own TCP connection to dserv's fire-and-forget datapoint port (4620, or
 * host:port); every completed swap then produces one DSERV_INT64 record of
 * seven values, in this order:
 *
 *   1 frame         writer's running count of records sent since enable (1..)
 *   2 swapcount     SwapCount after the increment for this swap
 *   3 flip_us       swapsyncLastFlipWallUs() for this swap (-1 = unknown)
 *   4 ticks_us      stim clock at the flip, microseconds (1e6 * glfwGetTime(),
 *                   i.e. StimTicksF * 1000), read right after waitForSwap
 *   5 missed        swapsyncMissedFrames(), cumulative
 *   6 cmds_applied  commands evaluated by processTclCommands since start
 *   7 ds_dispatched datapoints dispatched by processDSCommands since start
 *
 * Render-thread cost per frame: copy six integers into a lock-free ring
 * (256 slots) and a condition-variable notify.  A full ring drops the record
 * and counts it; the render thread never blocks or allocates for this.  A
 * dead dserv only costs dropped records: the writer reconnects with a 1 s
 * backoff and never touches the render thread.
 */
#ifndef FRAMELOG_H
#define FRAMELOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Render-thread counters (plain: written and read on the render thread
   only; the record copies them).  Incremented where the work is applied. */
extern long long FrameLogCmdsApplied;
extern long long FrameLogDsDispatched;

/* 1 while a writer is running (relaxed atomic read; cheap per frame). */
int frameLogEnabled(void);

/* Render thread, once per real swap, after SwapCount++ and after
   waitForSwap has returned.  Never blocks; drops (and counts) when the
   ring is full or no writer is running. */
void frameLogPush(int swapcount, long long flip_us, long long ticks_us,
                  long missed, long long cmds_applied,
                  long long ds_dispatched);

/* Start the writer toward host (optionally "host:port"; default port
   4620).  Already running toward the same target: no-op.  Running toward
   another: that writer is stopped and a new one started (counts reset).
   Connects asynchronously in the writer thread, so this returns at once.
   Returns 0, or -1 with *err set (static string) on a bad argument. */
int frameLogStart(const char *hostspec, const char **err);

/* Stop and join the writer, close its socket.  Counts are kept for
   status.  Safe when not running.  Also run at process exit. */
void frameLogStop(void);

typedef struct framelog_status_s {
  int enabled;            /* writer running */
  const char *host;       /* hostspec as given to the last start ("" if none) */
  long long sent;         /* records written since the last start */
  long long dropped;      /* records discarded since the last start:
                             ring full, no link, or write failure */
  int connected;          /* TCP link to dserv currently up */
} framelog_status_t;

void frameLogStatus(framelog_status_t *s);

#ifdef __cplusplus
}
#endif

#endif /* FRAMELOG_H */
