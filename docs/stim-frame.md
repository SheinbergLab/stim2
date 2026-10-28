# `stim/frame`: the per-swap frame record

Off by default. When enabled, stim2 publishes one dserv datapoint named
`stim/frame` for every completed buffer swap, so a data file knows the time
of every swap and what had been applied to the display by then. ESS turns it
on at configure; nothing changes for scripts that never ask for it.

## Command

    stimFrameLog ?host? ?0|1?

- `stimFrameLog host 1`: start streaming to dserv at `host` (`host` or
  `host:port`; default port 4620, dserv's fire-and-forget datapoint port).
  The connection is made in the background; the command returns at once.
  Starting again toward the same host is a no-op, toward a different host
  restarts the stream (counts reset).
- `stimFrameLog host 0`: stop and close.
- `stimFrameLog` (or any form) returns the state as a dict:

      enabled 0|1 host <h> sent <n> dropped <n> connected 0|1

  `sent` and `dropped` count since the last start and survive a stop. The
  bare command is also the capability probe: an older stim2 answers
  `invalid command name`.

## Record

One `DSERV_INT64` (type 16) array of eight values, 64 bytes, timestamp 0
(dserv stamps it on receipt):

| # | field | meaning |
|---|---|---|
| 1 | `frame` | records sent since the stream was enabled, from 1; contiguous in what dserv receives |
| 2 | `swapcount` | stim2's `SwapCount` after this swap (the same counter the ACK1 header and `swapStats` report) |
| 3 | `flip_us` | hardware flip time of this swap, CLOCK_REALTIME microseconds; -1 when the swap-sync strategy cannot time flips (e.g. glFinish on a Mac) |
| 4 | `ticks_us` | the free-running stim clock at the flip, in microseconds (`StimTicksF` is the same clock in ms), read the moment the swap wait returned |
| 5 | `missed` | `swapsyncMissedFrames()`, cumulative since start |
| 6 | `cmds_applied` | commands evaluated from the command/message ports since process start |
| 7 | `ds_dispatched` | datapoints dispatched to the script from dserv since process start (bound or not) |
| 8 | `eval_us` | the stim clock the frame was computed at, in microseconds on the same clock as `ticks_us` (from stim2 0.28.5). See below. |

A gap in `swapcount` between consecutive records means records were
dropped (the stream was down or the queue overflowed); `dropped` in the
status dict counts them.

## The frame clock (`eval_us`)

From the start of a frame's update to its swap, the stim clock holds at one
instant. The update functions, the pre and post scripts, the timers and
`StimTimeF` all read that instant for the whole frame. Before this, the
timer thread refreshed the clock every millisecond, so a frame drawn over
a millisecond boundary saw two times. The swap wait and everything after
it (post-frame scripts, `ticks_us`, ACK stamps) read the running clock, as
before.

`eval_us` records that instant. With late-draw off it falls just after
the previous flip, by however long the loop took to get there. With
late-draw on, it falls at about flip - budget. In step mode it is
`stepFrame`'s first time.

A replay (`stepFrame eval_us/1000 ticks_us/1000`) therefore computes each
frame at exactly the rig's instant. Before `eval_us`, a replay evaluated
each frame at the previous flip and missed the loop's jitter. A frame script
that integrates its own physics on `StimTimeF` could then take a step one
frame later than the rig did (door, 2026-10-28; see `mirror.md`).

Records from a stim2 before 0.28.5 have seven values; `tools/lib/framelock.tcl`
reads either.

The cost on the render loop is one clock read and one atomic store per
pass, plus one more value in the record.

## Wire format

The fixed 128-byte `>` frame that dserv's `DPOINT_BINARY_MSG_CHAR` parser
and `qpcs::dsSocketSendBytes` use, integers little-endian:

    byte 0       '>'
    bytes 1-2    uint16 varlen = 10
    bytes 3-12   "stim/frame"
    bytes 13-20  uint64 timestamp = 0
    bytes 21-24  uint32 datatype = 16
    bytes 25-28  uint32 datalen = 64
    bytes 29-92  8 x int64
    bytes 93-127 zero

## Cost and failure behaviour

The render thread copies six integers into a 256-slot lock-free ring and
notifies a writer thread; no Tcl, socket I/O, blocking or allocation per
frame. The writer owns its own TCP connection (never the script's
`::dserv_return_sock`). If dserv is down or goes away, records are dropped
and counted while the writer reconnects no faster than once a second; the
render thread never waits on it. The writer is stopped and joined on
`stimFrameLog host 0` and at process exit, flushing what is queued.

Test: `tclsh tests/test_frame_log.tcl` against a running local stim2.
