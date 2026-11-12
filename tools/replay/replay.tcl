#!/usr/bin/env dlsh
# replay.tcl -- replay the stim2 side of a dserv .ess data file into a live
# stim2, standing in for ESS.
#
#     dlsh tools/replay/replay.tcl FILE.ess ?options?
#     dlsh tools/replay/replay.tcl FILE.obs.dgz ?options?
#
# FILE may also be the obs DG dfconf converts each .ess into at close
# (dslog::readESS with df wide_datafiles on, the default from dserv
# 0.60.16; converted narrow, stim/frame's int64 records are dropped and
# nothing can be frame-locked -- the script says so). See read_obs below.
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
#       -dumpplan F    with -render, write the frame-locked plan to F: every
#                      rig frame (swapcount, flip tick, movie writes) and the
#                      inputs placed before it, and the gaze samples -- what
#                      two readings of one session can be compared on
#       -render OUT    frame-locked: draw each of the rig's frames (stim/frame)
#                      in step mode and write a movie on the rig's timeline.
#                      OUT ending .mp4/.mov/.mkv goes through ffmpeg; anything
#                      else is raw RGBA to a file or "|command". -speed is
#                      ignored; -gap compresses only when given. Needs a stim2
#                      with stepFrame (headless or not) and a file with
#                      stim/frame and ACK-stamped stim/cmd records.
#       -each          with -render OUT into a movie file: one movie per trial
#                      of the range (default: every trial), each holding the
#                      rig frames of that trial's window and the gaze over
#                      them. OUT names the files: a % format takes the trial
#                      number (out/planko_t%03d.mp4), else _tNNN goes before
#                      the extension. A trial's last frame holds for one
#                      movie frame; the rig's idle time before the next
#                      trial starts the next movie
#       -fps F         the movie's frame rate (default: the rig's refresh; above
#                      60, QuickTime Player slows down -- -fps 60 for it).
#                      Below the rig's refresh, into a movie file, every rig
#                      tick is still drawn and each movie frame is the mean
#                      of the ticks it spans (-noblend: drop instead)
#       -crf N         x264 quality for a movie file (default 14; lower is
#                      better and bigger, 0 lossless)
#       -eye MODE      with -render, draw the logged gaze (eyetracking/raw +
#                      em/settings) over each movie tick: auto (default; a
#                      file whose samples all lie within +/-90 is already in
#                      degrees, as the virtual source logs them, otherwise
#                      they are calibrated as ADC counts), analog, video,
#                      degrees, or off
#       -touch         with -render, draw the touchscreen presses (ESS TOUCH
#                      events) as rings
#       -geometry HX,HY[,HZ]  the rig's half-screen degrees (and refresh) for
#                      a file without ess/screen_* records; overrides them
#                      when the file has them
#       -bg R,G,B      the background (0-255) the display had at file open,
#                      for a file without ess/screen_bg; overrides it
#       -launch        start a stim2 of our own on -port's base and stop it on
#                      exit: with -render, headless at the rig's pixel size
#                      and refresh (ess/screen_w/h/refresh_rate), so the
#                      movie is the rig's picture; otherwise a window in the
#                      rig's aspect, no wider than 960. -host must be
#                      localhost, and nothing may already answer on -port.
#       -keep          with -launch, leave that stim2 running at exit (a
#                      batch of files, one startup)
#       -size WxH      with -launch, this size instead
#       -stim2 PATH    with -launch, the stim2 to start (default: the app
#                      bundle these tools came with, else the installed one)
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
#      -render, frame by frame on the rig's clock
#
# The reading, planning and rendering are tools/lib/replaylib.tcl, which a
# program that renders many trials of one session (a trial viewer's render
# service) uses directly; this script is its command line.
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

source [file join [file dirname [file normalize [info script]]] .. lib replaylib.tcl]

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
set opt(eye)     auto
set opt(touch)   0
set opt(blend)   1
set opt(geometry) ""
set opt(bg)      ""
set opt(dumpplan) ""
set opt(crf)     14
set opt(each)    0
set opt(launch)  0
set opt(keep)    0
set opt(size)    ""
set opt(stim2)   ""

if {[llength $argv] < 1} {
    puts stderr "usage: dlsh replay.tcl FILE.ess ?-host H? ?-port P? ?-live? ?-trial N | -trials A-B?\
 ?-speed X? ?-gap S? ?-stream NAME ...? ?-dry? ?-verbose? ?-render OUT? ?-each? ?-fps F? ?-noblend? ?-crf N? ?-eye MODE? ?-touch? ?-geometry HX,HY[,HZ]? ?-bg R,G,B?\
 ?-launch ?-keep? ?-size WxH? ?-stim2 PATH??"
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
        -noblend { set opt(blend) 0 }
        -each    { set opt(each) 1 }
        -launch  { set opt(launch) 1 }
        -keep    { set opt(keep) 1 }
        -size    { set opt(size) [lindex $args 0]; set args [lrange $args 1 end]
                   if {![regexp {^\d+x\d+$} $opt(size)]} { puts stderr "-size wants WxH"; exit 2 } }
        -stim2   { set opt(stim2) [lindex $args 0]; set args [lrange $args 1 end] }
        -dumpplan { set opt(dumpplan) [lindex $args 0]; set args [lrange $args 1 end] }
        -bg      { set opt(bg) [split [lindex $args 0] ,]; set args [lrange $args 1 end]
                   if {[llength $opt(bg)] != 3} { puts stderr "-bg wants R,G,B"; exit 2 } }
        -geometry { set opt(geometry) [split [lindex $args 0] ,]; set args [lrange $args 1 end]
                    if {[llength $opt(geometry)] < 2} { puts stderr "-geometry wants HX,HY or HX,HY,HZ"; exit 2 } }
        -crf     { set opt(crf)   [lindex $args 0]; set args [lrange $args 1 end] }
        -eye     { set opt(eye)   [lindex $args 0]; set args [lrange $args 1 end]
                   if {$opt(eye) ni {auto analog video degrees off}} { puts stderr "-eye wants auto|analog|video|degrees|off"; exit 2 } }
        -touch   { set opt(touch) 1 }
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
if {($opt(keep) || $opt(size) ne "" || $opt(stim2) ne "") && !$opt(launch)} {
    puts stderr "-keep, -size and -stim2 go with -launch"; exit 2
}
if {$opt(launch) && $opt(host) ni {localhost 127.0.0.1}} {
    puts stderr "-launch starts stim2 on this machine: -host must be localhost"; exit 2
}
if {$opt(each)} {
    if {$opt(render) eq "" || [string index $opt(render) 0] eq "|" \
            || ![regexp -nocase {\.(mp4|mov|mkv)$} $opt(render)]} {
        puts stderr "-each wants -render OUT with OUT a movie file (.mp4/.mov/.mkv): one is written per trial"
        exit 2
    }
}


# a library error ends the run with its exit code (-errorcode {REPLAY n});
# anything else is a bug, reported with its stack
proc run {script} {
    if {[catch {uplevel 1 $script} e opts]} {
        set ec [dict get $opts -errorcode]
        if {[lindex $ec 0] eq "REPLAY"} { puts stderr $e; exit [lindex $ec 1] }
        puts stderr [dict get $opts -errorinfo]
        exit 1
    }
}

# ---------------------------------------------------------------------------
# read the file, plan
# ---------------------------------------------------------------------------
run {
    set S [replay::load $path -streams $opt(streams) -geometry $opt(geometry) -bg $opt(bg)]
    set P [replay::plan $S -trials $opt(trial) -render $opt(render) -each $opt(each) \
               -fps $opt(fps) -blend $opt(blend) -gap [expr {$opt(gap_given) ? $opt(gap) : ""}] \
               -eye $opt(eye) -touch $opt(touch)]
    if {$opt(render) ne "" && $opt(dumpplan) ne ""} { replay::dumpplan $P $opt(dumpplan) }
}
foreach v {script script_path libs stimdg stimdg_b64 bind_names obs_note counts hashed eye_recs} {
    set $v [replay::get $S $v]
}
array set geom [replay::get $S geom]
dict with P {}

puts "replay: $path"
puts "  script  $script_path ([string length $script] bytes), libs: [expr {[dict size $libs] ? [dict keys $libs] : "none"}]"
puts "  stimdg  [expr {$stimdg_b64 ne "" ? [string range $stimdg 1 end-1] : "[string length $stimdg] bytes"}]; bound streams: [expr {[llength $bind_names] ? $bind_names : "none"}][expr {[llength $opt(streams)] ? " + $opt(streams)" : ""}]"
set bg_note [expr {[info exists geom(bg)] ? "background $geom(bg)" : "no ess/screen_bg: this stim2's background until the script sets one"}]
if {[info exists geom(halfx)] && [info exists geom(halfy)]} {
    puts "  screen  rig: +/-$geom(halfx) x +/-$geom(halfy) deg[expr {[info exists geom(w)] ? ", $geom(w)x$geom(h) px" : ""}][expr {[info exists geom(hz)] ? ", $geom(hz) Hz" : ""}]"
} else {
    puts "  screen  no ess/screen_* records: this stim2's own degree mapping will be used"
}
puts "          $bg_note"
if {$obs_note ne ""} { puts "  NOTE: $obs_note" }
puts "  file    [format %.1f [expr {($t_last - $t_first)/6e7}]] min, $ntrials observation periods, [dict get $counts stim/cmd] commands ($hashed hash-only, skipped)"
puts "  playing $span_note: $ncmd commands, $ndp datapoints, span [format %.1f [expr {([lindex $events end 0] - [lindex $events 0 0])/1e6}]] s, speed $opt(speed), gaps capped at $opt(gap) s"
if {[llength $bind_names] == 0 && [llength $opt(streams)] == 0} {
    puts "  NOTE: the stim/bind table is empty (a script from before stim::bind): no datapoints will\
 be injected, so a cursor driven by them will not move. Use -stream to name the records anyway."
}
if {$opt(render) ne ""} {
    puts "  render  $nf rig frames (swapcount $sc_first..$sc_last, $nmissing not logged),\
 movie $movie_frames frames at $fps fps = [format %.1f [expr {$movie_frames / double($fps)}]] s\
[expr {$opt(gap_given) ? ", gaps capped at $opt(gap) s" : ""}][expr {$blend > 1 ? ", each the mean of $blend rig ticks" : ""}]"
    puts "          commands placed by ACK stamp: [expr {$ncmd - $cmd_after}][expr {$cmd_before ? " ($cmd_before applied before the first frame)" : ""}][expr {$cmd_after ? ", $cmd_after after the last frame (not drawn)" : ""}]"
    puts "          datapoints placed by ds_dispatched: $dp_placed of $ndps[expr {$dp_after ? ", $dp_after after the last frame" : ""}]; $dp_foreign forwarded datapoints were not in the file's streams"
    puts "          stim clock - dserv clock: [format %.3f [expr {$tick_off / 1e6}]] s"
    if {$opt(each)} {
        set ts [dict keys $movie_each]
        puts "  each    [llength $ts] movies, one per trial ([lindex $ts 0]..[lindex $ts end]),\
 [replay::each_name $opt(render) [lindex $ts 0]] .. [file tail [replay::each_name $opt(render) [lindex $ts end]]]"
    }
    if {$n_eye} {
        puts "  gaze    $n_eye samples ([expr {$eye_deg_n ? "$eye_deg_n already degrees" : ""}][expr {$eye_deg_n && $eye_cal_n ? ", " : ""}][expr {$eye_cal_n ? "$eye_cal_n calibrated via em/settings" : ""}]), $eye_zero exact 0,0 dropped, -eye $opt(eye) ($eye_mode)[expr {$cal_changes ? ", em/settings changed $cal_changes times in the file" : ""}]"
    } elseif {[llength $eye_recs] && $opt(eye) eq "off"} {
        puts "  gaze    off (-eye off)"
    }
    if {$opt(touch)} { puts "  touch   $n_touch presses" }
    if {$fps > 60} {
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
# -launch: a stim2 of our own, started here and stopped on exit (unless
# -keep). For -render it is headless at the rig's pixel size and refresh,
# so the movie is the rig's picture; otherwise a window in the rig's
# aspect, like the mirror's.
# ---------------------------------------------------------------------------
set launched ""
if {$opt(launch)} {
    run {
        set launched [replay::launch -port $opt(port) -geom [array get geom] \
                          -render [expr {$opt(render) ne ""}] -size $opt(size) -exe $opt(stim2)]
    }
    if {$opt(keep)} {
        puts "  -keep: that stim2 stays up after this replay (port $opt(port)); stop it with `echo exit | nc localhost [expr {$opt(port) - 2}]`"
    } else {
        # a terminal's Ctrl-C reaches stim2 too (same process group); a
        # plain exit of the driver stops it here
        rename exit _exit
        proc exit {{code 0}} {
            if {$::launched ne "" && [replay::alive $::launched]} { catch {exec kill $::launched} }
            _exit $code
        }
    }
}

# ---------------------------------------------------------------------------
# connect and set up, as configure_stim does
# ---------------------------------------------------------------------------
run {
    set sock [replay::connect $opt(host) $opt(port) $launched]
    set v [replay::check_stimess $sock $opt(host):$opt(port)]
}
puts "  stim2   [framelock::send $sock {catch {stimVersion} v; set v}], stimess $v"
if {[catch {framelock::setup $sock [replay::setup_dict $S "replay: [file tail $path] ($span_note)"] replay::say} wh]} {
    puts stderr $wh; exit 1
}
puts "  sourced [file tail $script_path]"

if {$opt(trial) ne "" && [lindex $opt(trial) 0] > 1} { framelock::send $sock reset }

proc stream_counts {} {
    foreach n [dict keys [replay::get $::S stream_set]] {
        # a stimess binding reports its count (stimdg is stim2's own readdg)
        set c [framelock::send $::sock [list stim::count $n]]
        if {![framelock::is_error $c]} { puts "  $n: $c injected, last [framelock::send $::sock [list stim::get $n]]" }
    }
}

# ---------------------------------------------------------------------------
# -render: frame-locked, in step mode, into a manual frame sink
# ---------------------------------------------------------------------------
if {$opt(render) ne ""} {
    run { set r [replay::render $P $sock -crf $opt(crf) -verbose $opt(verbose)] }
    dict with r {}
    puts "done: $drawn rig frames drawn ([join [lmap {k v} $kinds {list $k $v}] {, }]), $sent events\
 ($errors errors, $skipped configure-time commands skipped) in [format %.1f $elapsed] s"
    if {$n_eye} { puts "  gaze: $n_eye samples over the movie, $repaints held-frame repaints" }
    if {$opt(each)} {
        set tot 0
        foreach m $movies {
            lassign $m t o s
            set mf [expr {[dict get $s frames] / $blend}]; incr tot $mf
            puts "  trial [format %4d $t]: $mf frames ([format %.1f [expr {$mf / double($fps)}]] s) -> $o[expr {[dict get $s status] != 0 ? ", status [dict get $s status]" : ""}]"
        }
        puts "  movies: [llength $movies], $tot frames at $fps fps[expr {$blend > 1 ? ", each the mean of $blend rig ticks" : ""}], $bad failed"
    } else {
        set mf [expr {[dict get $status frames] / $blend}]
        puts "  movie: $mf frames at $fps fps\
 ([format %.1f [expr {$mf / double($fps)}]] s)[expr {$blend > 1 ? ", each the mean of $blend rig ticks" : ""}]\
 -> $opt(render), status [dict get $status status]"
    }
    stream_counts
    close $sock
    exit [expr {$errors || $bad ? 1 : 0}]
}

# ---------------------------------------------------------------------------
# the timeline, paced by time
# ---------------------------------------------------------------------------
run { set r [replay::play $P $sock -speed $opt(speed) -gap $opt(gap) -verbose $opt(verbose)] }
dict with r {}
puts "done: $sent events in [format %.1f $elapsed] s ($errors errors, $skipped configure-time commands skipped),\
 stim2 swapped $swaps frames"
stream_counts
close $sock
exit [expr {$errors ? 1 : 0}]
