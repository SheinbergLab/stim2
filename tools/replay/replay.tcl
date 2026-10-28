#!/usr/bin/env dlsh
# replay.tcl -- replay the stim2 side of a dserv .ess data file into a live
# stim2, standing in for ESS.
#
#     dlsh tools/replay/replay.tcl FILE.ess ?options?
#     dlsh tools/replay/replay.tcl FILE.obs.dgz ?options?
#
# FILE may also be the obs DG dfconf converts each .ess into at close
# (dslog::readESS, df wide_datafiles on -- off, stim/frame's int64 records
# are dropped and nothing can be frame-locked). See read_obs below.
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
#       -geometry HX,HY[,HZ]  the rig's half-screen degrees (and refresh) for
#                      a file without ess/screen_* records; overrides them
#                      when the file has them
#       -bg R,G,B      the background (0-255) the display had at file open,
#                      for a file without ess/screen_bg; overrides it
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
source [file join [file dirname [file normalize [info script]]] .. lib framelock.tcl]

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
set opt(blend)   1
set opt(geometry) ""
set opt(bg)      ""
set opt(dumpplan) ""
set opt(crf)     14

if {[llength $argv] < 1} {
    puts stderr "usage: dlsh replay.tcl FILE.ess ?-host H? ?-port P? ?-live? ?-trial N | -trials A-B?\
 ?-speed X? ?-gap S? ?-stream NAME ...? ?-dry? ?-verbose? ?-render OUT? ?-fps F? ?-noblend? ?-crf N? ?-eye MODE? ?-geometry HX,HY[,HZ]? ?-bg R,G,B?"
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
        -dumpplan { set opt(dumpplan) [lindex $args 0]; set args [lrange $args 1 end] }
        -bg      { set opt(bg) [split [lindex $args 0] ,]; set args [lrange $args 1 end]
                   if {[llength $opt(bg)] != 3} { puts stderr "-bg wants R,G,B"; exit 2 } }
        -geometry { set opt(geometry) [split [lindex $args 0] ,]; set args [lrange $args 1 end]
                    if {[llength $opt(geometry)] < 2} { puts stderr "-geometry wants HX,HY or HX,HY,HZ"; exit 2 } }
        -crf     { set opt(crf)   [lindex $args 0]; set args [lrange $args 1 end] }
        -eye     { set opt(eye)   [lindex $args 0]; set args [lrange $args 1 end]
                   if {$opt(eye) ni {auto analog video degrees off}} { puts stderr "-eye wants auto|analog|video|degrees|off"; exit 2 } }
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
# read the file
# ---------------------------------------------------------------------------
set script ""; set script_path ""; set libs {}; set bind_names {}
set stimdg ""; set obs_begin {}; set obs_end {}
set events {}          ;# {ts kind payload...}, kinds: cmd | dp
set frames {}          ;# stim/frame: {dserv_ts frame swapcount flip_us ticks_us missed cmds ds eval_us}
set eye_recs {}        ;# eyetracking/raw: {dserv_ts {h v h v ...}}, 10 samples a record
set em_settings {}     ;# em/settings: {dserv_ts settings} each time it changed
set counts [dict create]
set hashed 0
set stream_set [dict create]
foreach s $opt(streams) { dict set stream_set $s 1 }

set records {}
set stimdg_b64 ""
set obs_note ""

# ---------------------------------------------------------------------------
# read_obs: the same structures from dfconf's obs DG (.obs.dgz), not the .ess.
#
# dslog::readESS puts a record that falls inside an observation period in
# that period's row -- <ds>name values (concatenated), <dst>name per-record
# ms from its BEGINOBS (microsecond fraction with wide types), <dsn>name
# per-record value counts -- and every other record (before, between and
# after periods) in <session>name, values only, no times.  stimdg arrives
# decoded as <stimdg> columns.
#
# Times are rebuilt on the dserv clock the .ess reader uses: a stim/cmd
# record's dserv timestamp IS its JSON t_ret, so each row's offset (median
# t_ret - (obs_start_ms + <dst>)) puts that row's records back to the
# microsecond (obs_start_ms is whole ms; the offset absorbs it).
#
# Outside the periods, a DG from a dlsh whose readESS writes <sessiont>
# (per-record ms from the file's first record) and <sessionn> (per-record
# counts) times every record directly: the file's first record on the dserv
# clock is the median (t_ret - <sessiont>) over the session commands, and
# em/settings applies from each change on, as from the .ess.  An older DG
# has values only: commands still carry their own t_send/t_ret, frames are
# timed from their flip tick through the median tick - dserv offset of the
# frames inside periods, and only the first em/settings is used (a note
# says how many changes were lost).  Bound streams and eyetracking/raw are
# logged obs-limited, so theirs are in rows, with times, either way.
#
# A bound stream with a numeric (non-text) type goes in as its values
# joined by spaces.
# ---------------------------------------------------------------------------
proc median {l} { set l [lsort -real $l]; lindex $l [expr {[llength $l] / 2}] }
proc read_obs {path} {
    global script script_path libs bind_names stimdg stimdg_b64 obs_begin obs_end \
        events frames eye_recs em_settings counts hashed records geom obs_note
    set g [dg_read $path]
    set names [dg_tclListnames $g]
    # the absolute anchor (a wide DG from a dlsh that writes it): the file's
    # first record on the dserv clock, and each BEGINOBS from it in us
    set A ""
    if {[dl_exists $g:file_t0_us] && [dl_exists $g:obs_start_us]} {
        set A [dl_get $g:file_t0_us 0]
    }
    if {![dl_exists $g:obs_start_ms] || ![dl_exists $g:e_types]} {
        error "$path is not an obs DG (no obs_start_ms / e_types)"
    }
    set sess {}
    foreach n $names {
        if {[string match <session>* $n]} {
            dict set sess [string range $n 9 end] [dl_tcllist $g:$n:0]
        }
    }
    # the header records
    if {[dict exists $sess stim/script]} {
        foreach v [dict get $sess stim/script] {
            set j [json_to_dict $v]
            if {[dict get $j body] ne ""} { set script [dict get $j body]; set script_path [dict get $j path] }
        }
    }
    dict for {n vals} $sess {
        if {[string match stim/lib/* $n]} {
            foreach v $vals { set j [json_to_dict $v]; dict set libs [dict get $j name] [dict get $j body] }
        }
    }
    if {[dict exists $sess stim/bind]} {
        set j [json_to_dict [lindex [dict get $sess stim/bind] end]]
        foreach b [dict get $j bindings] { lappend bind_names [dict get $b name] }
    }
    foreach {k n} {halfx screen_halfx halfy screen_halfy w screen_w h screen_h hz screen_refresh_rate bg screen_bg} {
        if {[dict exists $sess ess/$n]} { set geom($k) [string trim [lindex [dict get $sess ess/$n] end]] }
    }
    # per-record session times, when this DG has them (<sessiont>)
    set stimes {}
    foreach n $names {
        if {[string match <sessiont>* $n]} {
            dict set stimes [string range $n 10 end] \
                [list [dl_tcllist $g:$n:0] [dl_tcllist $g:<sessionn>[string range $n 10 end]:0]]
        }
    }
    # stimdg: every occurrence as logged (<dgbuf>), the first for setup;
    # else rebuilt from the decoded <stimdg> columns (an older DG)
    set dgocc {}
    if {[dl_exists $g:<dgbuf>stimdg]} {
        set nocc [dl_length $g:<dgbuf>stimdg:0]
        for {set k 0} {$k < $nocc} {incr k} {
            lappend dgocc [list [dl_get $g:<dgbuft>stimdg:0 $k] \
                [binary encode base64 [binary format c* [dl_tcllist $g:<dgbuf>stimdg:0:$k]]]]
        }
    }
    set sd {}
    foreach n $names { if {[string match <stimdg>* $n]} { lappend sd $n } }
    if {[llength $dgocc]} {
        set stimdg_b64 [lindex $dgocc 0 1]
        set stimdg "(the first of [llength $dgocc] logged stimdg occurrences)"
    } elseif {[llength $sd]} {
        catch { dg_delete stimdg }
        dg_create stimdg
        foreach n $sd { dl_set stimdg:[string range $n 8 end] $g:$n }
        dg_toString64 stimdg stimdg_b64
        set stimdg "(rebuilt from [llength $sd] <stimdg> columns)"
        dg_delete stimdg
    }
    # per-row clock: the commands' t_ret against their row times
    set nobs [dl_length $g:obs_start_ms]
    set has_cmd [dl_exists $g:<ds>stim/cmd]
    set all_off {}
    for {set i 0} {$i < $nobs} {incr i} {
        set t0 [expr {[dl_get $g:obs_start_ms $i] * 1000.0}]
        set row_off($i) {}
        if {$has_cmd} {
            foreach v [dl_tcllist $g:<ds>stim/cmd:$i] t [dl_tcllist $g:<dst>stim/cmd:$i] {
                if {![catch {json_to_dict $v} j] && [dict exists $j t_ret]} {
                    lappend row_off($i) [expr {[dict get $j t_ret] - ($t0 + $t * 1000.0)}]
                }
            }
        }
        lappend all_off {*}$row_off($i)
    }
    if {$A eq "" && ![llength $all_off]} {
        error "$path: no file_t0_us and no stim/cmd records in observation periods to set the clock by"
    }
    set off_all [expr {[llength $all_off] ? [median $all_off] : 0}]
    proc ::cmd_event {v} {
        global hashed
        dict incr ::counts stim/cmd
        set c [framelock::cmd_record $v]
        if {$c eq ""} { incr hashed; return }
        lassign $c t_send cmd seq ack
        lappend ::events [list $t_send cmd $cmd $seq $ack]
    }
    set skip {stim/cmd stim/frame eyetracking/raw}
    set ticks_off {}
    for {set i 0} {$i < $nobs} {incr i} {
        if {$A ne ""} {
            set t0 [expr {$A + [dl_get $g:obs_start_us $i]}]
        } else {
            set off [expr {[llength $row_off($i)] ? [median $row_off($i)] : $off_all}]
            set t0 [expr {[dl_get $g:obs_start_ms $i] * 1000.0 + $off}]
        }
        set et [dl_tcllist $g:e_times:$i]; set ety [dl_tcllist $g:e_types:$i]
        set k [lsearch -exact $ety 20]
        lappend obs_begin [expr {wide($t0)}]
        lappend obs_end [expr {wide($t0 + ($k >= 0 ? [lindex $et $k] : [lindex $et end]) * 1000.0)}]
        foreach n $names {
            if {![string match <ds>* $n]} continue
            set dp [string range $n 4 end]
            set vals [dl_tcllist $g:$n:$i]
            set ts [lmap t [dl_tcllist $g:<dst>$dp:$i] {expr {wide($t0 + $t * 1000.0)}}]
            set cnt [dl_tcllist $g:<dsn>$dp:$i]
            if {$dp eq "stim/cmd"} {
                foreach v $vals { cmd_event $v }
            } elseif {$dp eq "stim/frame"} {
                set p 0
                foreach t $ts c $cnt {
                    if {$c == 7 || $c == 8} {
                        set f [lrange $vals $p [expr {$p + $c - 1}]]
                        if {$c == 7} { lappend f "" }
                        lappend frames [list $t {*}$f]
                        lappend ticks_off [expr {[lindex $f 3] - $t}]
                    }
                    incr p $c
                }
            } elseif {$dp eq "eyetracking/raw"} {
                set p 0
                foreach t $ts c $cnt {
                    lappend eye_recs [list $t [lrange $vals $p [expr {$p + $c - 1}]]]
                    incr p $c
                }
            } else {
                set str [expr {[dl_datatype $g:$n:$i] eq "string"}]
                set p 0
                foreach t $ts c $cnt {
                    set data [expr {$str ? [lindex $vals $p] : [join [lrange $vals $p [expr {$p + $c - 1}]] " "]}]
                    lappend records [list $t $dp [expr {$str ? 1 : 2}] $data]
                    incr p $c
                }
            }
        }
    }
    # outside the periods.  The file's first record on the dserv clock,
    # from the session commands' t_ret against their <sessiont> times
    set anchor $A
    if {$anchor eq "" && [dict exists $sess stim/cmd] && [dict exists $stimes stim/cmd]} {
        set a {}
        foreach v [dict get $sess stim/cmd] t [lindex [dict get $stimes stim/cmd] 0] {
            if {![catch {json_to_dict $v} j] && [dict exists $j t_ret]} {
                lappend a [expr {[dict get $j t_ret] - $t * 1000.0}]
            }
        }
        if {[llength $a]} { set anchor [median $a] }
    }
    proc ::sess_time {stimes anchor dp k} {
        # dserv time of session record k of dp, or "" when untimed
        if {$anchor eq "" || ![dict exists $stimes $dp]} { return "" }
        set t [lindex [dict get $stimes $dp] 0 $k]
        expr {$t eq "" ? "" : wide($anchor + $t * 1000.0)}
    }
    if {[dict exists $sess stim/cmd]} { foreach v [dict get $sess stim/cmd] { cmd_event $v } }
    if {[dict exists $sess stim/frame]} {
        set toff [expr {[llength $ticks_off] ? [median $ticks_off] : 0}]
        set sv [dict get $sess stim/frame]
        # values per record from <sessionn>; 7 for a DG without it (no
        # dlsh that predates <sessionn> saw eval_us)
        set cnts [expr {[dict exists $stimes stim/frame] ? [lindex [dict get $stimes stim/frame] 1] : {}}]
        set k 0
        for {set p 0} {$p + 6 < [llength $sv]} {incr p $c} {
            set c [expr {[llength $cnts] ? [lindex $cnts $k] : 7}]
            set f [lrange $sv $p [expr {$p + $c - 1}]]
            if {$c == 7} { lappend f "" }
            set t [sess_time $stimes $anchor stim/frame $k]
            if {$t eq ""} { set t [expr {wide([lindex $f 3] - $toff)}] }
            lappend frames [list $t {*}$f]
            incr k
        }
    }
    if {[dict exists $sess em/settings]} {
        set ems [dict get $sess em/settings]
        if {[sess_time $stimes $anchor em/settings 0] ne ""} {
            set k 0
            foreach e $ems { lappend em_settings [list [sess_time $stimes $anchor em/settings $k] $e]; incr k }
        } else {
            lappend em_settings [list 0 [lindex $ems 0]]
            if {[llength $ems] > 1} {
                set obs_note "em/settings changed [expr {[llength $ems] - 1}] times in the file, untimed in this obs DG (a dlsh before <sessiont>): the first is used for every sample"
            }
        }
    }
    # every stimdg occurrence, on the timeline like the .ess reader's
    if {$anchor ne ""} {
        foreach o $dgocc {
            lappend records [list [expr {wide($anchor + [lindex $o 0] * 1000.0)}] stimdg 6 [lindex $o 1]]
        }
    }
    # any other session stream (a bound stream set between trials), timed
    dict for {dp vals} $sess {
        if {$dp in {stim/cmd stim/frame em/settings stim/script stim/bind} ||
            [string match stim/lib/* $dp] || [string match ess/screen_* $dp]} continue
        if {[sess_time $stimes $anchor $dp 0] eq ""} continue
        set cnt [lindex [dict get $stimes $dp] 1]
        set p 0; set k 0
        foreach c $cnt {
            set data [expr {$c == 1 ? [lindex $vals $p] : [join [lrange $vals $p [expr {$p + $c - 1}]] " "]}]
            lappend records [list [sess_time $stimes $anchor $dp $k] $dp 1 $data]
            incr p $c; incr k
        }
    }
    dict set counts stim/cmd [expr {[dict exists $counts stim/cmd] ? [dict get $counts stim/cmd] : 0}]
    dg_delete $g
}

if {[regexp -nocase {\.dgz?$} $path]} {
    read_obs $path
} else {
set h [dslog::open $path r]
# first pass: header-ish records (any order in the file; keep the last)
# and the bound-stream names, which decide what the second pass keeps
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
        stimdg {
            # the first sets stim2 up; every one (the re-emit at file open,
            # a new table mid-session) also reached stim2 on the rig as a
            # forwarded datapoint, counted in ds_dispatched, so each goes
            # in the timeline too
            if {$stimdg eq ""} { set stimdg [dict get $d data] }
            lappend records [list [dict get $d timestamp] stimdg 6 \
                                 [binary encode base64 [dict get $d data]]]
        }
        ess/screen_halfx { set geom(halfx) [string trim [dict get $d data]] }
        ess/screen_halfy { set geom(halfy) [string trim [dict get $d data]] }
        ess/screen_w     { set geom(w)     [string trim [dict get $d data]] }
        ess/screen_h     { set geom(h)     [string trim [dict get $d data]] }
        ess/screen_refresh_rate { set geom(hz) [string trim [dict get $d data]] }
        ess/screen_bg    { set geom(bg) [string trim [dict get $d data]] }
        eyetracking/raw {
            if {[binary scan [dict get $d data] f* v] == 1 && [llength $v] >= 2} {
                lappend eye_recs [list [dict get $d timestamp] $v]
            }
        }
        em/settings { lappend em_settings [list [dict get $d timestamp] [dict get $d data]] }
        stim/frame {
            set v [framelock::frame_decode [dict get $d data]]
            if {$v ne ""} { lappend frames [list [dict get $d timestamp] {*}$v] }
        }
        logger:beginobs { lappend obs_begin [dict get $d timestamp] }
        logger:endobs   { lappend obs_end   [dict get $d timestamp] }
        stim/cmd {
            # with the ACK1 stamps, when the command was sent with them:
            # swap is SwapCount when stim2 applied it (after the swap, for
            # a '!' command, which then has a flip), dq the rig tick it was
            # dequeued at, in ms
            set c [framelock::cmd_record [dict get $d data]]
            if {$c eq ""} { incr hashed; continue }
            lassign $c t_send cmd seq ack
            lappend events [list $t_send cmd $cmd $seq $ack]
        }
        default {
            # stream records: all kept for now, filtered once bind_names is
            # known (stim/bind may come after them, and a bound stream can
            # be named anything -- runner/press_cmd, not only ess/*)
            lappend records [list [dict get $d timestamp] $nm [dict get $d type] [dict get $d data]]
        }
    }
}
dslog::close $h
}

if {[llength $opt(bg)]} { set geom(bg) $opt(bg) }
if {[llength $opt(geometry)]} {
    lassign $opt(geometry) geom(halfx) geom(halfy) hz
    if {$hz ne ""} { set geom(hz) $hz }
}
foreach n $bind_names { dict set stream_set $n 1 }
dict set stream_set stimdg 1      ;# stim2 subscribes to it (readdg)
foreach r $records {
    lassign $r ts nm type data
    if {[dict exists $stream_set $nm]} { lappend events [list $ts dp $nm $type $data] }
}
unset records
set events [lsort -integer -index 0 $events]
set eye_recs [lsort -integer -index 0 $eye_recs]

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
# -render: the frame-locked plan (tools/lib/framelock.tcl).  stim/frame has
# one record per rig swap: its swapcount, the stim clock at the flip
# (ticks_us), and how many commands and forwarded datapoints stim2 had
# applied before drawing it.  Each rig frame is drawn here once, after
# exactly the inputs it saw: commands by their ACK stamp, datapoints by the
# frame's ds_dispatched advance, then stepFrame at max(clock, previous flip)
# with the frame's own flip.  The movie gets each frame once per movie tick
# it spans on the rig's timeline: static stretches hold, frames shorter
# than a tick drop.
# ---------------------------------------------------------------------------
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
    set P [framelock::plan_new $tick_off]

    # commands; those the rig applied after its last frame here are not drawn
    set cmd_before 0; set cmd_after 0; set no_ack 0; set skipped 0
    foreach e $events {
        if {[lindex $e 1] ne "cmd"} continue
        lassign [lindex $e 4] swap flip dq
        if {$swap eq "" || $dq eq ""} { incr no_ack; continue }
        set tf [framelock::cmd_target $swap $flip [lindex $e 2]]
        if {$tf < $sc_first} { incr cmd_before } elseif {$tf > $sc_last} { incr cmd_after; continue }
        if {[framelock::skipped_cmd [lindex $e 2]]} { incr skipped; continue }
        framelock::plan_cmd P $swap $flip $dq [lindex $e 2]
    }
    if {$no_ack} {
        puts stderr "$no_ack commands carry no ACK stamps (swap, dq): -render cannot place them"; exit 1
    }
    # datapoints: for a trial window, each stream's last record before the
    # window primes the first frame (a cursor stands where it stood)
    if {$w0 ne ""} {
        set last [dict create]
        foreach e $all_events { if {[lindex $e 1] eq "dp" && [lindex $e 0] <= $w0} { dict set last [lindex $e 2] $e } }
        foreach {n e} $last { framelock::plan_prime P [lindex $e 0] {*}[lrange $e 2 end] }
    }
    set ndps 0
    foreach e $events { if {[lindex $e 1] eq "dp"} { framelock::plan_dp P [lindex $e 0] {*}[lrange $e 2 end]; incr ndps } }
    for {set i 0} {$i < $nf} {incr i} { set fev($i) [framelock::plan_frame P [lindex $frames $i]] }
    set dp_after [llength [dict get $P dps]]
    set k [expr {$ndps - $dp_after}]
    set dp_foreign [dict get $P foreign]

    # the movie: output ticks per rig frame, from the flips (ms)
    if {$opt(fps) eq ""} {
        set opt(fps) [expr {[info exists geom(hz)] && $geom(hz) > 0 ? $geom(hz) : 60}]
    }
    # blending: below the rig's refresh into a movie file, draw at the rig's
    # rate and let ffmpeg average each group of `blend` ticks into one frame
    set movie_out [expr {[string index $opt(render) 0] ne "|" && [regexp -nocase {\.(mp4|mov|mkv)$} $opt(render)]}]
    set rig_hz [expr {[info exists geom(hz)] && $geom(hz) > 0 ? $geom(hz) : 0}]
    set blend 1
    if {$opt(blend) && $movie_out && $rig_hz > 0 && $opt(fps) < $rig_hz} {
        set blend [expr {max(1, int(round(double($rig_hz) / $opt(fps))))}]
    }
    set render_fps [expr {$blend > 1 ? $rig_hz : $opt(fps)}]
    set period [expr {1000.0 / $render_fps}]
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
    set movie_frames [expr {[tcl::mathop::+ {*}$writes] / $blend}]

    # the gaze: every eyetracking/raw sample in degrees on the rig clock.
    # A record holds the samples dserv buffered (80 bytes: 10 h,v pairs)
    # stamped with the first one's time; the rest are spread at the median
    # record interval / samples per record.  The calibration is em/settings,
    # as dserv's emconf applies it: linear (scale * (raw - center), then
    # invert on the analog path; the video path's raw is already inverted)
    # or the biquadratic fit.  An exact 0,0 is no sample.
    set eye_t {}; set eye_x {}; set eye_y {}
    set eye_zero 0; set eye_deg_n 0; set eye_cal_n 0
    if {$opt(eye) ne "off" && [llength $eye_recs]} {
        # -eye auto decides for the whole file: a file whose samples all lie
        # within +/-90 is in degrees (the virtual source logs degrees);
        # otherwise they are ADC counts, and an exact 0,0 is the virtual
        # eye at rest mixed in, dropped.  (Per sample would misread a
        # joystick or analog eye pushed near 0 counts.)
        if {$opt(eye) eq "auto"} {
            set big 0
            foreach rec $eye_recs {
                foreach {h v} [lindex $rec 1] { if {abs($h) > 90 || abs($v) > 90} { set big 1; break } }
                if {$big} break
            }
            set eye_mode [expr {$big ? "analog" : "degrees"}]
        } else { set eye_mode $opt(eye) }
        if {$eye_mode in {analog video} && ![llength $em_settings]} {
            puts stderr "eyetracking/raw without em/settings: no calibration; -eye degrees or -eye off"; exit 1
        }
        set cal0 [dict create scale_h 1 scale_v 1 raw_center_h 0 raw_center_v 0 invert_h 0 invert_v 0 \
                      use_biquadratic 0 bq_h_coeffs {} bq_v_coeffs {}]
        # the settings in force from each change on (the first also before it)
        set cals {}
        foreach e [lsort -integer -index 0 $em_settings] {
            set c $cal0; catch { set c [dict merge $cal0 [lindex $e 1]] }
            lappend cals [list [lindex $e 0] $c]
        }
        set ncal [llength $cals]; set ci 0; set cal_changes 0
        set ivals {}
        for {set r 1} {$r < [llength $eye_recs]} {incr r} {
            lappend ivals [expr {[lindex $eye_recs $r 0] - [lindex $eye_recs [expr {$r - 1}] 0]}]
        }
        set per_rec [expr {[llength [lindex $eye_recs 0 1]] / 2}]
        set spacing [expr {[llength $ivals] ? [median $ivals] / double($per_rec) : 2000.0}]
        proc bq {x y c} {
            lassign $c c0 c1 c2 c3 c4 c5 c6 c7 c8
            expr {$c0 + $c1*$x + $c2*$y + $c3*$x*$x + $c4*$y*$y + $c5*$x*$y
                  + $c6*$x*$x*$y + $c7*$x*$y*$y + $c8*$x*$x*$y*$y}
        }
        set cal [expr {$ncal ? [lindex $cals 0 1] : $cal0}]
        dict with cal {}
        foreach rec [lsort -integer -index 0 $eye_recs] {
                lassign $rec ts vals
                while {$ci + 1 < $ncal && [lindex $cals [expr {$ci + 1}] 0] <= $ts} {
                    incr ci; incr cal_changes
                    set cal [lindex $cals $ci 1]; dict with cal {}
                }
                set si 0
                foreach {h v} $vals {
                    set t [expr {($ts + $si * $spacing + $tick_off) / 1000.0}]
                    incr si
                    if {$h == 0 && $v == 0 && $eye_mode ne "degrees"} { incr eye_zero; continue }
                    set mode $eye_mode
                    if {$mode eq "degrees"} { set x $h; set y $v; incr eye_deg_n } \
                    elseif {$use_biquadratic} {
                        set x [bq $h $v $bq_h_coeffs]; set y [bq $h $v $bq_v_coeffs]; incr eye_cal_n
                    } else {
                        set x [expr {$scale_h * ($h - $raw_center_h)}]
                        set y [expr {$scale_v * ($v - $raw_center_v)}]
                        if {$mode eq "analog"} {
                            if {$invert_h} { set x [expr {-$x}] }
                            if {$invert_v} { set y [expr {-$y}] }
                        }
                        incr eye_cal_n
                    }
                    lappend eye_t $t; lappend eye_x $x; lappend eye_y $y
                }
        }
        # in time order (two sources interleave)
        set order [lsort -indices -real $eye_t]
        set eye_t [lmap i $order {lindex $eye_t $i}]
        set eye_x [lmap i $order {lindex $eye_x $i}]
        set eye_y [lmap i $order {lindex $eye_y $i}]
    }
    set n_eye [llength $eye_t]

    if {$opt(dumpplan) ne ""} {
        # the plan, one line per item: F frame, its inputs, then G gaze
        set fd [open $opt(dumpplan) w]
        puts $fd "# replay plan: $path ($span_note)"
        for {set i 0} {$i < $nf} {incr i} {
            set f [lindex $frames $i]
            puts $fd [format "F %d swap=%d tick_ms=%.3f writes=%d" $i [lindex $f 2] [expr {[lindex $f 4] / 1000.0}] [lindex $writes $i]]
            foreach ev $fev($i) {
                lassign $ev t kind p1 p2 p3
                if {$kind eq "cmd"} { puts $fd [format "  cmd t=%.3f %s" $t $p1] } \
                else {
                    set v [expr {[string length $p3] > 120 ? "<[string length $p3] chars [string range $p3 0 39]...>" : $p3}]
                    puts $fd [format "  dp t=%.3f %s=%s" $t $p1 $v]
                }
            }
        }
        foreach t $eye_t x $eye_x y $eye_y { puts $fd [format "G %.3f %.4f %.4f" $t $x $y] }
        close $fd
    }
}

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
 movie $movie_frames frames at $opt(fps) fps = [format %.1f [expr {$movie_frames / double($opt(fps))}]] s\
[expr {$opt(gap_given) ? ", gaps capped at $opt(gap) s" : ""}][expr {$blend > 1 ? ", each the mean of $blend rig ticks" : ""}]"
    puts "          commands placed by ACK stamp: [expr {$ncmd - $cmd_after}][expr {$cmd_before ? " ($cmd_before applied before the first frame)" : ""}][expr {$cmd_after ? ", $cmd_after after the last frame (not drawn)" : ""}]"
    puts "          datapoints placed by ds_dispatched: $k of $ndps[expr {$dp_after ? ", $dp_after after the last frame" : ""}]; $dp_foreign forwarded datapoints were not in the file's streams"
    puts "          stim clock - dserv clock: [format %.3f [expr {$tick_off / 1e6}]] s"
    if {$n_eye} {
        puts "  gaze    $n_eye samples ([expr {$eye_deg_n ? "$eye_deg_n already degrees" : ""}][expr {$eye_deg_n && $eye_cal_n ? ", " : ""}][expr {$eye_cal_n ? "$eye_cal_n calibrated via em/settings" : ""}]), $eye_zero exact 0,0 dropped, -eye $opt(eye) ($eye_mode)[expr {$cal_changes ? ", em/settings changed $cal_changes times in the file" : ""}]"
    } elseif {[llength $eye_recs] && $opt(eye) eq "off"} {
        puts "  gaze    off (-eye off)"
    }
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
set sock [framelock::connect $opt(host) $opt(port)]
set v [framelock::send $sock {package require stimess}]
if {[framelock::is_error $v] || [catch {package vcompare $v 1.1} c] || $c < 0} {
    puts stderr "stim2 at $opt(host):$opt(port) has no stimess 1.1 (got: $v); it cannot take a playback prelude"
    exit 1
}
puts "  stim2   [framelock::send $sock {catch {stimVersion} v; set v}], stimess $v"
set setup [dict create libs $libs script $script \
    stimdg [expr {$stimdg_b64 ne "" ? $stimdg_b64 : [binary encode base64 $stimdg]}] \
    title "replay: [file tail $path] ($span_note)"]
foreach k {halfx halfy w h hz bg} { if {[info exists geom($k)]} { dict set setup geom $k $geom($k) } }
proc say {msg} { puts "  $msg" }
if {[catch {framelock::setup $sock $setup say} wh]} { puts stderr $wh; exit 1 }
lassign $wh win_w win_h
puts "  sourced [file tail $script_path]"

if {$opt(trial) ne "" && [lindex $opt(trial) 0] > 1} { framelock::send $sock reset }

# ---------------------------------------------------------------------------
# -render: frame-locked, in step mode, into a manual frame sink
# ---------------------------------------------------------------------------
if {$opt(render) ne ""} {
    if {[framelock::send $sock {info commands stepFrame}] ne "stepFrame"} {
        puts stderr "this stim2 has no stepFrame (step mode): -render needs the headless/step build"; exit 1
    }
    set out $opt(render)
    # "|command" is always a command (it may well end in .mp4 itself)
    if {$movie_out} {
        set vf [expr {$blend > 1 ? "-vf tmix=frames=$blend,fps=$opt(fps)" : ""}]
        set target "|ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s ${win_w}x${win_h}\
 -r $render_fps -i - $vf -c:v libx264 -preset medium -tune animation -crf $opt(crf)\
 -pix_fmt yuv420p -movflags +faststart [list [file normalize $out]]"
    } else { set target $out }
    set r [framelock::send $sock [list frameSink open -manual $target]]
    if {[framelock::is_error $r]} { puts stderr "frameSink: $r"; exit 1 }
    set t_start [expr {[lindex $frames 0 4] / 1000.0}]
    if {[llength $fev(0)]} { set t_start [expr {min($t_start, [lindex $fev(0) 0 0])}] }
    set t_start [expr {$t_start - 1.0}]
    framelock::send $sock [list stepMode 1 $t_start]
    set clock $t_start; set prevflip $t_start
    # the gaze overlay: a disc at the newest sample, a fading trail behind
    set gaze_trail_ms 150.0; set gaze_hold_ms 100.0
    set eye_k 0; set gaze_last ""
    proc gaze_at {t} {
        # overlay commands for movie time t (rig ms): samples in (t - trail, t]
        global eye_t eye_x eye_y eye_k n_eye gaze_trail_ms gaze_hold_ms
        while {$eye_k < $n_eye && [lindex $eye_t $eye_k] <= $t} { incr eye_k }
        set j [expr {$eye_k - 1}]
        if {$j < 0 || $t - [lindex $eye_t $j] > $gaze_hold_ms} { return [framelock::gaze_cmd "" ""] }
        set pts {}
        for {set i $j} {$i >= 0 && $t - [lindex $eye_t $i] <= $gaze_trail_ms} {incr i -1} {
            lappend pts [lindex $eye_x $i] [lindex $eye_y $i]
        }
        framelock::gaze_cmd [list [lindex $eye_x $j] [lindex $eye_y $j]] $pts
    }
    if {$n_eye} { framelock::send $sock [framelock::gaze_style] }
    set repaints 0
    set sent 0; set errors 0
    set kinds [dict create]
    set wall0 [clock microseconds]
    for {set i 0} {$i < $nf} {incr i} {
        set flip [expr {[lindex $frames $i 4] / 1000.0}]
        set nw [lindex $writes $i]
        # One request per rig frame, however many movie ticks it spans: its
        # commands and datapoints, the gaze, the frame, and its writes --
        # for a frame the rig held while the eye moved, a repaint of the
        # gaze over the same scene at each tick's time. Over a slow link (a
        # Pi on Wi-Fi: ~20 ms a round trip) requests, not drawing, set the
        # pace.
        set pre {}; set post {}
        if {$n_eye} {
            # the gaze at this frame's first movie tick, drawn with the frame
            set g [gaze_at $flip]
            if {$g ne $gaze_last} { append pre "$g\n"; set gaze_last $g }
        }
        if {!$n_eye || $nw <= 1} {
            append post "frameSink write $nw\n"
        } else {
            append post "frameSink write 1\n"
            set same 0
            for {set k 1} {$k < $nw} {incr k} {
                set g [gaze_at [expr {$flip + $k * $period}]]
                if {$g eq $gaze_last} { incr same; continue }
                if {$same} { append post "frameSink write $same\n"; set same 0 }
                append post "$g\nframeRepaint\nframeSink write 1\n"
                set gaze_last $g
                incr repaints
            }
            if {$same} { append post "frameSink write $same\n" }
        }
        set ev_us [lindex $frames $i 8]
        set req [framelock::frame_request $fev($i) clock $prevflip $flip $pre $post \
                     [expr {$ev_us ne "" ? $ev_us / 1000.0 : ""}]]
        set r [framelock::send $sock $req]
        if {[framelock::is_error $r]} { incr errors; puts "  ERROR frame $i (stepFrame at $clock) -> $r"; break }
        lassign $r res errs
        foreach e $errs {
            lassign $e n msg
            lassign [lindex $fev($i) $n] - kind p1
            incr errors
            puts "  ERROR [expr {$kind eq "cmd" ? [string range $p1 0 60] : "inject $p1"}] -> $msg"
        }
        if {$opt(verbose)} {
            foreach ev $fev($i) {
                if {[lindex $ev 1] eq "cmd"} { puts [format "  frame %6d cmd %s" $i [string range [lindex $ev 2] 0 70]] }
            }
        }
        incr sent [llength $fev($i)]
        dict incr kinds [dict get $res kind]
        set prevflip $clock
        if {$opt(verbose) && $i % 600 == 0} { puts "  frame $i of $nf" }
    }
    set elapsed [expr {([clock microseconds] - $wall0)/1e6}]
    set st [framelock::send $sock {frameSink close}]
    framelock::send $sock {stepMode 0}
    if {$n_eye} { framelock::send $sock {overlay delete gaze; overlay delete gaze_trail} }
    puts "done: $i rig frames drawn ([join [lmap {k v} $kinds {list $k $v}] {, }]), $sent events\
 ($errors errors, $skipped configure-time commands skipped) in [format %.1f $elapsed] s"
    if {$n_eye} { puts "  gaze: $n_eye samples over the movie, $repaints held-frame repaints" }
    set mf [expr {[dict get $st frames] / $blend}]
    puts "  movie: $mf frames at $opt(fps) fps\
 ([format %.1f [expr {$mf / double($opt(fps))}]] s)[expr {$blend > 1 ? ", each the mean of $blend rig ticks" : ""}]\
 -> $out, status [dict get $st status]"
    foreach n [dict keys $stream_set] {
        # a stimess binding reports its count (stimdg is stim2's own readdg)
        set c [framelock::send $sock [list stim::count $n]]
        if {![framelock::is_error $c]} { puts "  $n: $c injected, last [framelock::send $sock [list stim::get $n]]" }
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
set swap0 [framelock::send $sock {set SwapCount}]
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
        if {[framelock::skipped_cmd $p1]} { incr skipped; continue }
        set r [framelock::send $sock $p1]
        if {[framelock::is_error $r]} { incr errors; puts "  ERROR [string range $p1 0 60] -> $r" } \
        elseif {$opt(verbose)} { puts [format "  %+8.3f cmd %s" [expr {($ts - $t0)/1e6}] [string range $p1 0 70]] }
    } else {
        set r [framelock::send $sock [list stim::inject $p1 $p3 $p2 $ts]]
        if {[framelock::is_error $r]} { incr errors; puts "  ERROR inject $p1 -> $r" } \
        elseif {$opt(verbose)} { puts [format "  %+8.3f dp  %s = %s" [expr {($ts - $t0)/1e6}] $p1 $p3] }
    }
    incr sent
}
set elapsed [expr {([clock microseconds] - $wall0)/1e6}]
set swaps [expr {[framelock::send $sock {set SwapCount}] - $swap0}]
puts "done: $sent events in [format %.1f $elapsed] s ($errors errors, $skipped configure-time commands skipped),\
 stim2 swapped $swaps frames"
foreach n [dict keys $stream_set] {
    set c [framelock::send $sock [list stim::count $n]]
    if {![framelock::is_error $c]} { puts "  $n: $c injected, last [framelock::send $sock [list stim::get $n]]" }
}
close $sock
exit [expr {$errors ? 1 : 0}]
