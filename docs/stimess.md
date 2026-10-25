# stimess: dserv datapoint bindings for stim scripts

`stimess` is a pure-Tcl package shipped with stim2 (`modules/stimess-1.0.tm`,
installed beside `workspace` and `stwidget`; `package require stimess` works
in the app). It replaces the per-protocol copies of "subscribe to a dserv
datapoint, split its value into globals, read them from the frame script"
with one declarative call, and exposes the binding table so ESS can log the
bound streams into the data file and a replay can install the same bindings.

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
| `stim::bindings_json` | The same as JSON, pure ASCII: `{"package":"stimess 1.0","bindings":[{"name":"ess/dial/pointer","fields":["x","y","show","band"],"mode":"latest","sep":","}]}`; `"bindings":[]` when nothing is bound. |

Only `dsStimRemoveMatch` is used to undo a binding. `dsStimUnregister` would
drop stim2's whole client registration, including matches a script made on
its own, so the package never calls it.

## Test

`tests/test_stimess.tcl` runs under plain `tclsh` with the qpcs procs stubbed
as recorders and stim2's dispatch imitated (`$::dsCmds(name) {*}<line>`):

    tclsh tests/test_stimess.tcl

It covers bind with and without `::dservhost`, a pointer-style line, `get`
in both forms, counts, event mode with plain and command-prefix handlers,
handler-error counting, rebind, `unbind_all` (only our matches removed, only
our `::dsCmds` entries dropped), and `bindings_json` (escaping, pure ASCII,
and strict parsing with `python3 -I` when available).
