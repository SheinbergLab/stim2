# stimess-1.0.tm
# Declarative bindings from dserv datapoints to stim2 script state.
#
# A stim script running inside stim2 receives dserv datapoints through the
# ds socket: stim2 registers itself as a datapoint client (dsStimRegister),
# asks for the names it wants (dsStimAddMatch), and the render thread then
# evaluates ::dsCmds(<name>) with the arriving line's words as arguments.
# Every protocol used to carry its own copy of that plumbing (a *_subscribe
# helper, a handler that splits the value into globals, a frame script that
# reads them). This package is the one copy: a script declares WHICH
# datapoint it wants and what the fields are called, and reads them back.
# ESS can ask stim2 for the binding table (stim::bindings_json) and log the
# bound streams into the data file, and a replay can install the same
# bindings from that table.
#
# Usage inside a stim script:
#
#   package require stimess
#   stim::bind ess/dial/pointer {x y show band}      ;# mode latest
#   ...
#   proc frame {args} {
#       if {[stim::get ess/dial/pointer show]} {
#           translateObj $::cursor [stim::get ess/dial/pointer x] \
#                                  [stim::get ess/dial/pointer y]
#       }
#   }
#
#   stim::bind ess/touch/event {x y type} -mode event -handler on_touch
#
# API (all in namespace ::stim)
#
#   stim::bind name fields ?-mode latest|event? ?-sep ,? ?-handler proc?
#       Registers dserv forwarding for datapoint `name`
#       (qpcs::dsStimAddMatch $::dservhost $name), installs the dispatcher as
#       ::dsCmds($name) and records the binding. `fields` is a Tcl list of
#       field names; each arriving value (word 4 of the line) is split on
#       -sep (default ",") into those fields. With exactly one field the
#       whole value is stored unsplit. Missing trailing parts read as "";
#       extra parts are dropped.
#         -mode latest  (default) store only; read with stim::get.
#         -mode event   store, then call -handler with the field values as
#                       arguments, once per arrival. -handler is required.
#       Returns 1 when the match was registered with dserv, 0 when it was
#       not (::dservhost unset, or dsStimAddMatch failed); the binding is
#       recorded and dispatches locally either way. Rebinding a name
#       replaces its binding and resets its counters.
#
#   stim::get name ?field?
#       Latest value of `field` ("" before anything arrived); error if
#       `name` is not bound. Without `field`: a dict of all fields.
#
#   stim::count name          arrivals for `name` since it was bound
#   stim::dispatch_count      arrivals across all bindings (the watermark a
#                             per-frame record can carry)
#   stim::errors name         handler errors swallowed for `name`
#   stim::last_error name     text of the most recent swallowed error ("")
#
#   stim::unbind name         remove the match (qpcs::dsStimRemoveMatch, only
#                             if this package registered it), drop
#                             ::dsCmds($name) and the binding
#   stim::unbind_all          the same for every name this package bound;
#                             unrelated ::dsCmds entries are left alone.
#                             ESS calls this at protocol reload.
#
#   stim::bindings            list of dicts {name fields mode sep}
#   stim::bindings_json       the same as JSON:
#       {"package":"stimess 1.0","bindings":[{"name":"ess/dial/pointer",
#        "fields":["x","y","show","band"],"mode":"latest","sep":","}]}
#       ("bindings":[] when nothing is bound). Pure ASCII output.
#
# The dispatcher runs on stim2's render thread for every arriving datapoint,
# so it does the minimum: one lindex, one split, one foreach over the fields,
# two incrs, and in event mode one guarded handler call. It never raises:
# handler errors are caught and counted in stim::errors.
#
# Nothing in the existing dsStimAddMatch + ::dsCmds path changes; scripts
# that set ::dsCmds themselves keep working beside this package.

package provide stimess 1.0

namespace eval ::stim {
    # Per-binding tables, keyed by datapoint name
    variable fields          ;# fields($name)  -> list of field names
    variable mode            ;# mode($name)    -> latest | event
    variable sep             ;# sep($name)     -> separator string
    variable handler         ;# handler($name) -> command prefix (event mode)
    variable registered      ;# registered($name) -> 1 if we added the match
    variable count           ;# count($name)   -> arrivals since bind
    variable errors          ;# errors($name)  -> swallowed handler errors
    variable last_error      ;# last_error($name) -> message of the last one
    variable vals            ;# vals($name,$field) -> latest value
    array set fields {}
    array set mode {}
    array set sep {}
    array set handler {}
    array set registered {}
    array set count {}
    array set errors {}
    array set last_error {}
    array set vals {}

    variable order {}        ;# bound names, in bind order
    variable dispatch_count 0

    namespace export bind get count dispatch_count errors last_error \
        unbind unbind_all bindings bindings_json
}

# ---------------------------------------------------------------------------
# Hot path: ::dsCmds($name) is "::stim::dispatch $name", so stim2 evaluates
#   ::stim::dispatch <name> {*}<line>
# where <line> is "<name> <type> <timestamp> <length> {<value>}" -- the value
# is word 4 of the expanded line, i.e. [lindex $args 4] here.
# ---------------------------------------------------------------------------
proc ::stim::dispatch {name args} {
    variable fields
    variable vals
    variable count
    variable dispatch_count

    if {![info exists fields($name)]} return   ;# stale ::dsCmds entry

    set v [lindex $args 4]
    incr count($name)
    incr dispatch_count

    set flist $fields($name)
    if {[llength $flist] == 1} {
        set vals($name,[lindex $flist 0]) $v
        set parts [list $v]
    } else {
        variable sep
        set parts [split $v $sep($name)]
        foreach f $flist x $parts { set vals($name,$f) $x }
    }

    variable mode
    if {$mode($name) eq "event"} {
        variable handler
        if {[catch {uplevel #0 [linsert $parts 0 {*}$handler($name)]} err]} {
            variable errors
            variable last_error
            incr errors($name)
            set last_error($name) $err
        }
    }
    return
}

# ---------------------------------------------------------------------------
# Binding management
# ---------------------------------------------------------------------------
proc ::stim::bind {name flist args} {
    variable fields
    variable mode
    variable sep
    variable handler
    variable registered
    variable count
    variable errors
    variable last_error
    variable vals
    variable order

    if {$name eq ""} { error "stim::bind: empty datapoint name" }
    if {[llength $flist] == 0} {
        error "stim::bind $name: fields must name at least one field"
    }
    foreach f $flist {
        if {$f eq ""} { error "stim::bind $name: empty field name" }
    }
    if {[llength $args] % 2} {
        error "stim::bind $name: option \"[lindex $args end]\" needs a value"
    }

    set m   latest
    set s   ,
    set h   ""
    foreach {opt val} $args {
        switch -- $opt {
            -mode    { set m $val }
            -sep     { set s $val }
            -handler { set h $val }
            default  { error "stim::bind $name: unknown option \"$opt\"" }
        }
    }
    if {$m ne "latest" && $m ne "event"} {
        error "stim::bind $name: -mode must be latest or event, not \"$m\""
    }
    if {$m eq "event" && $h eq ""} {
        error "stim::bind $name: -mode event needs -handler"
    }
    if {$s eq ""} { error "stim::bind $name: -sep must not be empty" }

    # Rebinding: drop old field slots so the dict from [get name] is clean
    if {[info exists fields($name)]} {
        foreach f $fields($name) { unset -nocomplain vals($name,$f) }
        set was_registered $registered($name)
    } else {
        lappend order $name
        set was_registered 0
    }

    set fields($name)     $flist
    set mode($name)       $m
    set sep($name)        $s
    set handler($name)    $h
    set count($name)      0
    set errors($name)     0
    set last_error($name) ""
    foreach f $flist { set vals($name,$f) "" }

    # Dispatcher first, so a datapoint that lands right after the match is
    # accepted never finds the slot empty
    set ::dsCmds($name) [list ::stim::dispatch $name]

    # Same handling the protocol scripts use: no dservhost -> record only
    set registered($name) $was_registered
    if {![info exists ::dservhost]} {
        puts "stimess: dservhost not set, $name not subscribed"
        return 0
    }
    if {[catch { qpcs::dsStimAddMatch $::dservhost $name } err]} {
        puts "stimess: subscribe $name failed: $err"
        return 0
    }
    set registered($name) 1
    return 1
}

proc ::stim::unbind {name} {
    variable fields
    variable mode
    variable sep
    variable handler
    variable registered
    variable count
    variable errors
    variable last_error
    variable vals
    variable order

    if {![info exists fields($name)]} { return 0 }

    # Only undo what we did: the server match if we added it, and our own
    # dispatcher if it is still the one installed
    if {$registered($name) && [info exists ::dservhost]} {
        catch { qpcs::dsStimRemoveMatch $::dservhost $name }
    }
    if {[info exists ::dsCmds($name)]
        && $::dsCmds($name) eq [list ::stim::dispatch $name]} {
        unset ::dsCmds($name)
    }

    foreach f $fields($name) { unset -nocomplain vals($name,$f) }
    unset fields($name) mode($name) sep($name) handler($name) \
        registered($name) count($name) errors($name) last_error($name)
    set idx [lsearch -exact $order $name]
    if {$idx >= 0} { set order [lreplace $order $idx $idx] }
    return 1
}

proc ::stim::unbind_all {} {
    variable order
    foreach name $order { unbind $name }
    return
}

# ---------------------------------------------------------------------------
# Reading
# ---------------------------------------------------------------------------
proc ::stim::get {name {field {}}} {
    variable fields
    variable vals
    if {![info exists fields($name)]} {
        error "stim::get: \"$name\" is not bound"
    }
    if {$field eq ""} {
        set d [dict create]
        foreach f $fields($name) { dict set d $f $vals($name,$f) }
        return $d
    }
    if {![info exists vals($name,$field)]} {
        error "stim::get: \"$name\" has no field \"$field\""
    }
    return $vals($name,$field)
}

proc ::stim::count {name} {
    variable count
    if {![info exists count($name)]} {
        error "stim::count: \"$name\" is not bound"
    }
    return $count($name)
}

proc ::stim::dispatch_count {} {
    variable dispatch_count
    return $dispatch_count
}

proc ::stim::errors {name} {
    variable errors
    if {![info exists errors($name)]} {
        error "stim::errors: \"$name\" is not bound"
    }
    return $errors($name)
}

proc ::stim::last_error {name} {
    variable last_error
    if {![info exists last_error($name)]} {
        error "stim::last_error: \"$name\" is not bound"
    }
    return $last_error($name)
}

# ---------------------------------------------------------------------------
# Binding table
# ---------------------------------------------------------------------------
proc ::stim::bindings {} {
    variable order
    variable fields
    variable mode
    variable sep
    set result {}
    foreach name $order {
        lappend result [dict create name $name fields $fields($name) \
                            mode $mode($name) sep $sep($name)]
    }
    return $result
}

# JSON string literal, pure ASCII: everything outside 0x20..0x7e is escaped,
# non-BMP characters as surrogate pairs.
proc ::stim::json_quote {s} {
    set out "\""
    foreach ch [split $s ""] {
        set c [scan $ch %c]
        if {$c == 0x22} {
            append out {\"}
        } elseif {$c == 0x5c} {
            append out {\\}
        } elseif {$c == 0x08} {
            append out {\b}
        } elseif {$c == 0x0c} {
            append out {\f}
        } elseif {$c == 0x0a} {
            append out {\n}
        } elseif {$c == 0x0d} {
            append out {\r}
        } elseif {$c == 0x09} {
            append out {\t}
        } elseif {$c < 0x20 || $c > 0x7e} {
            # "\\u" (not a braced {\u}): Tcl 9.1 substitutes \uXXXX inside
            # braces, so spell the backslash out
            if {$c > 0xffff} {
                set c [expr {$c - 0x10000}]
                append out [format "\\u%04x\\u%04x" \
                                [expr {0xd800 | ($c >> 10)}] \
                                [expr {0xdc00 | ($c & 0x3ff)}]]
            } else {
                append out [format "\\u%04x" $c]
            }
        } else {
            append out $ch
        }
    }
    append out "\""
    return $out
}

proc ::stim::bindings_json {} {
    set items {}
    foreach b [bindings] {
        set flds {}
        foreach f [dict get $b fields] { lappend flds [json_quote $f] }
        lappend items [format {{"name":%s,"fields":[%s],"mode":%s,"sep":%s}} \
                           [json_quote [dict get $b name]] \
                           [join $flds ,] \
                           [json_quote [dict get $b mode]] \
                           [json_quote [dict get $b sep]]]
    }
    return [format {{"package":"stimess 1.0","bindings":[%s]}} [join $items ,]]
}
