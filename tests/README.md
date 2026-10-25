# stim2 tests

Self-checking scripts that run inside a real stim2. Each prints `OK` /
`FAIL` lines and a summary, then exits non-zero on any failure (stim2's
`exit` honours its status).

    stim2 -w 640 -h 480 -f /abs/path/to/tests/test_frame_grab.tcl
    stim2 -w 600 -h 600 -f /abs/path/to/tests/test_dynlist_numeric.tcl

`-f` needs an absolute path.

| test | covers |
|---|---|
| `test_frame_grab.tcl` | `frameGrab` (pixels, row order, `-rect`, `-alpha`, dlsh conversion) and `afterFrames` (exact swap counts on a static display; display messages are not delayed) |
| `test_dynlist_numeric.tcl` | every numeric dynlist type through the modules that read coordinate, id and pixel lists |
| `test_ack_header.tcl` | the `@` reply-header prefix on the message port (`ACK1 <dequeue> <done> <swapcount> <flipwall_us>\n<result>`), `@!` after-swap stamps, `stimAckHeader` probe, and that plain and `!` commands are unchanged |
| `test_stimess.tcl` | the `stimess` package (`modules/stimess-1.0.tm`, see `docs/stimess.md`): `stim::bind` / `get` / `count` / event handlers / error counting / `unbind_all` / `bindings_json`, with stim2's `::dsCmds` dispatch imitated and the qpcs procs stubbed |

Two tests are exceptions and run from plain `tclsh`. `test_ack_header.tcl`
is a client against an already running stim2 (default `localhost 4612`):

    tclsh tests/test_ack_header.tcl ?host? ?port?

`test_stimess.tcl` needs neither stim2 nor dserv:

    tclsh tests/test_stimess.tcl

To check what a scene draws, chain steps with `afterFrames n script` and
read frames with `frameGrab script`, which gets `w h pixels swapcount`
(RGBA bytes, rows top-down). For pixel maths in dlsh:

    dl_local px [dl_clist]; dl_fromString $pixels $px
    dl_local v [dl_uint $px]     ;# 0..255
