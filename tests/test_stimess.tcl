#!/usr/bin/env tclsh
# test_stimess.tcl -- unit test for the stimess package (modules/stimess-1.0.tm)
# under plain tclsh, with no stim2 and no dserv:
#
#     tclsh tests/test_stimess.tcl
#
# The qpcs dsStim* procs are stubbed as recorders and stim2's render-thread
# dispatch is imitated exactly: stim2 runs Tcl_VarEval(cmd, " {*}", line),
# i.e. the script "$::dsCmds(name) {*}<line>" where <line> is dserv's
# "<name> <type> <timestamp> <length> {<value>}".
#
# Prints OK/FAIL lines and a summary; exits non-zero on any failure.

set here [file dirname [file normalize [info script]]]
set pkg  [file join $here .. modules stimess-1.0.tm]

set ::fails 0
set ::passes 0
proc check {name cond {detail ""}} {
    if {$cond} {
        incr ::passes
        puts "OK   $name"
    } else {
        incr ::fails
        puts "FAIL $name  $detail"
    }
}

# ---- qpcs stubs -------------------------------------------------------------
namespace eval qpcs {
    variable added {}
    variable removed {}
    variable unregistered 0
    variable fail_next 0
    proc dsStimAddMatch {server pattern {every 1} {port 4620}} {
        variable added; variable fail_next
        if {$fail_next} { set fail_next 0; error "connection refused" }
        lappend added [list $server $pattern]
        return 1
    }
    proc dsStimRemoveMatch {server pattern {port 4620}} {
        variable removed
        lappend removed [list $server $pattern]
        return 1
    }
    proc dsStimUnregister {server {port 4620}} {
        variable unregistered
        incr unregistered
        return 1
    }
}

# What stim2's processDSCommands does for one arriving datapoint line
set ::arrivals 0   ;# lines delivered to a stim::dispatch entry
proc arrive {name value {type 1} {ts 1729000000000000}} {
    set line "$name $type $ts [string length $value] {$value}"
    set ::dsVals($name) $line
    if {[info exists ::dsCmds($name)]} {
        if {[string match "::stim::dispatch *" $::dsCmds($name)]} { incr ::arrivals }
        eval "$::dsCmds($name) {*}$line"
    }
}

source $pkg
check "package provide" [expr {[package present stimess] eq "1.0"}]

# ---- bind without dservhost: recorded, not matched -------------------------
unset -nocomplain ::dservhost
set r [stim::bind ess/dial/pointer {x y show band}]
check "bind without ::dservhost returns 0" [expr {$r == 0}] "got $r"
check "  binding recorded anyway" \
    [expr {[llength [stim::bindings]] == 1}]
check "  no match sent" [expr {[llength $qpcs::added] == 0}]
check "  dispatcher installed" [info exists ::dsCmds(ess/dial/pointer)]
arrive ess/dial/pointer "1,2,1,0"
check "  dispatch works unmatched" [expr {[stim::get ess/dial/pointer x] == 1}]
stim::unbind_all
check "  unbind_all after unmatched bind removes nothing server-side" \
    [expr {[llength $qpcs::removed] == 0}]
check "  dsCmds cleared" [expr {![info exists ::dsCmds(ess/dial/pointer)]}]

# ---- bind with dservhost: pointer-style stream -----------------------------
set ::dservhost 127.0.0.1
set ::dsCmds(unrelated/thing) some_other_proc   ;# must survive unbind_all

set r [stim::bind ess/dial/pointer {x y show band}]
check "bind with ::dservhost returns 1" [expr {$r == 1}] "got $r"
check "  dsStimAddMatch called with host and name" \
    [expr {$qpcs::added eq [list [list 127.0.0.1 ess/dial/pointer]]}] \
    "got $qpcs::added"
check "  ::dsCmds set" [info exists ::dsCmds(ess/dial/pointer)]
check "  get before arrival is empty" \
    [expr {[stim::get ess/dial/pointer x] eq ""}]
check "  count starts at 0" [expr {[stim::count ess/dial/pointer] == 0}]
check "  dict form has all fields empty" \
    [expr {[stim::get ess/dial/pointer] eq {x {} y {} show {} band {}}}] \
    "got {[stim::get ess/dial/pointer]}"

arrive ess/dial/pointer "3.5,-2.25,1,0"
check "pointer line: x" [expr {[stim::get ess/dial/pointer x] eq "3.5"}]
check "pointer line: y" [expr {[stim::get ess/dial/pointer y] eq "-2.25"}]
check "pointer line: show" [expr {[stim::get ess/dial/pointer show] eq "1"}]
check "pointer line: band" [expr {[stim::get ess/dial/pointer band] eq "0"}]
check "pointer line: dict" \
    [expr {[stim::get ess/dial/pointer] eq {x 3.5 y -2.25 show 1 band 0}}]
check "count after one arrival" [expr {[stim::count ess/dial/pointer] == 1}]
check "dispatch_count counts the earlier unmatched arrival too" \
    [expr {[stim::dispatch_count] == 2 && [stim::dispatch_count] == $::arrivals}] \
    "got [stim::dispatch_count]"
check "::dsVals still holds the raw line" \
    [string match "ess/dial/pointer 1 * {3.5,-2.25,1,0}" $::dsVals(ess/dial/pointer)]

arrive ess/dial/pointer "0,0,0,1"
check "latest mode overwrites" \
    [expr {[stim::get ess/dial/pointer] eq {x 0 y 0 show 0 band 1}}]
check "count 2" [expr {[stim::count ess/dial/pointer] == 2}]

# fewer parts than fields -> trailing fields empty; extra parts dropped
arrive ess/dial/pointer "7,8"
check "short value: present fields set" \
    [expr {[stim::get ess/dial/pointer x] eq "7" && [stim::get ess/dial/pointer y] eq "8"}]
check "short value: missing fields empty" \
    [expr {[stim::get ess/dial/pointer show] eq "" && [stim::get ess/dial/pointer band] eq ""}]
arrive ess/dial/pointer "1,2,3,4,5,6"
check "long value: extras dropped" \
    [expr {[stim::get ess/dial/pointer] eq {x 1 y 2 show 3 band 4}}]

# ---- error cases ----------------------------------------------------------
check "get unbound name errors" \
    [catch {stim::get nope/nothing x}]
check "get unknown field errors" \
    [catch {stim::get ess/dial/pointer nope}]
check "count unbound errors" [catch {stim::count nope/nothing}]
check "errors unbound errors" [catch {stim::errors nope/nothing}]
check "bind event without handler errors" \
    [catch {stim::bind ess/x {a b} -mode event}]
check "bind bad mode errors" [catch {stim::bind ess/x {a b} -mode sometimes}]
check "bind unknown option errors" [catch {stim::bind ess/x {a b} -bogus 1}]
check "bind odd options errors" [catch {stim::bind ess/x {a b} -mode}]
check "bind empty fields errors" [catch {stim::bind ess/x {}}]
check "bad binds left no trace" \
    [expr {[llength [stim::bindings]] == 1 && ![info exists ::dsCmds(ess/x)]}]

# ---- single field: whole value unsplit --------------------------------------
stim::bind ess/status/text {text}
arrive ess/status/text "hello, world, with, commas"
check "single field keeps commas" \
    [expr {[stim::get ess/status/text text] eq "hello, world, with, commas"}]
check "single field dict" \
    [expr {[stim::get ess/status/text] eq {text {hello, world, with, commas}}}]

# ---- custom separator -------------------------------------------------------
stim::bind ess/pair {a b} -sep :
arrive ess/pair "left:right,with,commas"
check "custom -sep" \
    [expr {[stim::get ess/pair] eq {a left b right,with,commas}}]

# ---- event mode -------------------------------------------------------------
set ::seen {}
proc on_touch {x y type} { lappend ::seen [list $x $y $type] }
stim::bind ess/touch/event {x y type} -mode event -handler on_touch
arrive ess/touch/event "10,20,0"
arrive ess/touch/event "11,21,2"
check "event handler called once per arrival with field values" \
    [expr {$::seen eq {{10 20 0} {11 21 2}}}] "got {$::seen}"
check "event mode also stores latest" \
    [expr {[stim::get ess/touch/event] eq {x 11 y 21 type 2}}]
check "event count" [expr {[stim::count ess/touch/event] == 2}]
check "no handler errors" [expr {[stim::errors ess/touch/event] == 0}]

# command-prefix handler, single field
set ::words {}
proc on_word {tag w} { lappend ::words $tag $w }
stim::bind ess/word {w} -mode event -handler [list on_word T]
arrive ess/word "spoken, words"
check "handler may be a command prefix; single field unsplit" \
    [expr {$::words eq {T {spoken, words}}}] "got {$::words}"

# handler runs at global level
proc on_glob {a b} { set ::glob_level [info level] ; set ::glob_args [list $a $b] }
stim::bind ess/glob {a b} -mode event -handler on_glob
arrive ess/glob "p,q"
check "handler called from level 0" [expr {$::glob_level == 1}] "level $::glob_level"
check "handler args" [expr {$::glob_args eq {p q}}]

# ---- handler errors are swallowed and counted -------------------------------
proc bad_handler {args} { error "boom: $args" }
stim::bind ess/bad {a b} -mode event -handler bad_handler
set rc [catch {arrive ess/bad "1,2"} err]
check "handler error does not propagate" [expr {$rc == 0}] "got $err"
arrive ess/bad "3,4"
check "handler errors counted" [expr {[stim::errors ess/bad] == 2}]
check "last_error kept" [expr {[stim::last_error ess/bad] eq "boom: 3 4"}] \
    "got {[stim::last_error ess/bad]}"
check "value still stored despite handler error" \
    [expr {[stim::get ess/bad] eq {a 3 b 4}}]
check "arrivals still counted" [expr {[stim::count ess/bad] == 2}]
set rc [catch {arrive ess/bad "5,6"} err]
check "nonexistent handler proc also swallowed" [expr {$rc == 0}]

# ---- dispatch_count counts every dispatched arrival ------------------------
check "dispatch_count == lines delivered to stim::dispatch" \
    [expr {[stim::dispatch_count] == $::arrivals}] \
    "[stim::dispatch_count] vs $::arrivals"
set total 0
foreach b [stim::bindings] { incr total [stim::count [dict get $b name]] }
check "dispatch_count == sum of live counts + the one unbound arrival" \
    [expr {[stim::dispatch_count] == $total + 1}] \
    "[stim::dispatch_count] vs $total + 1"

# ---- rebind resets ----------------------------------------------------------
set r [stim::bind ess/dial/pointer {x y show band}]
check "rebind returns 1" [expr {$r == 1}]
check "rebind resets count" [expr {[stim::count ess/dial/pointer] == 0}]
check "rebind clears values" [expr {[stim::get ess/dial/pointer x] eq ""}]
check "rebind keeps one table entry" \
    [expr {[llength [lsearch -all -inline -index 1 [stim::bindings] ess/dial/pointer]] == 1}]
check "rebind keeps bind order" \
    [expr {[dict get [lindex [stim::bindings] 0] name] eq "ess/dial/pointer"}]

# ---- dsStimAddMatch failure: recorded, returns 0 ----------------------------
set qpcs::fail_next 1
set r [stim::bind ess/flaky {v}]
check "AddMatch failure returns 0" [expr {$r == 0}] "got $r"
check "  but binding recorded" \
    [expr {[lsearch -index 1 [stim::bindings] ess/flaky] >= 0}]
arrive ess/flaky "ok"
check "  and dispatches" [expr {[stim::get ess/flaky v] eq "ok"}]
set n_removed_before [llength $qpcs::removed]
stim::unbind ess/flaky
check "  unbind of unregistered name sends no RemoveMatch" \
    [expr {[llength $qpcs::removed] == $n_removed_before}]

# ---- bindings table ---------------------------------------------------------
set b0 [lindex [stim::bindings] 0]
check "bindings dict has name fields mode sep" \
    [expr {[lsort [dict keys $b0]] eq {fields mode name sep}}]
check "bindings entry for pointer" \
    [expr {$b0 eq {name ess/dial/pointer fields {x y show band} mode latest sep ,}}] \
    "got {$b0}"
set bt [lsearch -inline -index 1 [stim::bindings] ess/touch/event]
check "bindings entry for event stream" \
    [expr {[dict get $bt mode] eq "event" && [dict get $bt fields] eq {x y type}}]

# ---- bindings_json ----------------------------------------------------------
# a name and field that need escaping
stim::bind {ess/we"ird\name} [list plain "tab\there" "é"]

set js [stim::bindings_json]
check "json starts with package tag" \
    [string match {\{"package":"stimess 1.0","bindings":\[*\]\}} $js] "got $js"
check "json is pure ASCII" [regexp {^[\x20-\x7e]*$} $js]
check "json escapes quote and backslash" \
    [expr {[string first {"name":"ess/we\"ird\\name"} $js] >= 0}] "got $js"
# needles spelled with "\\": Tcl 9.1 substitutes \uXXXX even inside braces
check "json escapes control and non-ASCII chars" \
    [expr {[string first "\"tab\\there\"" $js] >= 0 && [string first "\"\\u00e9\"" $js] >= 0}] \
    "got $js"

proc json_check {js} {
    # Validate with python's strict parser when available; the structural
    # checks above still run without it.
    set py [auto_execok python3]
    if {$py eq ""} { return skip }
    set script {
import json, sys
d = json.loads(sys.stdin.read())
assert d["package"] == "stimess 1.0"
assert isinstance(d["bindings"], list)
for b in d["bindings"]:
    assert set(b) == {"name", "fields", "mode", "sep"}
    assert isinstance(b["fields"], list) and all(isinstance(f, str) for f in b["fields"])
    assert b["mode"] in ("latest", "event")
# echo back the escaped entry, one item per line, so Tcl can compare the
# decoded strings (one field holds a tab, so no tab separator)
w = [b for b in d["bindings"] if b["name"] == 'ess/we"ird\\name']
print(len(d["bindings"]), len(w), *(w[0]["fields"] if w else []), sep="\n")
    }
    set f [file join [file dirname [file normalize [info script]]] .json_check.py]
    set fd [open $f w]; puts $fd $script; close $fd
    set rc [catch {exec {*}$py -I $f << $js} out]
    file delete $f
    if {$rc} { return [list fail $out] }
    return [list ok $out]
}

lassign [json_check $js] st out
if {$st eq "skip"} {
    puts "SKIP python3 JSON validation (python3 not found)"
} else {
    check "python json.loads accepts bindings_json" [expr {$st eq "ok"}] $out
    if {$st eq "ok"} {
        set exp [list [llength [stim::bindings]] 1 plain "tab\there" "é"]
        check "  decoded escaped name/fields round-trip" \
            [expr {[split $out \n] eq $exp}] "got {$out}"
    }
}

# ---- unbind_all -------------------------------------------------------------
set names {}
foreach b [stim::bindings] { lappend names [dict get $b name] }
set qpcs::removed {}
stim::unbind_all
check "unbind_all empties the table" [expr {[llength [stim::bindings]] == 0}]
set removed_names {}
foreach r $qpcs::removed { lappend removed_names [lindex $r 1] }
check "unbind_all sends RemoveMatch for each registered name" \
    [expr {[lsort $removed_names] eq [lsort $names]}] \
    "removed {$removed_names} expected {$names}"
check "unbind_all never calls dsStimUnregister (would drop the whole client)" \
    [expr {$qpcs::unregistered == 0}]
set leftover {}
foreach n $names { if {[info exists ::dsCmds($n)]} { lappend leftover $n } }
check "unbind_all drops our ::dsCmds entries" [expr {$leftover eq {}}] "left {$leftover}"
check "unbind_all leaves unrelated ::dsCmds alone" \
    [expr {[info exists ::dsCmds(unrelated/thing)] && $::dsCmds(unrelated/thing) eq "some_other_proc"}]
check "bindings_json empty table" \
    [expr {[stim::bindings_json] eq {{"package":"stimess 1.0","bindings":[]}}}] \
    "got [stim::bindings_json]"
lassign [json_check [stim::bindings_json]] st out
if {$st ne "skip"} { check "python accepts empty table" [expr {$st eq "ok"}] $out }
check "get after unbind errors" [catch {stim::get ess/dial/pointer x}]
check "unbind of unknown name returns 0" [expr {[stim::unbind nope] == 0}]
check "dispatch_count persists across unbind_all" [expr {[stim::dispatch_count] > 0}]

# a late datapoint for an unbound name whose dsCmds entry someone kept must
# not throw either
set ::dsCmds(ess/dial/pointer) [list ::stim::dispatch ess/dial/pointer]
set rc [catch {arrive ess/dial/pointer "1,2,3,4"} err]
check "stale dispatcher entry is a no-op" [expr {$rc == 0}] "got $err"
unset ::dsCmds(ess/dial/pointer)

# a script that replaced our dsCmds entry keeps it through unbind
stim::bind ess/mine {a}
set ::dsCmds(ess/mine) their_proc
stim::unbind ess/mine
check "unbind leaves a foreign ::dsCmds entry for our name alone" \
    [expr {[info exists ::dsCmds(ess/mine)] && $::dsCmds(ess/mine) eq "their_proc"}]
unset ::dsCmds(ess/mine)

puts "\n$::passes passed, $::fails failed"
exit [expr {$::fails ? 1 : 0}]
