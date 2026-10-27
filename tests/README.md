# stim2 tests

Self-checking scripts that run inside a real stim2. Each prints `OK` /
`FAIL` lines and a summary, then exits non-zero on any failure (stim2's
`exit` honours its status).

    stim2 -w 640 -h 480 -f /abs/path/to/tests/test_frame_grab.tcl
    stim2 -w 600 -h 600 -f /abs/path/to/tests/test_dynlist_numeric.tcl
    stim2 --headless -w 320 -h 180 --port-base 5610 -f /abs/path/to/tests/test_headless_step.tcl

`-f` needs an absolute path.

| test | covers |
|---|---|
| `test_frame_grab.tcl` | `frameGrab` (pixels, row order, `-rect`, `-alpha`, dlsh conversion) and `afterFrames` (exact swap counts on a static display; display messages are not delayed) |
| `test_headless_step.tcl` | headless rendering, step mode and the frame sink (see `docs/headless.md`): the offscreen display's exact size, `stepMode` / `stepClock` / `stepFrame` arguments and clock rules, a held redraw then dynamic updates drawn at the stepped times, the flip time, a square moved by the clock alone found where its stepped time puts it in every frame the sink wrote, and `frameGrab` agreeing with the sink. `test_frame_grab.tcl` also passes headless |
| `test_dynlist_numeric.tcl` | every numeric dynlist type through the modules that read coordinate, id and pixel lists |
| `test_ack_header.tcl` | the `@` reply-header prefix on the message port (`ACK1 <dequeue> <done> <swapcount> <flipwall_us>\n<result>`), `@!` after-swap stamps, `stimAckHeader` probe, that plain and `!` commands are unchanged, and `stimClock` (`<ticks_ms> <wall_us> <span_us>`: tick lies between the surrounding `@` stamps, wall within 1 s of the client's clock, span small) |
| `test_frame_log.tcl` | the per-swap frame record (`stimFrameLog host ?0|1?`, dserv datapoint `stim/frame`, see `docs/stim-frame.md`): stands in for dserv on a free port, decodes the 128-byte `>` frames, checks ordering and units of the seven fields, stop/restart, status counts, and that a dead sink only costs dropped records |
| `test_late_draw.tcl` | the `stimLateDraw` command (opt-in draw-late scheduler, see `docs/late-draw.md`): status dict shape, argument errors, that enabling is refused naming the strategy where there are no flip timestamps (Mac/glfinish) and accepted on a rig with them (then a dynamic group animates without misses and `@!` acks still follow a swap) |
| `test_stimess.tcl` | the `stimess` package (`modules/stimess-1.0.tm`, see `docs/stimess.md`): `stim::bind` / `get` / `count` / event handlers / error counting / `unbind_all` / `bindings_json`, with stim2's `::dsCmds` dispatch imitated and the qpcs procs stubbed |

Four tests are exceptions and run from plain `tclsh`. `test_ack_header.tcl`,
`test_frame_log.tcl` and `test_late_draw.tcl` are clients against an
already running stim2 (default `localhost 4612`; the frame-log test's fake
dserv listens on 127.0.0.1, so that stim2 must be local). To test a build
beside a live stim2, start it with `--port-base 5610` and pass port 5612:

    tclsh tests/test_ack_header.tcl ?host? ?port?
    tclsh tests/test_frame_log.tcl ?host? ?port?
    tclsh tests/test_late_draw.tcl ?host? ?port?

The draw-late scheduler's arithmetic has a display-free unit check built
with stim2 (`tests/latedraw_check.cpp`):

    ctest --test-dir build -R latedraw

`test_stimess.tcl` needs neither stim2 nor dserv:

    tclsh tests/test_stimess.tcl

To check what a scene draws, chain steps with `afterFrames n script` and
read frames with `frameGrab script`, which gets `w h pixels swapcount`
(RGBA bytes, rows top-down). For pixel maths in dlsh:

    dl_local px [dl_clist]; dl_fromString $pixels $px
    dl_local v [dl_uint $px]     ;# 0..255
