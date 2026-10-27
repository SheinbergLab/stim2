# stimess: dserv datapoint bindings and the configure prelude

`stimess` is a pure-Tcl package shipped with stim2 (`modules/stimess-1.1.tm`,
installed beside `workspace` and `stwidget`; `package require stimess` works
in the app). It replaces the per-protocol copies of "subscribe to a dserv
datapoint, split its value into globals, read them from the frame script"
with one declarative call, and exposes the binding table so ESS can log the
bound streams into the data file and a replay can install the same bindings.
Since 1.1 it also carries the configure prelude, the dserv plumbing every
stim script relies on, in a live form for the rig and a playback form for a
replay (see "The prelude" below).

```tcl
package require stimess

# ESS pushes ess/dial/pointer as "x,y,show,band"
stim::bind ess/dial/pointer {x y show band}

proc targets_frame {args} {
    if {[stim::get ess/dial/pointer show]} {
        translateObj $::cursor [stim::get ess/dial/pointer x] \
                               [stim::get ess/dial/pointer y]
        setVisible $::cursor 1
    } else {
        setVisible $::cursor 0
    }
}

# per-arrival callback instead of polling
stim::bind ess/touch/event {x y type} -mode event -handler on_touch
```

## How it plugs in

stim2 is a dserv datapoint client: `qpcs::dsStimRegister $::dservhost` once,
then `qpcs::dsStimAddMatch $::dservhost <name>` per stream. dserv forwards
each matching datapoint as the text line

    <name> <type> <timestamp> <length> {<value>}

stim2's render thread stores the line in `::dsVals(<name>)` and, if
`::dsCmds(<name>)` exists, evaluates it with the line's words appended, so
the value is word 4. `stim::bind` installs `::stim::dispatch <name>` as that
entry; nothing in the existing path changes, and scripts that set `::dsCmds`
themselves keep working next to it.

The dispatcher is the per-datapoint cost on the render thread: one `lindex`,
one `split`, a `foreach` over the fields, two `incr`s and, in event mode, one
guarded handler call. It never throws into stim2: handler errors are caught
and counted.

## API (namespace `::stim`)

| command | |
|---|---|
| `stim::bind name fields ?-mode latest\|event? ?-sep ,? ?-handler proc?` | Register forwarding for `name`, install the dispatcher, record the binding. `fields` is a list of field names; the value is split on `-sep` (default `,`) into them. With one field the whole value is stored unsplit. Missing trailing parts read as `""`, extra parts are dropped. `latest` (default) stores only; `event` stores then calls `-handler` with the field values as arguments, once per arrival (`-handler` required). Returns 1 when `dsStimAddMatch` succeeded, 0 when `::dservhost` is unset or the match failed (the binding is still recorded and dispatches). Rebinding a name replaces it and resets its counters. |
| `stim::get name ?field?` | Latest value of `field` (`""` before the first arrival); error if unbound. Without `field`, a dict of all fields. |
| `stim::count name` | Arrivals for `name` since it was bound. |
| `stim::dispatch_count` | Arrivals across all bindings since the package loaded (the watermark a per-frame record can carry). Not reset by unbind. |
| `stim::errors name` | Handler errors swallowed for `name`. `stim::last_error name` gives the most recent message. |
| `stim::unbind name` | `dsStimRemoveMatch` for `name` if this package registered it, drop its `::dsCmds` entry (only if still ours) and the binding. Returns 1, or 0 if `name` was not bound. |
| `stim::unbind_all` | `unbind` for every name this package bound. Unrelated `::dsCmds` entries are untouched. ESS calls this at protocol reload, before sourcing the new stim script. |
| `stim::bindings` | List of dicts `{name fields mode sep}`, in bind order. |
| `stim::bindings_json` | The same as JSON, pure ASCII: `{"package":"stimess 1.1","bindings":[{"name":"ess/dial/pointer","fields":["x","y","show","band"],"mode":"latest","sep":","}]}`; `"bindings":[]` when nothing is bound. |
| `stim::prelude live -host H ?-port 4620?` | The rig's configure prelude (below). Returns `live`. |
| `stim::prelude playback` | A replay's stand-in for it (below). Returns `playback`. |
| `stim::playback` | 1 after `stim::prelude playback`, 0 otherwise. |
| `stim::inject name value ?type? ?timestamp?` | Deliver one datapoint exactly as stim2's render thread does: the line `<name> <type> <timestamp> <length> {<value>}` is stored in `::dsVals(name)` and `::dsCmds(name)`, if any, is evaluated with its words. Returns 1 when a handler ran, 0 when nothing is bound to `name`. `type` defaults to 1 (string), `timestamp` to now in microseconds. Works in either mode. |

Only `dsStimRemoveMatch` is used to undo a binding. `dsStimUnregister` would
drop stim2's whole client registration, including matches a script made on
its own, so `unbind` never calls it; the live prelude does, once, at the
start of a load, which is where a clean slate is wanted.

## The prelude

Before every stim script, ESS's `configure_stim` used to send about a hundred
lines of Tcl that the script then relied on without naming: register with
dserv, subscribe to `stimdg`, open the return socket, define `dserv_send`
and friends, bridge the window mouse to the touchscreen datapoints, point
`puts` at `stim/stdout`. None of it was in the data file, so a replay had no
way to know what the script expected of its environment, and most of it is
exactly what a replay must not do.

From 1.1 that text lives here, and `configure_stim` sends one recorded line
when stim2 carries it (an older stim2 still gets the inline block, which is
the same text; ESS keeps the two identical):

    stim::prelude live -host <dserv address>

which does, in this order: run the outgoing script's `stim_deinit` while its
procs and dgs still exist, then reset `stim_init`/`stim_deinit` to no-ops;
drop every `::dsCmds` and `::dsVals` entry and this package's bindings;
`dsStimUnregister` then `dsStimRegister` with the host (set as
`::dservhost`), and add the `stimdg` match with `readdg` (base64 → dg) as
its handler; open the persistent binary return socket to `host:4620` and
define `dserv_send`, `dserv_send_string`, `dserv_send_evt`, `dserv_tag_evt`
(installed with `swapTagCallback` where stim2 has it) and
`dserv_return_close`; define the mouse bridge `onMousePress`,
`onMouseRelease`, `onMouseMove` (`mtouch/event`, plus `mouse/event` and a
one-time `mouse/event/range` when `::mouse_bridge_mouse` is 1, which ESS
sets beforehand); and redirect `puts` to `stim/stdout` through
`qpcs::dsPut`, renaming the original to `_puts` once.

A replay (a stim2 fed from a data file, native or in a browser) answers the
recorded line with

    stim::prelude playback

Same hooks reset, same table clearing, then: `::dservhost` unset and no
registration; an open return socket closed and none opened; `dserv_send`,
`dserv_send_string`, `dserv_send_evt`, `dserv_tag_evt` and
`dserv_return_close` defined with the same signatures and doing nothing;
`readdg` still installed, so the file's `stimdg` record goes in with
`stim::inject stimdg [binary encode base64 $bytes] 6`; the mouse handlers
defined as no-ops; `puts` the plain one again. `stim::bind` then records
bindings without asking any server and without printing, and the logged
bound streams are fed back with `stim::inject`, one call per record, in
file order. A stim script sourced after this runs unchanged.

## Test

`tests/test_stimess.tcl` runs under plain `tclsh` with the qpcs procs stubbed
as recorders and stim2's dispatch imitated (`$::dsCmds(name) {*}<line>`):

    tclsh tests/test_stimess.tcl

It covers bind with and without `::dservhost`, a pointer-style line, `get`
in both forms, counts, event mode with plain and command-prefix handlers,
handler-error counting, rebind, `unbind_all` (only our matches removed, only
our `::dsCmds` entries dropped), `bindings_json` (escaping, pure ASCII, and
strict parsing with `python3 -I` when available), and both prelude forms:
playback (hooks run and reset, tables cleared, no-op helpers, silent bind,
`stim::inject` delivery and the `::dsVals` line it leaves) and live, against
a local listener standing in for dserv's 4620 (registration order, the
`stimdg` match, the socket, the send helpers and `puts` going through the
qpcs recorders, and the socket closed again by a following playback).
