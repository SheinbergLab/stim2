#!/usr/bin/env dlsh
# replay.tcl -- replay the stim2 side of a dserv .ess data file into a live
# stim2, standing in for ESS.
#
#     dlsh tools/replay/replay.tcl FILE.ess ?options?
#
#       -host H        stim2 host (default localhost)
#       -port P        stim2 message port (default 5612: a stim2 started with
#                      --port-base 5610, beside any live one). Port 4612 is
#                      the live display's and is refused without -live.
#       -live          allow -port 4612 (you really do mean the live stim2)
#       -trial N       play one trial: the Nth observation period, 1-based,
#                      with the setup commands that preceded it
#       -trials A-B    play trials A through B
#       -speed X       playback rate (default 1.0; 0 = as fast as stim2 takes
#                      the commands)
#       -gap S         longest pause to keep between two events, seconds
#                      (default 1.0): idle time between trials is compressed
#                      to this
#       -stream NAME   also inject records named NAME (repeatable); the
#                      streams in the file's stim/bind table are always
#                      injected
#       -dry           read and plan, print the timeline summary, send nothing
#       -verbose       print every event as it is sent
#
# What it does, in the order ESS's configure_stim does it:
#
#   1. stim::prelude playback         (needs stimess 1.1 in the stim2)
#   2. source each stim/lib/<name> body, then the stim/script body, then
#      stim_init -- the exact text the rig ran
#   3. stim::inject stimdg <base64> 6 (the trial table as logged)
#   4. the rig's screen geometry: screen_set HalfScreenDegreeX/Y from the
#      file's ess/screen_halfx/halfy, so degrees map onto this window the
#      way they mapped onto the rig's display (start stim2 with -w/-h in the
#      rig's aspect for the picture to match; the script warns otherwise)
#   5. walk the timeline: every stim/cmd goes out as the command text that
#      was sent, every record of a bound stream goes in with stim::inject,
#      in dserv-timestamp order, paced by those timestamps
#
# The stim script therefore runs unchanged: its bindings fill from the
# injected records, its frame script draws the cursor, its procs answer the
# commands. What differs from the rig is the display's refresh and pixel
# size, and that nothing leaves stim2. The window is retitled with the file
# and trials (setWindowTitle, stim2 >= 0.28.2) so it cannot be taken for a
# live display.
#
# Needs dlsh with the dslog and tcljson packages (dlsh.zip 0.20.4 or newer)
# and a stim2 with stimess 1.1 or newer. Never touch a stim2 that is driving
# an experiment: start one with --port-base 5610 (the default -port here).

package require dslog
package require tcljson

# ---------------------------------------------------------------------------
# options
# ---------------------------------------------------------------------------
set opt(host)    localhost
set opt(port)    5612
set opt(live)    0
set opt(trial)   ""
set opt(speed)   1.0
set opt(gap)     1.0
set opt(streams) {}
set opt(dry)     0
set opt(verbose) 0

if {[llength $argv] < 1} {
    puts stderr "usage: dlsh replay.tcl FILE.ess ?-host H? ?-port P? ?-live? ?-trial N | -trials A-B?\
 ?-speed X? ?-gap S? ?-stream NAME ...? ?-dry? ?-verbose?"
    exit 2
}
set path [lindex $argv 0]
set args [lrange $argv 1 end]
while {[llength $args]} {
    set a [lindex $args 0]; set args [lrange $args 1 end]
    switch -- $a {
        -host    { set opt(host)  [lindex $args 0]; set args [lrange $args 1 end] }
        -port    { set opt(port)  [lindex $args 0]; set args [lrange $args 1 end] }
        -live    { set opt(live) 1 }
        -trial   { set v [lindex $args 0]; set args [lrange $args 1 end]; set opt(trial) [list $v $v] }
        -trials  { set v [lindex $args 0]; set args [lrange $args 1 end]
                   if {![regexp {^(\d+)-(\d+)$} $v -> a b]} { puts stderr "-trials wants A-B"; exit 2 }
                   set opt(trial) [list $a $b] }
        -speed   { set opt(speed) [lindex $args 0]; set args [lrange $args 1 end] }
        -gap     { set opt(gap)   [lindex $args 0]; set args [lrange $args 1 end] }
        -stream  { lappend opt(streams) [lindex $args 0]; set args [lrange $args 1 end] }
        -dry     { set opt(dry) 1 }
        -verbose { set opt(verbose) 1 }
        default  { puts stderr "unknown option $a"; exit 2 }
    }
}
# 4612 is where ESS drives the rig's display; a replay sent there would
# draw over an experiment. A second stim2 (--port-base 5610) is the target.
if {$opt(port) == 4612 && !$opt(live)} {
    puts stderr "replay: port 4612 is the live display's message port; start a stim2 with\
 --port-base 5610 and use -port 5612 (the default), or pass -live if you mean it"
    exit 2
}

# ---------------------------------------------------------------------------
# stim2 message port: 4-byte big-endian length, payload, same framing back
# ---------------------------------------------------------------------------
proc stim_open {host port} {
    set s [socket $host $port]
    fconfigure $s -translation binary -buffering none
    return $s
}
proc stim_send {sock cmd} {
    puts -nonewline $sock [binary format I [string length $cmd]]
    puts -nonewline $sock $cmd
    flush $sock
    set hdr [read $sock 4]
    if {[string length $hdr] != 4} { error "stim2 closed the connection" }
    binary scan $hdr I n
    set n [expr {$n & 0xffffffff}]
    return [expr {$n ? [read $sock $n] : ""}]
}
proc is_error {reply} { string match "!TCL_ERROR*" $reply }

# ---------------------------------------------------------------------------
# read the file
# ---------------------------------------------------------------------------
set script ""; set script_path ""; set libs {}; set bind_names {}
set stimdg ""; set obs_begin {}; set obs_end {}
set events {}          ;# {ts kind payload...}, kinds: cmd | dp
set counts [dict create]
set hashed 0
set stream_set [dict create]
foreach s $opt(streams) { dict set stream_set $s 1 }

set h [dslog::open $path r]
# first pass: header-ish records (any order in the file; keep the last)
# and the bound-stream names, which decide what the second pass keeps
set records {}
while {1} {
    set d [dslog::next $h]
    if {$d eq ""} break
    set nm [dict get $d varname]
    dict incr counts $nm
    switch -glob -- $nm {
        stim/script {
            set j [json_to_dict [dict get $d data]]
            if {[dict get $j body] ne ""} {
                set script [dict get $j body]; set script_path [dict get $j path]
                set script_meta $j
            }
        }
        stim/lib/* {
            set j [json_to_dict [dict get $d data]]
            dict set libs [dict get $j name] [dict get $j body]
        }
        stim/bind {
            set j [json_to_dict [dict get $d data]]
            set bind_names {}
            foreach b [dict get $j bindings] { lappend bind_names [dict get $b name] }
        }
        stimdg { set stimdg [dict get $d data] }
        ess/screen_halfx { set geom(halfx) [string trim [dict get $d data]] }
        ess/screen_halfy { set geom(halfy) [string trim [dict get $d data]] }
        ess/screen_w     { set geom(w)     [string trim [dict get $d data]] }
        ess/screen_h     { set geom(h)     [string trim [dict get $d data]] }
        ess/screen_refresh_rate { set geom(hz) [string trim [dict get $d data]] }
        logger:beginobs { lappend obs_begin [dict get $d timestamp] }
        logger:endobs   { lappend obs_end   [dict get $d timestamp] }
        stim/cmd {
            set j [json_to_dict [dict get $d data]]
            if {![dict exists $j cmd]} { incr hashed; continue }
            lappend events [list [dict get $j t_send] cmd [dict get $j cmd] [dict get $j seq]]
        }
        default {
            # stream records: kept for now, filtered once bind_names is known
            if {[string match "ess/*" $nm] || [dict exists $stream_set $nm]} {
                lappend records [list [dict get $d timestamp] $nm [dict get $d type] [dict get $d data]]
            }
        }
    }
}
dslog::close $h

foreach n $bind_names { dict set stream_set $n 1 }
foreach r $records {
    lassign $r ts nm type data
    if {[dict exists $stream_set $nm]} { lappend events [list $ts dp $nm $type $data] }
}
unset records
set events [lsort -integer -index 0 $events]

if {$script eq ""} { puts stderr "no stim/script with a body in $path -- nothing to source"; exit 1 }
if {$stimdg eq ""} { puts stderr "no stimdg record in $path"; exit 1 }
if {[llength $events] == 0} { puts stderr "no stim/cmd or bound-stream records in $path"; exit 1 }

# ---------------------------------------------------------------------------
# trial windows: trial i is (endobs_{i-1}, endobs_i], so the commands that
# set the trial up (nexttrial, targets_on) travel with it
# ---------------------------------------------------------------------------
set ntrials [llength $obs_end]
set t_first [lindex $events 0 0]
set t_last  [lindex $events end 0]
if {$opt(trial) ne ""} {
    lassign $opt(trial) a b
    if {$a < 1 || $b > $ntrials || $a > $b} {
        puts stderr "trials $a-$b out of range: the file has $ntrials observation periods"; exit 1
    }
    set w0 [expr {$a == 1 ? $t_first - 1 : [lindex $obs_end [expr {$a - 2}]]}]
    set w1 [lindex $obs_end [expr {$b - 1}]]
    set sel {}
    foreach e $events { set ts [lindex $e 0]; if {$ts > $w0 && $ts <= $w1} { lappend sel $e } }
    set events $sel
    set span_note "trials $a-$b"
} else {
    set span_note "all $ntrials trials"
}
set ncmd 0; set ndp 0
foreach e $events { if {[lindex $e 1] eq "cmd"} { incr ncmd } else { incr ndp } }

puts "replay: $path"
puts "  script  $script_path ([string length $script] bytes), libs: [expr {[dict size $libs] ? [dict keys $libs] : "none"}]"
puts "  stimdg  [string length $stimdg] bytes; bound streams: [expr {[llength $bind_names] ? $bind_names : "none"}][expr {[llength $opt(streams)] ? " + $opt(streams)" : ""}]"
if {[info exists geom(halfx)] && [info exists geom(halfy)]} {
    puts "  screen  rig: +/-$geom(halfx) x +/-$geom(halfy) deg[expr {[info exists geom(w)] ? ", $geom(w)x$geom(h) px" : ""}][expr {[info exists geom(hz)] ? ", $geom(hz) Hz" : ""}]"
} else {
    puts "  screen  no ess/screen_* records: this stim2's own degree mapping will be used"
}
puts "  file    [format %.1f [expr {($t_last - $t_first)/6e7}]] min, $ntrials observation periods, [dict get $counts stim/cmd] commands ($hashed hash-only, skipped)"
puts "  playing $span_note: $ncmd commands, $ndp datapoints, span [format %.1f [expr {([lindex $events end 0] - [lindex $events 0 0])/1e6}]] s, speed $opt(speed), gaps capped at $opt(gap) s"
if {[llength $bind_names] == 0 && [llength $opt(streams)] == 0} {
    puts "  NOTE: the stim/bind table is empty (a script from before stim::bind): no datapoints will\
 be injected, so a cursor driven by them will not move. Use -stream to name the records anyway."
}
if {$opt(dry)} {
    set shown 0
    foreach e $events {
        if {[incr shown] > 25} { puts "  ..."; break }
        lassign $e ts kind p1 p2 p3
        if {$kind eq "cmd"} { puts [format "  %+9.3f s  cmd  %s" [expr {($ts - [lindex $events 0 0])/1e6}] [string range $p1 0 70]] } \
        else { puts [format "  %+9.3f s  dp   %s = %s" [expr {($ts - [lindex $events 0 0])/1e6}] $p1 [string range $p3 0 40]] }
    }
    exit 0
}

# ---------------------------------------------------------------------------
# connect and set up, as configure_stim does
# ---------------------------------------------------------------------------
set sock [stim_open $opt(host) $opt(port)]
set v [stim_send $sock {package require stimess}]
if {[is_error $v] || [catch {package vcompare $v 1.1} c] || $c < 0} {
    puts stderr "stim2 at $opt(host):$opt(port) has no stimess 1.1 (got: $v); it cannot take a playback prelude"
    exit 1
}
puts "  stim2   [stim_send $sock {catch {stimVersion} v; set v}], stimess $v"
set r [stim_send $sock {stim::prelude playback}]
if {[is_error $r]} { puts stderr "prelude failed: $r"; exit 1 }
dict for {name body} $libs {
    stim_send $sock "catch {package forget $name}"
    set r [stim_send $sock $body]
    if {[is_error $r]} { puts stderr "library $name failed to load: $r"; exit 1 }
    puts "  sourced lib $name"
}
set r [stim_send $sock $script]
if {[is_error $r]} { puts stderr "stim script failed to load: $r"; exit 1 }
stim_send $sock {catch {stim_init}}
puts "  sourced [file tail $script_path]; bindings: [stim_send $sock {stim::bindings_json}]"
set r [stim_send $sock [list stim::inject stimdg [binary encode base64 $stimdg] 6]]
if {[is_error $r]} { puts stderr "stimdg inject failed: $r"; exit 1 }
puts "  stimdg loaded: [stim_send $sock {dl_length stimdg:stimtype}] trials"

# the rig's degree mapping onto this window; the pixel size is stim2's own
# (-w/-h at startup), so say when the aspect does not match the rig's
set win_w [stim_send $sock {screen_set WinWidth}]
set win_h [stim_send $sock {screen_set WinHeight}]
if {[info exists geom(halfx)] && [info exists geom(halfy)]} {
    set r1 [stim_send $sock [list screen_set HalfScreenDegreeX $geom(halfx)]]
    set r2 [stim_send $sock [list screen_set HalfScreenDegreeY $geom(halfy)]]
    if {[is_error $r1] || [is_error $r2]} {
        puts "  WARNING: could not set the rig's degree mapping: $r1 $r2"
    } else {
        puts "  geometry: +/-$geom(halfx) x +/-$geom(halfy) deg onto this ${win_w}x${win_h} window"
        if {[info exists geom(w)] && $win_h > 0 && $geom(h) > 0} {
            set rig_aspect [expr {double($geom(w)) / $geom(h)}]
            set win_aspect [expr {double($win_w) / $win_h}]
            if {abs($rig_aspect - $win_aspect) / $rig_aspect > 0.05} {
                puts [format "  WARNING: window aspect %.2f vs rig %.2f -- circles will be ellipses;\
 start stim2 with -w %d -h %d (or any %dx%d multiple)" $win_aspect $rig_aspect \
                          [expr {int($geom(w)/2)}] [expr {int($geom(h)/2)}] $geom(w) $geom(h)]
            }
        }
    }
}
# label the window; an older stim2 has no setWindowTitle, which is fine
catch { stim_send $sock [list setWindowTitle "replay: [file tail $path] ($span_note)"] }

if {$opt(trial) ne "" && [lindex $opt(trial) 0] > 1} { stim_send $sock reset }

# ---------------------------------------------------------------------------
# the timeline
# ---------------------------------------------------------------------------
set skip_prefixes {stim::prelude stimFrameLog {set dservhost}}
set t0 [lindex $events 0 0]
set wall0 [clock microseconds]
set offset 0.0            ;# recorded time removed by gap compression, us
set sent 0; set errors 0; set skipped 0; set prev_ts $t0
set swap0 [stim_send $sock {set SwapCount}]
foreach e $events {
    set ts [lindex $e 0]
    # pacing: recorded delta, compressed, scaled
    set dt [expr {$ts - $prev_ts}]
    if {$dt > $opt(gap) * 1e6} { set offset [expr {$offset + $dt - $opt(gap) * 1e6}] }
    set prev_ts $ts
    if {$opt(speed) > 0} {
        set due [expr {$wall0 + ($ts - $t0 - $offset) / $opt(speed)}]
        set wait [expr {int(($due - [clock microseconds]) / 1000)}]
        if {$wait > 0} { after $wait }
    }
    lassign $e _ kind p1 p2 p3
    if {$kind eq "cmd"} {
        set skip 0
        foreach pre $skip_prefixes { if {[string match "$pre*" $p1]} { set skip 1 } }
        if {$skip} { incr skipped; continue }
        set r [stim_send $sock $p1]
        if {[is_error $r]} { incr errors; puts "  ERROR [string range $p1 0 60] -> $r" } \
        elseif {$opt(verbose)} { puts [format "  %+8.3f cmd %s" [expr {($ts - $t0)/1e6}] [string range $p1 0 70]] }
    } else {
        set r [stim_send $sock [list stim::inject $p1 $p3 $p2 $ts]]
        if {[is_error $r]} { incr errors; puts "  ERROR inject $p1 -> $r" } \
        elseif {$opt(verbose)} { puts [format "  %+8.3f dp  %s = %s" [expr {($ts - $t0)/1e6}] $p1 $p3] }
    }
    incr sent
}
set elapsed [expr {([clock microseconds] - $wall0)/1e6}]
set swaps [expr {[stim_send $sock {set SwapCount}] - $swap0}]
puts "done: $sent events in [format %.1f $elapsed] s ($errors errors, $skipped configure-time commands skipped),\
 stim2 swapped $swaps frames"
foreach n [dict keys $stream_set] {
    catch { puts "  $n: [stim_send $sock [list stim::count $n]] injected, last [stim_send $sock [list stim::get $n]]" }
}
close $sock
exit [expr {$errors ? 1 : 0}]
