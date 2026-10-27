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
#       -render OUT    frame-locked: draw each of the rig's frames (stim/frame)
#                      in step mode and write a movie on the rig's timeline.
#                      OUT ending .mp4/.mov/.mkv goes through ffmpeg; anything
#                      else is raw RGBA to a file or "|command". -speed is
#                      ignored; -gap compresses only when given. Needs a stim2
#                      with stepFrame (headless or not) and a file with
#                      stim/frame and ACK-stamped stim/cmd records.
#       -fps F         the movie's frame rate (default: the rig's refresh; above
#                      60, QuickTime Player slows down -- -fps 60 for it)
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
#      in dserv-timestamp order, paced by those timestamps -- or, with
#      -render, frame by frame on the rig's clock (see play_locked below)
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
set opt(render)  ""
set opt(fps)     ""
set opt(gap_given) 0

if {[llength $argv] < 1} {
    puts stderr "usage: dlsh replay.tcl FILE.ess ?-host H? ?-port P? ?-live? ?-trial N | -trials A-B?\
 ?-speed X? ?-gap S? ?-stream NAME ...? ?-dry? ?-verbose? ?-render OUT? ?-fps F?"
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
        -gap     { set opt(gap)   [lindex $args 0]; set args [lrange $args 1 end]; set opt(gap_given) 1 }
        -render  { set opt(render) [lindex $args 0]; set args [lrange $args 1 end] }
        -fps     { set opt(fps)   [lindex $args 0]; set args [lrange $args 1 end] }
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
    # one write: header and payload as two writes on an unbuffered socket
    # meet Nagle and the server's delayed ACK on Linux, ~40 ms per command
    puts -nonewline $sock [binary format I [string length $cmd]]$cmd
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
set frames {}          ;# stim/frame: {dserv_ts frame swapcount flip_us ticks_us missed cmds ds}
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
        stim/frame {
            if {[binary scan [dict get $d data] w7 v] == 1 && [llength $v] == 7} {
                lappend frames [list [dict get $d timestamp] {*}$v]
            }
        }
        logger:beginobs { lappend obs_begin [dict get $d timestamp] }
        logger:endobs   { lappend obs_end   [dict get $d timestamp] }
        stim/cmd {
            set j [json_to_dict [dict get $d data]]
            if {![dict exists $j cmd]} { incr hashed; continue }
            # the ACK1 stamps, when the command was sent with them: swap is
            # SwapCount when stim2 applied it (after the swap, for a '!'
            # command, which then has a flip), dq the rig tick it was
            # dequeued at, in ms
            set ack {}
            foreach k {swap flip dq} {
                lappend ack [expr {[dict exists $j $k] ? [dict get $j $k] : ""}]
            }
            lappend events [list [dict get $j t_send] cmd [dict get $j cmd] [dict get $j seq] $ack]
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
set all_events $events
set w0 ""; set w1 ""
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

# ---------------------------------------------------------------------------
# -render: the frame-locked plan.  stim/frame has one record per rig swap:
# its swapcount, the stim clock at the flip (ticks_us), and how many
# commands and forwarded datapoints stim2 had applied before drawing it.
# Each rig frame is drawn here once, after exactly the inputs it saw:
#   commands  by their ACK stamp -- applied with SwapCount = swap, so first
#             shown in frame swap+1 (a '!' command answered after its swap,
#             with a flip, in frame swap); clock set to its dq tick
#   datapoints by the frame's ds_dispatched advance: that many records, in
#             order, among those dserv had before the frame's record
#             (a shortfall is a forwarded datapoint that is not in the
#             file's streams); clock set to its dserv time on the rig clock
# then stepFrame at max(clock, previous flip) with the frame's own flip.
# The movie gets each frame once per movie tick it spans on the rig's
# timeline: static stretches hold, frames shorter than a tick drop.
# ---------------------------------------------------------------------------
proc median {l} { set l [lsort -real $l]; lindex $l [expr {[llength $l] / 2}] }
if {$opt(render) ne ""} {
    if {![llength $frames]} {
        puts stderr "-render needs stim/frame records, and $path has none (recorded before stimFrameLog)"; exit 1
    }
    set frames [lsort -integer -index 2 $frames]
    if {$w0 ne ""} {
        set sel {}
        foreach f $frames { set ts [lindex $f 0]; if {$ts > $w0 && $ts <= $w1} { lappend sel $f } }
        set frames $sel
        if {![llength $frames]} { puts stderr "no stim/frame records in $span_note"; exit 1 }
    }
    set nf [llength $frames]
    set sc_first [lindex $frames 0 2]; set sc_last [lindex $frames end 2]
    set nmissing [expr {$sc_last - $sc_first + 1 - $nf}]
    # stim clock minus dserv clock, us: places a datapoint on the rig's clock
    set offs {}
    foreach f $frames { lappend offs [expr {[lindex $f 4] - [lindex $f 0]}] }
    set tick_off [median $offs]
    for {set i 0} {$i < $nf} {incr i} { set fev($i) {} }
    set idx [dict create]
    for {set i 0} {$i < $nf} {incr i} { dict set idx [lindex $frames $i 2] $i }

    # commands
    set cmd_before 0; set cmd_after 0; set no_ack 0
    foreach e $events {
        if {[lindex $e 1] ne "cmd"} continue
        lassign [lindex $e 4] swap flip dq
        if {$swap eq "" || $dq eq ""} { incr no_ack; continue }
        set tf [expr {$flip ne "" && $flip != 0 ? $swap : $swap + 1}]
        if {$tf <= $sc_first} { set i 0; if {$tf < $sc_first} { incr cmd_before } } \
        elseif {$tf > $sc_last} { incr cmd_after; continue } \
        else {
            # the first logged frame at or after tf (frames may have gaps)
            while {![dict exists $idx $tf]} { incr tf }
            set i [dict get $idx $tf]
        }
        lappend fev($i) [list $dq cmd [lindex $e 2]]
    }
    if {$no_ack} {
        puts stderr "$no_ack commands carry no ACK stamps (swap, dq): -render cannot place them"; exit 1
    }
    # datapoints: for a trial window, each stream's last record before the
    # window primes the first frame (a cursor stands where it stood)
    set dps {}
    if {$w0 ne ""} {
        set last [dict create]
        foreach e $all_events { if {[lindex $e 1] eq "dp" && [lindex $e 0] <= $w0} { dict set last [lindex $e 2] $e } }
        foreach {n e} $last { lappend fev(0) [list [expr {([lindex $e 0] + $tick_off) / 1000.0}] dp {*}[lrange $e 2 end] [lindex $e 0]] }
    }
    foreach e $events { if {[lindex $e 1] eq "dp"} { lappend dps $e } }
    set k 0; set ndps [llength $dps]; set dp_foreign 0
    for {set i 0} {$i < $nf} {incr i} {
        set fts [lindex $frames $i 0]
        if {$i == 0} { set n -1 } else { set n [expr {[lindex $frames $i 7] - [lindex $frames [expr {$i - 1}] 7]}] }
        while {$k < $ndps && $n != 0 && [lindex $dps $k 0] < $fts} {
            set e [lindex $dps $k]
            lappend fev($i) [list [expr {([lindex $e 0] + $tick_off) / 1000.0}] dp {*}[lrange $e 2 end] [lindex $e 0]]
            incr k; incr n -1
        }
        if {$n > 0} { incr dp_foreign $n }
    }
    set dp_after [expr {$ndps - $k}]
    # in time order within each frame
    for {set i 0} {$i < $nf} {incr i} { set fev($i) [lsort -real -index 0 $fev($i)] }

    # the movie: output ticks per rig frame, from the flips (ms)
    if {$opt(fps) eq ""} {
        set opt(fps) [expr {[info exists geom(hz)] && $geom(hz) > 0 ? $geom(hz) : 60}]
    }
    set period [expr {1000.0 / $opt(fps)}]
    set gap_ms [expr {$opt(gap_given) ? $opt(gap) * 1000.0 : 1e300}]
    set F0 [expr {[lindex $frames 0 4] / 1000.0}]
    set shift 0.0; set pos {}; set prevF $F0
    foreach f $frames {
        set F [expr {[lindex $f 4] / 1000.0}]
        if {$F - $prevF > $gap_ms} { set shift [expr {$shift + $F - $prevF - $gap_ms}] }
        set prevF $F
        lappend pos [expr {round(($F - $F0 - $shift) / $period)}]
    }
    set writes {}
    for {set i 0} {$i < $nf - 1} {incr i} { lappend writes [expr {[lindex $pos [expr {$i + 1}]] - [lindex $pos $i]}] }
    lappend writes 1
    set movie_frames [tcl::mathop::+ {*}$writes]
}

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
if {$opt(render) ne ""} {
    puts "  render  $nf rig frames (swapcount $sc_first..$sc_last, $nmissing not logged),\
 movie $movie_frames frames at $opt(fps) fps = [format %.1f [expr {$movie_frames / double($opt(fps))}]] s\
[expr {$opt(gap_given) ? ", gaps capped at $opt(gap) s" : ""}]"
    puts "          commands placed by ACK stamp: [expr {$ncmd - $cmd_after}][expr {$cmd_before ? " ($cmd_before applied before the first frame)" : ""}][expr {$cmd_after ? ", $cmd_after after the last frame (not drawn)" : ""}]"
    puts "          datapoints placed by ds_dispatched: $k of $ndps[expr {$dp_after ? ", $dp_after after the last frame" : ""}]; $dp_foreign forwarded datapoints were not in the file's streams"
    puts "          stim clock - dserv clock: [format %.3f [expr {$tick_off / 1e6}]] s"
    if {$opt(fps) > 60} {
        puts "  NOTE: QuickTime Player plays movies above 60 fps in slow motion after the\
 first seconds (browsers and mrv2 play them right); -fps 60 makes a copy for it"
    }
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

set skip_prefixes {stim::prelude stimFrameLog {set dservhost}}
proc skipped_cmd {cmd} {
    foreach pre $::skip_prefixes { if {[string match "$pre*" $cmd]} { return 1 } }
    return 0
}

# ---------------------------------------------------------------------------
# -render: frame-locked, in step mode, into a manual frame sink
# ---------------------------------------------------------------------------
if {$opt(render) ne ""} {
    if {[stim_send $sock {info commands stepFrame}] ne "stepFrame"} {
        puts stderr "this stim2 has no stepFrame (step mode): -render needs the headless/step build"; exit 1
    }
    set out $opt(render)
    if {[regexp -nocase {\.(mp4|mov|mkv)$} $out]} {
        set target "|ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s ${win_w}x${win_h}\
 -r $opt(fps) -i - -pix_fmt yuv420p [list [file normalize $out]]"
    } else { set target $out }
    set r [stim_send $sock [list frameSink open -manual $target]]
    if {[is_error $r]} { puts stderr "frameSink: $r"; exit 1 }
    set t_start [expr {[lindex $frames 0 4] / 1000.0}]
    if {[llength $fev(0)]} { set t_start [expr {min($t_start, [lindex $fev(0) 0 0])}] }
    set t_start [expr {$t_start - 1.0}]
    stim_send $sock [list stepMode 1 $t_start]
    set clock $t_start; set prevflip $t_start
    set sent 0; set errors 0; set skipped 0
    set kinds [dict create]
    set wall0 [clock microseconds]
    for {set i 0} {$i < $nf} {incr i} {
        foreach ev $fev($i) {
            lassign $ev t kind p1 p2 p3 p4
            if {$t > $clock} { stim_send $sock [list stepClock $t]; set clock $t }
            if {$kind eq "cmd"} {
                if {[skipped_cmd $p1]} { incr skipped; continue }
                set r [stim_send $sock $p1]
                if {[is_error $r]} { incr errors; puts "  ERROR [string range $p1 0 60] -> $r" } \
                elseif {$opt(verbose)} { puts [format "  frame %6d cmd %s" $i [string range $p1 0 70]] }
            } else {
                set r [stim_send $sock [list stim::inject $p1 $p3 $p2 $p4]]
                if {[is_error $r]} { incr errors; puts "  ERROR inject $p1 -> $r" }
            }
            incr sent
        }
        set flip [expr {[lindex $frames $i 4] / 1000.0}]
        set eval [expr {max($clock, $prevflip)}]
        if {$flip < $eval} { set flip $eval }
        set r [stim_send $sock [list stepFrame $eval $flip]]
        if {[is_error $r]} { incr errors; puts "  ERROR stepFrame $eval $flip -> $r"; break }
        dict incr kinds [dict get $r kind]
        set clock $flip; set prevflip $flip
        set r [stim_send $sock [list frameSink write [lindex $writes $i]]]
        if {[is_error $r]} { incr errors; puts "  ERROR frameSink write -> $r"; break }
        if {$opt(verbose) && $i % 600 == 0} { puts "  frame $i of $nf" }
    }
    set elapsed [expr {([clock microseconds] - $wall0)/1e6}]
    set st [stim_send $sock {frameSink close}]
    stim_send $sock {stepMode 0}
    puts "done: $i rig frames drawn ([join [lmap {k v} $kinds {list $k $v}] {, }]), $sent events\
 ($errors errors, $skipped configure-time commands skipped) in [format %.1f $elapsed] s"
    puts "  movie: [dict get $st frames] frames at $opt(fps) fps\
 ([format %.1f [expr {[dict get $st frames] / double($opt(fps))}]] s) -> $out, status [dict get $st status]"
    foreach n [dict keys $stream_set] {
        catch { puts "  $n: [stim_send $sock [list stim::count $n]] injected, last [stim_send $sock [list stim::get $n]]" }
    }
    close $sock
    exit [expr {$errors || [dict get $st status] != 0 ? 1 : 0}]
}

# ---------------------------------------------------------------------------
# the timeline, paced by time
# ---------------------------------------------------------------------------
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
        if {[skipped_cmd $p1]} { incr skipped; continue }
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
