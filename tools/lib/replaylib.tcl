# replaylib.tcl -- replaying a recorded session into stim2, as a library:
# read a file once, then play or render any part of it, as often as asked.
#
#     source [file join [file dirname [info script]] .. lib replaylib.tcl]
#
#     set S [replay::load FILE.obs.dgz]      ;# or FILE.ess
#     set P [replay::plan $S -trials {7 7} -render out.mp4]
#     set sock [replay::connect localhost 5612]
#     replay::check_stimess $sock
#     framelock::setup $sock [replay::setup_dict $S "replay: ..."] replay::say
#     replay::render $S $P $sock
#
#     replay::render_trial $S $sock 7 out/t007.mp4   ;# all of the above for
#                                                    ;# one trial, + timing JSON
#
# tools/replay/replay.tcl is the command line over this; docs/replay.md
# says what a replay does and does not reproduce.  The frame-locked core
# (placement, the frame request, setup) is framelock.tcl, shared with the
# live mirror.
#
# A session S is a namespace holding what was read from the file; it is
# not changed by planning or rendering, so one load serves any number of
# plans.  A plan P is a dict (replay::plan).  Errors are Tcl errors with
# -errorcode {REPLAY <exit code>}: 2 for a usage problem, 1 otherwise.
#
# Logging: procs that report as they go take -log, a command prefix given
# one line (no leading indentation); the default, replay::say, prints it
# indented by two spaces.

package require dslog
package require tcljson
source [file join [file dirname [file normalize [info script]]] framelock.tcl]

namespace eval replay {
    variable nsess 0
    variable here [file normalize [info script]]
    # what load fills in a session's namespace (geom is an array)
    variable state_vars {path script script_path script_meta libs bind_names stimdg
        stimdg_b64 obs_begin obs_end events frames eye_recs em_settings counts
        hashed stream_set records obs_note obs_frames_narrow geom touches}
}

proc replay::say {msg} { puts "  $msg" }
proc replay::fail {code msg} { return -code error -errorcode [list REPLAY $code] $msg }
proc replay::median {l} { set l [lsort -real $l]; lindex $l [expr {[llength $l] / 2}] }

# link the caller's variables to session S's
proc replay::_vars {S} {
    variable state_vars
    foreach v $state_vars { uplevel 1 [list upvar #0 ${S}::$v $v] }
}

# a session variable's value (geom as a dict)
proc replay::get {S name} {
    upvar #0 ${S}::$name v
    if {[array exists v]} { return [array get v] }
    return $v
}
proc replay::ntrials {S} { llength [set ${S}::obs_end] }
proc replay::delete {S} { catch {namespace delete $S} }

# -each: the movie file for one trial. OUT with a % format takes the trial
# number (out/planko_t%03d.mp4); otherwise _tNNN goes before the extension.
proc replay::each_name {out trial} {
    if {[string first % $out] >= 0} { return [format $out $trial] }
    return "[file rootname $out]_t[format %03d $trial][file extension $out]"
}
# OUT is a movie file (through ffmpeg), not raw RGBA to a file or "|command"
proc replay::is_movie {out} {
    expr {$out ne "" && [string index $out 0] ne "|" && [regexp -nocase {\.(mp4|mov|mkv)$} $out]}
}

# ---------------------------------------------------------------------------
# load: read a .ess or an obs DG into a new session
#   -streams {NAME ...}   also inject these records (the stim/bind table's
#                         streams always are)
#   -geometry {HX HY ?HZ?} the rig's half-screen degrees (and refresh), for a
#                         file without ess/screen_* records or over them
#   -bg {R G B}           the background at file open (0-255), likewise
# ---------------------------------------------------------------------------
proc replay::load {file args} {
    variable nsess
    array set o {-streams {} -geometry {} -bg {}}
    foreach {k v} $args {
        if {![info exists o($k)]} { fail 2 "replay::load: unknown option $k" }
        set o($k) $v
    }
    set S ::replay::s[incr nsess]
    namespace eval $S {}
    _vars $S
    set script ""; set script_path ""; set script_meta {}; set libs {}; set bind_names {}
    set stimdg ""; set stimdg_b64 ""; set obs_begin {}; set obs_end {}
    set events {}          ;# {ts kind payload...}, kinds: cmd | dp
    set frames {}          ;# stim/frame: {dserv_ts frame swapcount flip_us ticks_us missed cmds ds eval_us}
    set eye_recs {}        ;# eyetracking/raw: {dserv_ts {h v h v ...}}, 10 samples a record
    set em_settings {}     ;# em/settings: {dserv_ts settings} each time it changed
    set touches {}         ;# ESS TOUCH events: {dserv_ts subtype px py}, subtype 0 press 1 release 2 move
    set counts [dict create]
    set hashed 0
    set stream_set [dict create]
    set records {}
    set obs_note ""
    set obs_frames_narrow 0   ;# an obs DG whose stim/frame columns are there but empty
    array set geom {}
    set path $file
    foreach s $o(-streams) { dict set stream_set $s 1 }

    if {[catch {
        if {[regexp -nocase {\.dgz?$} $path]} { _read_obs $S $path } else { _read_ess $S $path }
    } e opts]} {
        delete $S
        return -options $opts $e
    }

    if {[llength $o(-bg)]} { set geom(bg) $o(-bg) }
    if {[llength $o(-geometry)]} {
        lassign $o(-geometry) geom(halfx) geom(halfy) hz
        if {$hz ne ""} { set geom(hz) $hz }
    }
    foreach n $bind_names { dict set stream_set $n 1 }
    dict set stream_set stimdg 1      ;# stim2 subscribes to it (readdg)
    foreach r $records {
        lassign $r ts nm type data
        if {[dict exists $stream_set $nm]} { lappend events [list $ts dp $nm $type $data] }
    }
    set records {}
    set events [lsort -integer -index 0 $events]
    set eye_recs [lsort -integer -index 0 $eye_recs]
    set touches [lsort -integer -index 0 $touches]

    set err ""
    if {$script eq ""} { set err "no stim/script with a body in $path -- nothing to source" } \
    elseif {$stimdg eq ""} { set err "no stimdg record in $path" } \
    elseif {[llength $events] == 0} { set err "no stim/cmd or bound-stream records in $path" }
    if {$err ne ""} { delete $S; fail 1 $err }
    return $S
}

# ---------------------------------------------------------------------------
# the .ess reader
# ---------------------------------------------------------------------------
proc replay::_read_ess {S fname} {
    _vars $S
    set h [dslog::open $path r]
    set evt_begin {}; set evt_end {}
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
            eventlog/events {
                # an ESS event: type word EVT(9) | type<<8 | subtype<<16 |
                # puttype<<24.  TOUCH (48) carries the screen pixel x y
                # carries the screen pixel x y; BEGINOBS (19) and ENDOBS (20)
                # mark the periods in a file without logger:beginobs records
                set ty [dict get $d type]
                if {($ty & 0xff) == 9} {
                    set et [expr {($ty >> 8) & 0xff}]
                    if {$et == 48 && [binary scan [dict get $d data] f2 xy] == 1} {
                        lappend touches [list [dict get $d timestamp] [expr {($ty >> 16) & 0xff}] {*}$xy]
                    } elseif {$et == 19} {
                        lappend evt_begin [dict get $d timestamp]
                    } elseif {$et == 20} {
                        lappend evt_end [dict get $d timestamp]
                    }
                }
            }
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
    if {![llength $obs_begin] && [llength $evt_begin]} {
        set obs_begin $evt_begin; set obs_end $evt_end
    }
}

# ---------------------------------------------------------------------------
# _read_obs: the same structures from dfconf's obs DG (.obs.dgz), not the .ess.
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
proc replay::_cmd_event {S v} {
    _vars $S
    dict incr counts stim/cmd
    set c [framelock::cmd_record $v]
    if {$c eq ""} { incr hashed; return }
    lassign $c t_send cmd seq ack
    lappend events [list $t_send cmd $cmd $seq $ack]
}
# dserv time of session record k of dp, or "" when untimed
proc replay::_sess_time {stimes anchor dp k} {
    if {$anchor eq "" || ![dict exists $stimes $dp]} { return "" }
    set t [lindex [dict get $stimes $dp] 0 $k]
    expr {$t eq "" ? "" : wide($anchor + $t * 1000.0)}
}

proc replay::_read_obs {S path} {
    set g [dg_read $path]
    try {
        _read_obs_dg $S $path $g
    } finally {
        catch { dg_delete $g }
    }
}

proc replay::_read_obs_dg {S fname g} {
    _vars $S
    set names [dg_tclListnames $g]
    # the absolute anchor (a wide DG from a dlsh that writes it): the file's
    # first record on the dserv clock, and each BEGINOBS from it in us
    set A ""
    if {[dl_exists $g:file_t0_us] && [dl_exists $g:obs_start_us]} {
        set A [dl_get $g:file_t0_us 0]
    }
    if {![dl_exists $g:obs_start_ms] || ![dl_exists $g:e_types]} {
        fail 1 "$path is not an obs DG (no obs_start_ms / e_types)"
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
            if {[dict get $j body] ne ""} {
                set script [dict get $j body]; set script_path [dict get $j path]
                set script_meta $j
            }
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
        fail 1 "$path: no file_t0_us and no stim/cmd records in observation periods to set the clock by"
    }
    set off_all [expr {[llength $all_off] ? [median $all_off] : 0}]
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
        # TOUCH events (48): {subtype} {px py}, ms from BEGINOBS
        set kt [lsearch -all -exact $ety 48]
        if {[llength $kt]} {
            set est [dl_tcllist $g:e_subtypes:$i]; set ep [dl_tcllist $g:e_params:$i]
            foreach k $kt {
                lassign [lindex $ep $k] px py
                if {[string is double -strict $px] && [string is double -strict $py]} {
                    lappend touches [list [expr {wide($t0 + [lindex $et $k] * 1000.0)}] [lindex $est $k] $px $py]
                }
            }
        }
        foreach n $names {
            if {![string match <ds>* $n]} continue
            set dp [string range $n 4 end]
            set vals [dl_tcllist $g:$n:$i]
            set ts [lmap t [dl_tcllist $g:<dst>$dp:$i] {expr {wide($t0 + $t * 1000.0)}}]
            set cnt [dl_tcllist $g:<dsn>$dp:$i]
            if {$dp eq "stim/cmd"} {
                foreach v $vals { _cmd_event $S $v }
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
    if {[dict exists $sess stim/cmd]} { foreach v [dict get $sess stim/cmd] { _cmd_event $S $v } }
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
            set t [_sess_time $stimes $anchor stim/frame $k]
            if {$t eq ""} { set t [expr {wide([lindex $f 3] - $toff)}] }
            lappend frames [list $t {*}$f]
            incr k
        }
    }
    if {[dict exists $sess em/settings]} {
        set ems [dict get $sess em/settings]
        if {[_sess_time $stimes $anchor em/settings 0] ne ""} {
            set k 0
            foreach e $ems { lappend em_settings [list [_sess_time $stimes $anchor em/settings $k] $e]; incr k }
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
        if {[_sess_time $stimes $anchor $dp 0] eq ""} continue
        set cnt [lindex [dict get $stimes $dp] 1]
        set p 0; set k 0
        foreach c $cnt {
            set data [expr {$c == 1 ? [lindex $vals $p] : [join [lrange $vals $p [expr {$p + $c - 1}]] " "]}]
            lappend records [list [_sess_time $stimes $anchor $dp $k] $dp 1 $data]
            incr p $c; incr k
        }
    }
    dict set counts stim/cmd [expr {[dict exists $counts stim/cmd] ? [dict get $counts stim/cmd] : 0}]
    # stim/frame is DSERV_INT64: a dslog converting with wide types off
    # (dfconf's `df wide_datafiles`, off by default before dserv 0.60.16)
    # keeps the columns and drops every value, so the frames were in the
    # .ess but are not here
    if {![llength $frames] && [dl_exists $g:<ds>stim/frame]} { set obs_frames_narrow 1 }
}

# ---------------------------------------------------------------------------
# plan: what to play, as a dict.
#   -trials {A B}   trials A..B, 1-based by observation period (default all).
#                   Trial i is (endobs_{i-1}, endobs_i], so the commands that
#                   set it up (nexttrial, targets_on) travel with it
#   -render OUT     also plan a frame-locked render into OUT (below)
#   -each 0|1       with -render: one movie per trial
#   -fps F          the movie's frame rate (default the rig's refresh, or 60)
#   -blend 0|1      below the rig's refresh into a movie file, draw every rig
#                   tick and average them (default 1); 0 drops ticks
#   -gap S          with -render, compress idle stretches longer than S s
#                   (default "": none)
#   -eye MODE       with -render: auto analog video degrees off (default auto)
#   -touch 0|1      with -render: draw the touchscreen presses (ESS TOUCH
#                   events) as rings (default 0)
#   -lead 0|1       with -render (not -each): the movie starts at the first
#                   trial's BEGINOBS, not at its first rig frame (default
#                   0).  stim2 draws only when something changes, so the
#                   first new frame often comes well after BEGINOBS (a
#                   fixation period with gaze can be seconds): that stretch
#                   is a lead of the rig's background, with the gaze and
#                   touches over it.  Frames from before BEGINOBS (between
#                   the previous ENDOBS and it) are still drawn, so the
#                   trial's state builds up as on the rig, but not written
#
# What the movie starts at is t0 (rig ms) in the plan: BEGINOBS with
# -lead, else the first frame's flip.  lead is the movie ticks before the
# first frame (0 without -lead, or when a frame came before BEGINOBS), and
# pos a frame's first tick from t0 (negative for one before it).
#
# Always in the dict: trials (as given), span_note, events (those of the
# window), ncmd, ndp, ntrials, t_first, t_last (of the whole file), w0 w1
# (the window on the dserv clock, "" for the whole file).  With -render:
# the frame-locked plan -- frames, fev (each frame's inputs), writes (movie
# ticks per frame), pos (each frame's first tick), t0, lead, ftrial, movie_each,
# movie_frames, fps, blend, render_fps, period, tick_off, the placement
# counts and the gaze (eye_t eye_x eye_y and its counts).
# ---------------------------------------------------------------------------
proc replay::plan {S args} {
    array set o {-trials "" -render "" -each 0 -fps "" -blend 1 -gap "" -eye auto -touch 0 -lead 0}
    foreach {k v} $args {
        if {![info exists o($k)]} { fail 2 "replay::plan: unknown option $k" }
        set o($k) $v
    }
    if {$o(-eye) ni {auto analog video degrees off}} { fail 2 "-eye wants auto|analog|video|degrees|off" }
    if {![string is boolean -strict $o(-touch)]} { fail 2 "-touch wants 0|1" }
    if {![string is boolean -strict $o(-lead)]} { fail 2 "-lead wants 0|1" }
    if {$o(-each) && ![is_movie $o(-render)]} {
        fail 2 "-each wants -render OUT with OUT a movie file (.mp4/.mov/.mkv): one is written per trial"
    }
    if {$o(-each) && $o(-lead)} { fail 2 "-lead goes with one movie, not -each" }
    # the session's data, as local copies: planning leaves the session as is
    foreach v {path events frames eye_recs em_settings obs_begin obs_end obs_frames_narrow} {
        set $v [set ${S}::$v]
    }
    array set geom [array get ${S}::geom]

    # trial windows
    set ntrials [llength $obs_end]
    set t_first [lindex $events 0 0]
    set t_last  [lindex $events end 0]
    set all_events $events
    set w0 ""; set w1 ""
    if {$o(-trials) ne ""} {
        lassign $o(-trials) a b
        if {$a < 1 || $b > $ntrials || $a > $b} {
            fail 1 "trials $a-$b out of range: the file has $ntrials observation periods"
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
    set keys {path span_note events ncmd ndp ntrials t_first t_last w0 w1}
    set R [dict create trials $o(-trials) render $o(-render) each $o(-each) eye $o(-eye)]
    if {$o(-render) eq ""} {
        foreach v $keys { dict set R $v [set $v] }
        return $R
    }

    # -render: the frame-locked plan (framelock.tcl).  stim/frame has one
    # record per rig swap: its swapcount, the stim clock at the flip
    # (ticks_us), and how many commands and forwarded datapoints stim2 had
    # applied before drawing it.  Each rig frame is drawn once, after
    # exactly the inputs it saw: commands by their ACK stamp, datapoints by
    # the frame's ds_dispatched advance, then stepFrame at the instant the
    # rig computed it, with the frame's own flip.  The movie gets each frame
    # once per movie tick it spans on the rig's timeline: static stretches
    # hold, frames shorter than a tick drop.
    if {![llength $frames]} {
        if {$obs_frames_narrow} {
            fail 1 "-render needs stim/frame records: $path has the stim/frame columns but they\
 are empty, because the .ess was converted with wide types off (the rig's `df wide_datafiles` setting;\
 stim/frame is int64). Render from the .ess, or reconvert it with `dslog::wideTypes 1` first."
        }
        fail 1 "-render needs stim/frame records, and $path has none (recorded before stimFrameLog)"
    }
    set frames [lsort -integer -index 2 $frames]
    if {$w0 ne ""} {
        set sel {}
        foreach f $frames { set ts [lindex $f 0]; if {$ts > $w0 && $ts <= $w1} { lappend sel $f } }
        set frames $sel
        if {![llength $frames]} { fail 1 "no stim/frame records in $span_note" }
    }
    # -each: a frame belongs to the trial whose window (endobs_{i-1},
    # endobs_i] holds it; frames after the last period are in no trial
    set ftrial {}
    if {$o(-each)} {
        set sel {}; set t 0
        foreach f $frames {
            set ts [lindex $f 0]
            while {$t < $ntrials && $ts > [lindex $obs_end $t]} { incr t }
            if {$t >= $ntrials} break
            lappend sel $f; lappend ftrial [expr {$t + 1}]
        }
        set frames $sel
        if {![llength $frames]} { fail 1 "no stim/frame records inside an observation period in $span_note" }
    }
    set nf [llength $frames]
    set sc_first [lindex $frames 0 2]; set sc_last [lindex $frames end 2]
    set nmissing [expr {$sc_last - $sc_first + 1 - $nf}]
    # stim clock minus dserv clock, us: places a datapoint on the rig's clock
    set offs {}
    foreach f $frames { lappend offs [expr {[lindex $f 4] - [lindex $f 0]}] }
    set tick_off [median $offs]
    set FP [framelock::plan_new $tick_off]

    # commands; those the rig applied after its last frame here are not drawn
    set cmd_before 0; set cmd_after 0; set no_ack 0; set skipped 0
    foreach e $events {
        if {[lindex $e 1] ne "cmd"} continue
        lassign [lindex $e 4] swap flip dq
        if {$swap eq "" || $dq eq ""} { incr no_ack; continue }
        set tf [framelock::cmd_target $swap $flip [lindex $e 2]]
        if {$tf < $sc_first} { incr cmd_before } elseif {$tf > $sc_last} { incr cmd_after; continue }
        if {[framelock::skipped_cmd [lindex $e 2]]} { incr skipped; continue }
        framelock::plan_cmd FP $swap $flip $dq [lindex $e 2]
    }
    if {$no_ack} {
        fail 1 "$no_ack commands carry no ACK stamps (swap, dq): -render cannot place them"
    }
    # datapoints: for a trial window, each stream's last record before the
    # window primes the first frame (a cursor stands where it stood)
    if {$w0 ne ""} {
        set last [dict create]
        foreach e $all_events { if {[lindex $e 1] eq "dp" && [lindex $e 0] <= $w0} { dict set last [lindex $e 2] $e } }
        foreach {n e} $last { framelock::plan_prime FP [lindex $e 0] {*}[lrange $e 2 end] }
    }
    set ndps 0
    foreach e $events { if {[lindex $e 1] eq "dp"} { framelock::plan_dp FP [lindex $e 0] {*}[lrange $e 2 end]; incr ndps } }
    set fev {}
    for {set i 0} {$i < $nf} {incr i} { lappend fev [framelock::plan_frame FP [lindex $frames $i]] }
    set dp_after [llength [dict get $FP dps]]
    set dp_placed [expr {$ndps - $dp_after}]
    set dp_foreign [dict get $FP foreign]

    # the movie: output ticks per rig frame, from the flips (ms)
    set fps $o(-fps)
    if {$fps eq ""} {
        set fps [expr {[info exists geom(hz)] && $geom(hz) > 0 ? $geom(hz) : 60}]
    }
    # blending: below the rig's refresh into a movie file, draw at the rig's
    # rate and let ffmpeg average each group of `blend` ticks into one frame
    set movie_out [is_movie $o(-render)]
    set rig_hz [expr {[info exists geom(hz)] && $geom(hz) > 0 ? $geom(hz) : 0}]
    set blend 1
    if {$o(-blend) && $movie_out && $rig_hz > 0 && $fps < $rig_hz} {
        set blend [expr {max(1, int(round(double($rig_hz) / $fps)))}]
    }
    set render_fps [expr {$blend > 1 ? $rig_hz : $fps}]
    set period [expr {1000.0 / $render_fps}]
    set gap_ms [expr {$o(-gap) ne "" ? $o(-gap) * 1000.0 : 1e300}]
    set F0 [expr {[lindex $frames 0 4] / 1000.0}]
    # the movie's start, t0: the first frame's flip, or with -lead the
    # window's first BEGINOBS on the rig clock
    set t0 $F0; set start frame
    if {$o(-lead)} {
        set ta [expr {$o(-trials) ne "" ? [lindex $o(-trials) 0] : 1}]
        set ob [lindex $obs_begin [expr {$ta - 1}]]
        if {$ob ne ""} { set t0 [expr {($ob + $tick_off) / 1000.0}]; set start beginobs }
    }
    set shift 0.0; set pos {}; set prevF $F0
    foreach f $frames {
        set F [expr {[lindex $f 4] / 1000.0}]
        if {$F - $prevF > $gap_ms} { set shift [expr {$shift + $F - $prevF - $gap_ms}] }
        set prevF $F
        lappend pos [expr {round(($F - $t0 - $shift) / $period)}]
    }
    # ticks before the first frame: the lead.  A frame before t0 is drawn
    # (its inputs build the scene) but written only from t0 on, so frames
    # wholly before it get no ticks
    set lead [expr {max(0, [lindex $pos 0])}]
    set writes {}
    for {set i 0} {$i < $nf - 1} {incr i} {
        lappend writes [expr {max(0, [lindex $pos [expr {$i + 1}]] - max(0, [lindex $pos $i]))}]
    }
    lappend writes 1
    # -each: a trial's last frame holds for one movie frame; the rig's idle
    # time up to the next trial's first frame belongs to the next movie
    set movie_each [dict create]
    if {$o(-each)} {
        for {set i 0} {$i < $nf - 1} {incr i} {
            if {[lindex $ftrial $i] != [lindex $ftrial [expr {$i + 1}]]} {
                lset writes $i [expr {min([lindex $writes $i], $blend)}]
            }
        }
        foreach w $writes t $ftrial { dict incr movie_each $t $w }
    }
    set movie_frames [expr {($lead + [tcl::mathop::+ {*}$writes]) / $blend}]

    # the gaze: every eyetracking/raw sample in degrees on the rig clock.
    # A record holds the samples dserv buffered (80 bytes: 10 h,v pairs)
    # stamped with the first one's time; the rest are spread at the median
    # record interval / samples per record.  The calibration is em/settings,
    # as dserv's emconf applies it: linear (scale * (raw - center), then
    # invert on the analog path; the video path's raw is already inverted)
    # or the biquadratic fit.  An exact 0,0 is no sample.
    set eye_t {}; set eye_x {}; set eye_y {}
    set eye_zero 0; set eye_deg_n 0; set eye_cal_n 0; set eye_mode ""; set cal_changes 0
    if {$o(-eye) ne "off" && [llength $eye_recs]} {
        # -eye auto decides for the whole file: a file whose samples all lie
        # within +/-90 is in degrees (the virtual source logs degrees);
        # otherwise they are ADC counts, and an exact 0,0 is the virtual
        # eye at rest mixed in, dropped.  (Per sample would misread a
        # joystick or analog eye pushed near 0 counts.)
        if {$o(-eye) eq "auto"} {
            set big 0
            foreach rec $eye_recs {
                foreach {h v} [lindex $rec 1] { if {abs($h) > 90 || abs($v) > 90} { set big 1; break } }
                if {$big} break
            }
            set eye_mode [expr {$big ? "analog" : "degrees"}]
        } else { set eye_mode $o(-eye) }
        if {$eye_mode in {analog video} && ![llength $em_settings]} {
            fail 1 "eyetracking/raw without em/settings: no calibration; -eye degrees or -eye off"
        }
        set cal0 [dict create scale_h 1 scale_v 1 raw_center_h 0 raw_center_v 0 invert_h 0 invert_v 0 \
                      use_biquadratic 0 bq_h_coeffs {} bq_v_coeffs {}]
        # the settings in force from each change on (the first also before it)
        set cals {}
        foreach e [lsort -integer -index 0 $em_settings] {
            set c $cal0; catch { set c [dict merge $cal0 [lindex $e 1]] }
            lappend cals [list [lindex $e 0] $c]
        }
        set ncal [llength $cals]; set ci 0
        set ivals {}
        for {set r 1} {$r < [llength $eye_recs]} {incr r} {
            lappend ivals [expr {[lindex $eye_recs $r 0] - [lindex $eye_recs [expr {$r - 1}] 0]}]
        }
        set per_rec [expr {[llength [lindex $eye_recs 0 1]] / 2}]
        set spacing [expr {[llength $ivals] ? [median $ivals] / double($per_rec) : 2000.0}]
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

    # -touch: the touchscreen presses, as markers on the rig clock
    set touch [expr {$o(-touch) ? [touch_marks $S $tick_off $w0 $w1] : {}}]
    set n_touch [llength $touch]

    lappend keys frames fev writes pos t0 lead start ftrial movie_each movie_frames nf sc_first sc_last nmissing \
        tick_off cmd_before cmd_after skipped ndps dp_after dp_placed dp_foreign fps blend \
        render_fps period movie_out eye_t eye_x eye_y n_eye eye_zero eye_deg_n eye_cal_n \
        eye_mode cal_changes touch n_touch
    foreach v $keys { dict set R $v [set $v] }
    dict set R gap_given [expr {$o(-gap) ne ""}]
    return $R
}

# ---------------------------------------------------------------------------
# touch_marks: session S's touches in (w0, w1] (dserv us; "" the whole
# file) as strokes, each {t x y t_up x_up y_up path}: down at t (ms on the
# rig clock: dserv + tick_off) at x y degrees, up at t_up at x_up y_up
# ("" "" "" when no release was logged: ESS logs only inside observation
# periods), and path {t x y t x y ...} the finger's way from the first
# sample through every TOUCH MOVE to the release, "" when the stroke had
# no moves (a tap, or a file from before ESS logged drags: the finger's
# way between press and release is then unknown).  A MOVE with no PRESS
# open starts a stroke: the finger was down since before the period (ESS
# logs one at BEGINOBS then).  Pixels become degrees as ESS's
# touch_pixels_to_deg does, from the session's ess/screen_w/h/halfx/halfy;
# without those there are no strokes.
# ---------------------------------------------------------------------------
proc replay::touch_marks {S tick_off {w0 ""} {w1 ""}} {
    array set geom [array get ${S}::geom]
    foreach k {w h halfx halfy} {
        if {![info exists geom($k)] || ![string is double -strict $geom($k)] || $geom($k) <= 0} { return {} }
    }
    set cx [expr {int($geom(w)) / 2}]; set cy [expr {int($geom(h)) / 2}]
    set sx [expr {$geom(w) / (2.0 * $geom(halfx))}]
    set sy [expr {$geom(h) / (2.0 * $geom(halfy))}]
    set marks {}; set down ""
    # a stroke: {t x y path moves}; closed into a mark
    set close {{down up} {
        lassign $down t x y path moves
        if {$up eq ""} { set up {"" "" ""} }
        list $t $x $y {*}$up [expr {$moves ? $path : ""}]
    }}
    foreach e [set ${S}::touches] {
        lassign $e ts sub px py
        if {$w0 ne "" && ($ts <= $w0 || $ts > $w1)} continue
        set t [expr {($ts + $tick_off) / 1000.0}]
        set x [expr {($px - $cx) / $sx}]; set y [expr {($cy - $py) / $sy}]
        switch -- $sub {
            0 {
                if {$down ne ""} { lappend marks [apply $close $down ""] }
                set down [list $t $x $y [list $t $x $y] 0]
            }
            2 {
                if {$down eq ""} {
                    set down [list $t $x $y [list $t $x $y] 0]
                } else {
                    lset down 3 [concat [lindex $down 3] $t $x $y]
                    lset down 4 [expr {[lindex $down 4] + 1}]
                }
            }
            1 {
                if {$down eq ""} continue
                lset down 3 [concat [lindex $down 3] $t $x $y]
                lappend marks [apply $close $down [list $t $x $y]]
                set down ""
            }
        }
    }
    if {$down ne ""} { lappend marks [apply $close $down ""] }
    return $marks
}

# the touches for movie time t (rig ms), the newest touch_slots strokes:
# a ring where the finger is (the stroke's last sample by t), with a trail
# of its path over the last touch_trail_ms, from touch-down until the
# release (touch_hold_ms past the last sample when none was logged), then
# at the release point fading over touch_fade_ms, its trail with it.  A
# stroke without a path (a tap) is a ring at the press, then at the
# release.
proc replay::_touch_at {t} {
    upvar 1 touch touch touch_r touch_r touch_rgb touch_rgb touch_a touch_a \
        touch_hold_ms hold touch_fade_ms fade touch_trail_ms trail
    set vis {}
    foreach m $touch {
        lassign $m on x y up xu yu path
        if {$on > $t} break
        if {$up eq ""} {
            set last [expr {$path eq "" ? $on : [lindex $path end-2]}]
            set up [expr {$last + $hold}]
            if {$path ne ""} { set x [lindex $path end-1]; set y [lindex $path end] }
            set xu $x; set yu $y
        }
        if {$t >= $up + $fade} continue
        set f [expr {$t < $up ? 1.0 : 1.0 - ($t - $up) / $fade}]
        set tt [expr {min($t, $up)}]
        # the finger at tt: the last path sample by then, newest first
        # behind it the trail
        set px $x; set py $y; set tr {}
        if {$path ne ""} {
            set n [expr {[llength $path] / 3}]
            for {set i [expr {$n - 1}]} {$i >= 0} {incr i -1} {
                set ts [lindex $path [expr {3 * $i}]]
                if {$ts > $tt} continue
                if {$tt - $ts > $trail} break
                lappend tr [lindex $path [expr {3 * $i + 1}]] [lindex $path [expr {3 * $i + 2}]]
            }
            if {[llength $tr]} { lassign $tr px py }
        } elseif {$t >= $up} {
            set px $xu; set py $yu
        }
        lappend vis [list $px $py $f $tr]
    }
    framelock::touch_cmd [lrange $vis end-[expr {$::framelock::touch_slots - 1}] end] \
        $touch_r $touch_rgb $touch_a
}

# the biquadratic eye fit, as dserv's em applies it
proc replay::bq {x y c} {
    lassign $c c0 c1 c2 c3 c4 c5 c6 c7 c8
    expr {$c0 + $c1*$x + $c2*$y + $c3*$x*$x + $c4*$y*$y + $c5*$x*$y
          + $c6*$x*$x*$y + $c7*$x*$y*$y + $c8*$x*$x*$y*$y}
}

# the frame-locked plan, one line per item: F frame, its inputs, then G
# gaze -- what two readings of one session can be compared on
proc replay::dumpplan {P file} {
    set fd [open $file w]
    puts $fd "# replay plan: [dict get $P path] ([dict get $P span_note])"
    set frames [dict get $P frames]; set writes [dict get $P writes]
    set i 0
    foreach f $frames ev_list [dict get $P fev] {
        puts $fd [format "F %d swap=%d tick_ms=%.3f writes=%d" $i [lindex $f 2] [expr {[lindex $f 4] / 1000.0}] [lindex $writes $i]]
        foreach ev $ev_list {
            lassign $ev t kind p1 p2 p3
            if {$kind eq "cmd"} { puts $fd [format "  cmd t=%.3f %s" $t $p1] } \
            else {
                set v [expr {[string length $p3] > 120 ? "<[string length $p3] chars [string range $p3 0 39]...>" : $p3}]
                puts $fd [format "  dp t=%.3f %s=%s" $t $p1 $v]
            }
        }
        incr i
    }
    foreach t [dict get $P eye_t] x [dict get $P eye_x] y [dict get $P eye_y] { puts $fd [format "G %.3f %.4f %.4f" $t $x $y] }
    close $fd
}

# ---------------------------------------------------------------------------
# stim2: launch one, connect, check it, set it up
# ---------------------------------------------------------------------------
proc replay::alive {pid} { expr {![catch {exec kill -0 $pid}]} }

# Start a stim2 on this machine; returns its pid.  With -render 1 it is
# headless at the rig's pixel size (geom w h, 1920x1080 when the file does
# not say) and refresh, so a movie is the rig's picture; otherwise a window
# in the rig's aspect, no wider than 960.
#   -port P (the message port; the stim2 gets --port-base P-2)  -geom DICT
#   -render 0|1  -size WxH  -exe PATH (default: the app bundle these tools
#   came with, else the installed one)  -log CMD
proc replay::launch {args} {
    variable here
    array set o {-port 5612 -geom {} -render 0 -size "" -exe "" -log replay::say}
    foreach {k v} $args {
        if {![info exists o($k)]} { fail 2 "replay::launch: unknown option $k" }
        set o($k) $v
    }
    set exe $o(-exe)
    # the stim2 these tools came with: an app bundle's own binary when run
    # from its Resources/tools, else the installed one
    if {$exe eq "" && [regexp {^(.*\.app)/Contents/Resources/tools/} $here -> app]} {
        set exe $app/Contents/MacOS/stim2
    }
    if {$exe eq ""} {
        set exe [expr {$::tcl_platform(os) eq "Darwin" ? "/Applications/stim2.app/Contents/MacOS/stim2" : "/usr/local/stim2/stim2"}]
    }
    if {![file executable $exe]} { fail 2 "-launch: no stim2 at $exe (-stim2 PATH)" }
    if {![catch {socket localhost $o(-port)} s]} {
        close $s
        fail 2 "-launch: a stim2 already answers on port $o(-port); stop it, pick another -port, or run without -launch"
    }
    set g $o(-geom)
    set rw ""; set rh ""
    if {[dict exists $g w] && [dict exists $g h] && [string is integer -strict [dict get $g w]] \
            && [string is integer -strict [dict get $g h]] && [dict get $g w] > 0 && [dict get $g h] > 0} {
        set rw [dict get $g w]; set rh [dict get $g h]
    }
    set args [list --port-base [expr {$o(-port) - 2}] --title "stim2 replay"]
    if {$o(-render)} {
        # the movie's pixels are the rig's (1920x1080 when the file does
        # not say), at the rig's refresh
        set w 1920; set h 1080
        if {$rw ne ""} { set w $rw; set h $rh }
        regexp {^(\d+)x(\d+)$} $o(-size) -> w h
        lappend args --headless -w $w -h $h
        if {[dict exists $g hz] && [string is double -strict [dict get $g hz]] && [dict get $g hz] > 0} {
            lappend args -r [expr {int(round([dict get $g hz]))}]
        }
    } else {
        # a window in the rig's aspect, no wider than 960
        set w 960; set h 540
        if {$rw ne ""} {
            if {$rw > 960} { set w 960; set h [expr {round($rh * 960.0 / $rw)}] } else { set w $rw; set h $rh }
        }
        regexp {^(\d+)x(\d+)$} $o(-size) -> w h
        lappend args -w $w -h $h
    }
    if {$::tcl_platform(os) ne "Darwin" && [file exists /usr/local/stim2/config/linux.cfg]} {
        lappend args -f /usr/local/stim2/config/linux.cfg
    }
    # not dlsh's own Tcl/Tk library paths (inside its zip): stim2 would
    # look for its library, and its modules (stimess), there
    set saved {}
    foreach v {TCL_LIBRARY TK_LIBRARY} {
        if {[info exists ::env($v)]} { dict set saved $v $::env($v); unset ::env($v) }
    }
    set pid [exec $exe {*}$args >/dev/null 2>/dev/null &]
    dict for {v val} $saved { set ::env($v) $val }
    {*}$o(-log) "launched stim2 (pid $pid): $exe $args"
    return $pid
}

# Connect to a stim2's message port.  With pid (one we launched), wait up
# to 30 s for it to answer.
proc replay::connect {host port {pid ""}} {
    set deadline [expr {[clock milliseconds] + 30000}]
    while {[catch {framelock::connect $host $port} sock]} {
        if {$pid eq ""} {
            fail 1 "no stim2 answers at $host:$port ($sock). Add -launch to start one here, or start one beside any live display, e.g.\
\n    stim2 --headless -w 1920 -h 1080 -r 120 --port-base [expr {$port - 2}]\
\nand give it a few seconds to load before replaying."
        }
        if {![alive $pid]} { fail 1 "the launched stim2 exited before answering" }
        if {[clock milliseconds] > $deadline} { fail 1 "the launched stim2 did not answer on port $port within 30 s" }
        after 200
    }
    return $sock
}

# stimess 1.1 or newer (the playback prelude); returns its version
proc replay::check_stimess {sock {where ""}} {
    set v [framelock::send $sock {package require stimess}]
    if {[framelock::is_error $v] || [catch {package vcompare $v 1.1} c] || $c < 0} {
        fail 1 "stim2[expr {$where ne "" ? " at $where" : ""}] has no stimess 1.1 (got: $v); it cannot take a playback prelude"
    }
    return $v
}

# framelock::setup's dict for session S
proc replay::setup_dict {S {title ""}} {
    set stimdg_b64 [set ${S}::stimdg_b64]
    set d [dict create libs [set ${S}::libs] script [set ${S}::script] \
        stimdg [expr {$stimdg_b64 ne "" ? $stimdg_b64 : [binary encode base64 [set ${S}::stimdg]]}] \
        title $title]
    foreach k {halfx halfy w h hz bg} {
        if {[info exists ${S}::geom($k)]} { dict set d geom $k [set ${S}::geom($k)] }
    }
    return $d
}

# ---------------------------------------------------------------------------
# play: the plan's events paced by time (not frame-locked).  The stim2 must
# be set up.  Returns {sent elapsed errors skipped swaps}.
#   -speed X (default 1.0; 0 = as fast as stim2 takes them)
#   -gap S   longest pause kept between two events (default 1.0)
#   -verbose 0|1  -log CMD
# ---------------------------------------------------------------------------
proc replay::play {P sock args} {
    array set o {-speed 1.0 -gap 1.0 -verbose 0 -log replay::say}
    foreach {k v} $args {
        if {![info exists o($k)]} { fail 2 "replay::play: unknown option $k" }
        set o($k) $v
    }
    set log $o(-log)
    set events [dict get $P events]
    set t0 [lindex $events 0 0]
    set wall0 [clock microseconds]
    set offset 0.0            ;# recorded time removed by gap compression, us
    set sent 0; set errors 0; set skipped 0; set prev_ts $t0
    set swap0 [framelock::send $sock {set SwapCount}]
    foreach e $events {
        set ts [lindex $e 0]
        # pacing: recorded delta, compressed, scaled
        set dt [expr {$ts - $prev_ts}]
        if {$dt > $o(-gap) * 1e6} { set offset [expr {$offset + $dt - $o(-gap) * 1e6}] }
        set prev_ts $ts
        if {$o(-speed) > 0} {
            set due [expr {$wall0 + ($ts - $t0 - $offset) / $o(-speed)}]
            set wait [expr {int(($due - [clock microseconds]) / 1000)}]
            if {$wait > 0} { after $wait }
        }
        lassign $e _ kind p1 p2 p3
        if {$kind eq "cmd"} {
            if {[framelock::skipped_cmd $p1]} { incr skipped; continue }
            set r [framelock::send $sock $p1]
            if {[framelock::is_error $r]} { incr errors; {*}$log "ERROR [string range $p1 0 60] -> $r" } \
            elseif {$o(-verbose)} { {*}$log [format "%+8.3f cmd %s" [expr {($ts - $t0)/1e6}] [string range $p1 0 70]] }
        } else {
            set r [framelock::send $sock [list stim::inject $p1 $p3 $p2 $ts]]
            if {[framelock::is_error $r]} { incr errors; {*}$log "ERROR inject $p1 -> $r" } \
            elseif {$o(-verbose)} { {*}$log [format "%+8.3f dp  %s = %s" [expr {($ts - $t0)/1e6}] $p1 $p3] }
        }
        incr sent
    }
    set elapsed [expr {([clock microseconds] - $wall0)/1e6}]
    set swaps [expr {[framelock::send $sock {set SwapCount}] - $swap0}]
    dict create sent $sent elapsed $elapsed errors $errors skipped $skipped swaps $swaps
}

# ---------------------------------------------------------------------------
# render: a -render plan, frame-locked, in step mode, into a manual frame
# sink.  The stim2 must be set up.  Returns a dict: drawn (rig frames),
# kinds (stepFrame kinds counted), sent (inputs), errors, skipped, elapsed,
# repaints, and either movies ({trial out status} per movie, -each) or
# status (frameSink close's dict), with bad (movies whose writer failed).
#   -crf N       x264 quality for a movie file (default 14)
#   -keyint N    at most N frames between keyframes (default: x264's own);
#                short for a clip a browser will scrub
#   -progress CMD  called with {frames-done of} every 10 rig frames: a
#                server rendering inside a coroutine yields there, so it
#                keeps answering while a clip renders
#   -verbose 0|1  -log CMD
# ---------------------------------------------------------------------------
proc replay::_sink_target {out movie_out blend fps win_w win_h render_fps crf keyint} {
    # "|command" is always a command (it may well end in .mp4 itself); a
    # movie file goes through ffmpeg
    if {!$movie_out} { return $out }
    set vf [expr {$blend > 1 ? "-vf tmix=frames=$blend,fps=$fps" : ""}]
    set g [expr {$keyint ne "" ? "-g $keyint" : ""}]
    return "|ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s ${win_w}x${win_h}\
 -r $render_fps -i - $vf -c:v libx264 -preset medium -tune animation -crf $crf $g\
 -pix_fmt yuv420p -movflags +faststart [list [file normalize $out]]"
}

# the overlay commands for movie time t (rig ms): samples in (t - trail, t]
proc replay::_gaze_at {t} {
    upvar 1 eye_t eye_t eye_x eye_x eye_y eye_y eye_k eye_k n_eye n_eye \
        gaze_trail_ms gaze_trail_ms gaze_hold_ms gaze_hold_ms
    while {$eye_k < $n_eye && [lindex $eye_t $eye_k] <= $t} { incr eye_k }
    set j [expr {$eye_k - 1}]
    if {$j < 0 || $t - [lindex $eye_t $j] > $gaze_hold_ms} { return [framelock::gaze_cmd "" ""] }
    set pts {}
    for {set i $j} {$i >= 0 && $t - [lindex $eye_t $i] <= $gaze_trail_ms} {incr i -1} {
        lappend pts [lindex $eye_x $i] [lindex $eye_y $i]
    }
    framelock::gaze_cmd [list [lindex $eye_x $j] [lindex $eye_y $j]] $pts
}
# the whole overlay (gaze, touches) for movie time t, from render's frame
proc replay::_overlay_at {t} {
    upvar 1 eye_t eye_t eye_x eye_x eye_y eye_y eye_k eye_k n_eye n_eye \
        gaze_trail_ms gaze_trail_ms gaze_hold_ms gaze_hold_ms \
        touch touch n_touch n_touch touch_r touch_r touch_rgb touch_rgb touch_a touch_a \
        touch_hold_ms touch_hold_ms touch_fade_ms touch_fade_ms touch_trail_ms touch_trail_ms
    set cmds {}
    if {$n_eye} { lappend cmds [_gaze_at $t] }
    if {$n_touch} { lappend cmds [_touch_at $t] }
    join $cmds "\n"
}

proc replay::render {P sock args} {
    array set o {-crf 14 -keyint "" -progress "" -verbose 0 -log replay::say -gaze-style ""
                 -touch-style ""}
    foreach {k v} $args {
        if {![info exists o($k)]} { fail 2 "replay::render: unknown option $k" }
        set o($k) $v
    }
    set log $o(-log)
    if {[dict get $P render] eq ""} { fail 2 "replay::render: the plan was made without -render" }
    foreach v {render each fps blend render_fps movie_out period nf frames fev writes pos ftrial
               n_eye eye_t eye_x eye_y skipped} {
        set $v [dict get $P $v]
    }
    set lead [expr {[dict exists $P lead] ? [dict get $P lead] : 0}]
    set t0 [expr {[dict exists $P t0] ? [dict get $P t0] : [lindex $frames 0 4] / 1000.0}]
    set touch [expr {[dict exists $P touch] ? [dict get $P touch] : {}}]
    set n_touch [llength $touch]
    if {[framelock::send $sock {info commands stepFrame}] ne "stepFrame"} {
        fail 1 "this stim2 has no stepFrame (step mode): -render needs the headless/step build"
    }
    set win_w [framelock::send $sock {screen_set WinWidth}]
    set win_h [framelock::send $sock {screen_set WinHeight}]
    set out $render
    set sinkargs [list $movie_out $blend $fps $win_w $win_h $render_fps $o(-crf) $o(-keyint)]
    set sink_open {{out} {
        upvar 1 sock sock movie_out movie_out sinkargs sinkargs
        if {$movie_out} { file mkdir [file dirname [file normalize $out]] }
        set r [framelock::send $sock [list frameSink open -manual [replay::_sink_target $out {*}$sinkargs]]]
        if {[framelock::is_error $r]} { replay::fail 1 "frameSink: $r" }
    }}
    set movies {}             ;# -each: {trial out status-dict} per movie
    set cur_trial ""; set cur_out $out
    if {!$each} { apply $sink_open $out }
    set t_start [expr {[lindex $frames 0 4] / 1000.0}]
    set fev0 [lindex $fev 0]
    if {[llength $fev0]} { set t_start [expr {min($t_start, [lindex $fev0 0 0])}] }
    set t_start [expr {$t_start - 1.0}]
    framelock::send $sock [list stepMode 1 $t_start]
    set clock $t_start; set prevflip $t_start
    # the gaze overlay: a disc at the newest sample, a fading trail behind
    set gaze_trail_ms 150.0; set gaze_hold_ms 100.0
    set eye_k 0; set gaze_last ""
    if {$n_eye} { framelock::send $sock [framelock::gaze_style {*}$o(-gaze-style)] }
    # the touches: a ring per press while it is down, fading after
    set touch_hold_ms 250.0; set touch_fade_ms 400.0; set touch_trail_ms 500.0
    if {$n_touch} {
        lassign $o(-touch-style) tsize tcolor
        if {$tsize eq ""} { set tsize 1.6 }
        if {$tcolor eq ""} { set tcolor {1 0.3 0.9 0.95} }
        lassign $tcolor tr tg tb touch_a
        if {$touch_a eq ""} { set touch_a 1 }
        set touch_rgb [list $tr $tg $tb]
        set touch_r [expr {$tsize / 2.0}]
        framelock::send $sock [framelock::touch_style $tsize $tcolor]
    }
    set ov [expr {$n_eye || $n_touch}]
    set repaints 0
    set sent 0; set errors 0
    set kinds [dict create]
    set wall0 [clock microseconds]
    if {$lead > 0 && !$each} {
        # The lead (-lead): from BEGINOBS to the trial's first new frame the
        # rig still showed what the last trial left, which this render does
        # not have, so the rig's background (setup set it) with the gaze and
        # touches over it.  One frame stepped with the scene list hidden, at
        # the clock the render starts from (so it moves no input's time),
        # then held: repainted where the overlay moves, written in chunks
        # so a server's -progress gets its turns during a long fixation.
        set s "set ::framelock_lead_vis \[glistSetVisible\]\n"
        append s "if {\$::framelock_lead_vis} {glistSetVisible 0}\n"
        if {$ov} { set g [_overlay_at $t0]; append s "$g\n"; set gaze_last $g }
        append s "stepFrame $t_start $t_start\nframeSink write 1\n"
        set k 1
        while 1 {
            set same 0
            for {set n 0} {$k < $lead && $n < 120} {incr k; incr n} {
                set g [expr {$ov ? [_overlay_at [expr {$t0 + $k * $period}]] : $gaze_last}]
                if {$g eq $gaze_last} { incr same; continue }
                if {$same} { append s "frameSink write $same\n"; set same 0 }
                append s "$g\nframeRepaint\nframeSink write 1\n"
                set gaze_last $g
                incr repaints
            }
            if {$same} { append s "frameSink write $same\n" }
            if {$k >= $lead} { append s "if {\$::framelock_lead_vis} {glistSetVisible 1}\n" }
            set r [framelock::send $sock $s]
            if {[framelock::is_error $r]} {
                catch { framelock::send $sock {if {$::framelock_lead_vis} {glistSetVisible 1}} }
                catch { framelock::send $sock {frameSink close} }
                catch { framelock::send $sock {stepMode 0} }
                fail 1 "the lead before the first frame: $r"
            }
            if {$k >= $lead} break
            if {$o(-progress) ne ""} { {*}$o(-progress) 0 $nf }
            set s ""
        }
    }
    for {set i 0} {$i < $nf} {incr i} {
        if {$each && [lindex $ftrial $i] ne $cur_trial} {
            # the next trial's movie: close this one, open its own
            if {$cur_trial ne ""} { lappend movies [list $cur_trial $cur_out [framelock::send $sock {frameSink close}]] }
            set cur_trial [lindex $ftrial $i]
            set cur_out [each_name $out $cur_trial]
            apply $sink_open $cur_out
            if {$o(-verbose)} { {*}$log "trial $cur_trial -> $cur_out" }
        }
        set fr [lindex $frames $i]
        set fe [lindex $fev $i]
        set flip [expr {[lindex $fr 4] / 1000.0}]
        set nw [lindex $writes $i]
        # One request per rig frame, however many movie ticks it spans: its
        # commands and datapoints, the gaze, the frame, and its writes --
        # for a frame the rig held while the eye moved, a repaint of the
        # gaze over the same scene at each tick's time. Over a slow link (a
        # Pi on Wi-Fi: ~20 ms a round trip) requests, not drawing, set the
        # pace.
        set pre {}; set post {}
        # its first movie tick: the flip, or the movie's start for a frame
        # from before it (-lead: shown since before BEGINOBS)
        set tick0 [expr {[lindex $pos $i] < 0 ? $t0 : $flip}]
        if {$ov && $nw > 0} {
            # the overlay at this frame's first movie tick, drawn with the frame
            set g [_overlay_at $tick0]
            if {$g ne $gaze_last} { append pre "$g\n"; set gaze_last $g }
        }
        if {$nw == 0} {
            # before the movie's start: drawn, not written
        } elseif {!$ov || $nw <= 1} {
            append post "frameSink write $nw\n"
        } else {
            append post "frameSink write 1\n"
            set same 0
            for {set k 1} {$k < $nw} {incr k} {
                set g [_overlay_at [expr {$tick0 + $k * $period}]]
                if {$g eq $gaze_last} { incr same; continue }
                if {$same} { append post "frameSink write $same\n"; set same 0 }
                append post "$g\nframeRepaint\nframeSink write 1\n"
                set gaze_last $g
                incr repaints
            }
            if {$same} { append post "frameSink write $same\n" }
        }
        set ev_us [lindex $fr 8]
        set req [framelock::frame_request $fe clock $prevflip $flip $pre $post \
                     [expr {$ev_us ne "" ? $ev_us / 1000.0 : ""}]]
        set r [framelock::send $sock $req]
        if {[framelock::is_error $r]} { incr errors; {*}$log "ERROR frame $i (stepFrame at $clock) -> $r"; break }
        lassign $r res errs
        foreach e $errs {
            lassign $e n msg
            lassign [lindex $fe $n] - kind p1
            incr errors
            {*}$log "ERROR [expr {$kind eq "cmd" ? [string range $p1 0 60] : "inject $p1"}] -> $msg"
        }
        if {$o(-verbose)} {
            foreach ev $fe {
                if {[lindex $ev 1] eq "cmd"} { {*}$log [format "frame %6d cmd %s" $i [string range [lindex $ev 2] 0 70]] }
            }
        }
        incr sent [llength $fe]
        dict incr kinds [dict get $res kind]
        set prevflip $clock
        if {$o(-verbose) && $i % 600 == 0} { {*}$log "frame $i of $nf" }
        if {$o(-progress) ne "" && $i % 10 == 9} { {*}$o(-progress) [expr {$i + 1}] $nf }
    }
    set elapsed [expr {([clock microseconds] - $wall0)/1e6}]
    set st [framelock::send $sock {frameSink close}]   ;# {frames N status S}
    if {$each && $cur_trial ne ""} { lappend movies [list $cur_trial $cur_out $st] }
    framelock::send $sock {stepMode 0}
    if {$n_eye} { framelock::send $sock {overlay delete gaze; overlay delete gaze_trail} }
    if {$n_touch} {
        for {set k 0} {$k < $::framelock::touch_slots} {incr k} { framelock::send $sock "overlay delete touch$k; overlay delete touchtrail$k" }
    }
    set bad 0
    if {$each} {
        foreach m $movies { if {[dict get [lindex $m 2] status] != 0} { incr bad } }
    } elseif {[dict get $st status] != 0} { incr bad }
    dict create drawn $i kinds $kinds sent $sent errors $errors skipped $skipped \
        elapsed $elapsed repaints $repaints n_eye $n_eye n_touch $n_touch movies $movies status $st bad $bad \
        width $win_w height $win_h
}

# ---------------------------------------------------------------------------
# render_trial: one trial into its own clip, from a fresh setup, on a stim2
# that may have rendered anything before -- the call a trial viewer makes,
# in any order.  Writes OUT (a movie file) and, beside it, OUT's root .json:
# the clip's timing (replay::timing).  Returns render's dict plus json (the
# path).
#
# Each trial starts from the script as loaded, stimdg and the bound
# streams' last values, not from the state the earlier trials left in the
# rig's stim2: a script that carries state across trials (a block counter,
# an adaptive staircase in stim2) can differ from the rig here, where a
# straight-through render (-each) does not.
#   -fps F  -crf N  -keyint N (default 12, for scrubbing)  -eye MODE
#   (default off: a viewer draws the gaze itself)  -json PATH  -progress CMD
#   -verbose  -log
#   -gaze {T X Y}  the gaze to draw, in place of -eye's reading of the
#                  file: T seconds from the period's first eye record (a
#                  trials DG's em_seconds; from BEGINOBS when the period has
#                  none), X Y degrees, three lists of one length.  What a
#                  viewer shows is then what the movie shows.
#   -gaze-style {SIZE {R G B A}}  the gaze disc (framelock::gaze_style)
#   -labels {NAME OPTS ...}  overlay items over the whole clip, each
#                  `overlay set NAME {*}OPTS` (a caption: -text -pos -size
#                  -anchor -color -bg), deleted after
#   -touch 0|1     draw the trial's touchscreen presses (default 0)
#   -touch-style {SIZE {R G B A}}  the rings: diameter, colour
#   -lead 0|1      start the clip at BEGINOBS, the stretch before the
#                  trial's first new rig frame as the rig's background with
#                  the gaze and touches over it (plan -lead; default 1).  0:
#                  the clip starts at the first frame, as before
# ---------------------------------------------------------------------------
proc replay::render_trial {S sock trial out args} {
    array set o {-fps "" -crf 14 -keyint 12 -eye off -json "" -progress "" -verbose 0 -log replay::say
                 -gaze "" -gaze-style "" -labels "" -touch 0 -touch-style "" -lead 1}
    foreach {k v} $args {
        if {![info exists o($k)]} { fail 2 "replay::render_trial: unknown option $k" }
        set o($k) $v
    }
    if {![is_movie $out]} { fail 2 "replay::render_trial: $out is not a movie file (.mp4/.mov/.mkv)" }
    set eye [expr {$o(-gaze) ne "" ? "off" : $o(-eye)}]
    set P [plan $S -trials [list $trial $trial] -render $out -fps $o(-fps) -eye $eye -touch $o(-touch) \
               -lead $o(-lead)]
    if {$o(-gaze) ne ""} { set P [gaze_tracks $S $P $trial {*}$o(-gaze)] }
    if {[llength $o(-labels)] % 2} { fail 2 "replay::render_trial: -labels wants NAME OPTS pairs" }
    # a stepped stim2 left by an earlier render: out of step mode, so the
    # next stepMode 1 may start its clock anywhere
    catch { framelock::send $sock {stepMode 0} }
    set quiet {apply {{msg} {}}}
    framelock::setup $sock [setup_dict $S "replay: [file tail [set ${S}::path]] (trial $trial)"] \
        [expr {$o(-verbose) ? $o(-log) : $quiet}]
    if {$trial > 1} { framelock::send $sock reset }
    foreach {name opts} $o(-labels) {
        set r [framelock::send $sock [list overlay set $name {*}$opts]]
        if {[framelock::is_error $r]} { fail 1 "label $name: $r" }
    }
    try {
        set r [render $P $sock -crf $o(-crf) -keyint $o(-keyint) -progress $o(-progress) \
                   -verbose $o(-verbose) -log $o(-log) -gaze-style $o(-gaze-style) \
                   -touch-style $o(-touch-style)]
    } finally {
        foreach {name opts} $o(-labels) { catch { framelock::send $sock [list overlay delete $name] } }
    }
    set jpath [expr {$o(-json) ne "" ? $o(-json) : "[file rootname $out].json"}]
    set fd [open $jpath w]
    puts $fd [timing $S $P $r $trial]
    close $fd
    dict set r json $jpath
    return $r
}

# ---------------------------------------------------------------------------
# em_zero: trial's first eyetracking/raw record inside its observation
# period, on the dserv clock (us): where a trials DG's em_seconds is 0
# (em::normalize_timestamps).  "" when the period has none.
# ---------------------------------------------------------------------------
proc replay::em_zero {S trial} {
    set k [expr {$trial - 1}]
    set ob [lindex [set ${S}::obs_begin] $k]; set oe [lindex [set ${S}::obs_end] $k]
    set eye_recs [set ${S}::eye_recs]
    set j [expr {[lsearch -bisect -integer -index 0 -sorted $eye_recs $ob] + 1}]
    if {$j > 0 && [lindex $eye_recs [expr {$j - 1}] 0] == $ob} { incr j -1 }
    if {$j < [llength $eye_recs] && [lindex $eye_recs $j 0] <= $oe} { return [lindex $eye_recs $j 0] }
    return ""
}

# gaze_tracks: plan P with its gaze replaced by T X Y (render_trial -gaze):
# T seconds from em_zero (else BEGINOBS), put on the rig clock the plan's
# frames use, as plan does its own samples
proc replay::gaze_tracks {S P trial T X Y} {
    set n [llength $T]
    if {[llength $X] != $n || [llength $Y] != $n} { fail 2 "-gaze: T, X and Y differ in length" }
    set z [em_zero $S $trial]
    if {$z eq ""} { set z [lindex [set ${S}::obs_begin] [expr {$trial - 1}]] }
    set z_ms [expr {($z + [dict get $P tick_off]) / 1000.0}]
    set et {}; set ex {}; set ey {}
    foreach t $T x $X y $Y {
        if {![string is double -strict $t] || ![string is double -strict $x] || ![string is double -strict $y]} continue
        lappend et [expr {$z_ms + 1000.0 * $t}]; lappend ex $x; lappend ey $y
    }
    set order [lsort -indices -real $et]
    dict set P eye_t [lmap i $order {lindex $et $i}]
    dict set P eye_x [lmap i $order {lindex $ex $i}]
    dict set P eye_y [lmap i $order {lindex $ey $i}]
    dict set P n_eye [llength $et]
    dict set P eye_mode supplied
    return $P
}

# ---------------------------------------------------------------------------
# timing: a single-trial clip's timing as JSON, for a viewer to put the
# clip on the trial's own timeline.  All times are ms on the clip's
# timeline (0 = its first movie frame: BEGINOBS when start is "beginobs",
# the trial's first rig frame when it is "frame"):
#   version                    2 (1 had no start, lead_ms, first_frame_ms,
#                              and was always "frame")
#   start                      "beginobs" (plan -lead; render_trial's
#                              default) or "frame"
#   lead_ms, first_frame_ms    the background lead before the first rig
#                              frame, and where that frame starts showing
#                              (0 when one came before BEGINOBS)
#   obs_begin_ms, obs_end_ms   the observation period (BEGINOBS, ENDOBS):
#                              data timed from BEGINOBS (em_seconds, the
#                              trial's event times) is at t + obs_begin_ms
#                              (0 with start "beginobs")
#   window_start_ms            the trial window's start (the previous
#                              period's end; before the clip)
#   rig_frames                 per rig frame drawn: movie_ms (where it starts
#                              in the clip; negative for a frame from before
#                              BEGINOBS, drawn but not in the clip), flip_ms
#                              (the rig's flip on the same timeline: they
#                              differ by the rounding to movie ticks) and
#                              swap (its swapcount)
#   cmds                       each command, with the rig frame (index into
#                              rig_frames) that first showed it
#   em_zero_ms                 the period's first eyetracking/raw record (null
#                              without one): where em::'s em_seconds is 0,
#                              since it counts from the trial's first eye
#                              sample, a few ms after BEGINOBS
#   touches                    each touch stroke in the trial (drawn in the
#                              clip only with -touch): [t, x, y, t_up, x_up,
#                              y_up, path], degrees, the release null when
#                              none was logged, path [t, x, y, ...] the
#                              finger's way through its TOUCH MOVEs (null
#                              for a tap or a file without them)
# Datapoint times are dserv's receipt on the rig clock, through the median
# stim - dserv clock offset of the trial's frames (tick_off_us).
# ---------------------------------------------------------------------------
proc replay::_jstr {s} {
    set s [string map {\\ \\\\ \" \\\" \n \\n \r \\r \t \\t} $s]
    regsub -all {[\x01-\x1f]} $s {} s
    return "\"$s\""
}
proc replay::_jnum {v} { format %.3f $v }
proc replay::_jarr {l} { return "\[[join $l ,]\]" }
proc replay::_jobj {pairs} {
    set parts {}
    foreach {k v} $pairs { lappend parts "[_jstr $k]:$v" }
    return "{[join $parts ,]}"
}

proc replay::timing {S P r trial} {
    set obs_begin [set ${S}::obs_begin]; set obs_end [set ${S}::obs_end]
    array set geom [array get ${S}::geom]
    set frames [dict get $P frames]
    set tick_off [dict get $P tick_off]
    set period [dict get $P period]
    # the clip's 0: BEGINOBS with -lead, else the first frame's flip
    set F0 [expr {[dict exists $P t0] ? [dict get $P t0] : [lindex $frames 0 4] / 1000.0}]
    set lead [expr {[dict exists $P lead] ? [dict get $P lead] : 0}]
    # dserv time (us) -> clip ms
    set clip {{us tick_off F0} { expr {($us + $tick_off) / 1000.0 - $F0} }}
    set k [expr {$trial - 1}]
    set movie_ms {}; set flip_ms {}; set swap {}
    foreach f $frames p [dict get $P pos] {
        lappend movie_ms [_jnum [expr {$p * $period}]]
        lappend flip_ms [_jnum [expr {[lindex $f 4] / 1000.0 - $F0}]]
        lappend swap [lindex $f 2]
    }
    set cmds {}
    set i 0
    foreach fe [dict get $P fev] {
        foreach ev $fe {
            if {[lindex $ev 1] eq "cmd"} {
                lappend cmds [_jobj [list frame $i cmd [_jstr [string range [lindex $ev 2] 0 199]]]]
            }
        }
        incr i
    }
    set scr {}
    foreach key {halfx halfy w h hz} {
        if {[info exists geom($key)] && [string is double -strict $geom($key)]} { lappend scr $key $geom($key) }
    }
    if {[info exists geom(bg)]} { lappend scr bg [_jarr $geom(bg)] }
    # the period's first eye record
    set em_zero null
    set z [em_zero $S $trial]
    if {$z ne ""} { set em_zero [_jnum [apply $clip $z $tick_off $F0]] }
    # the trial's touches, drawn in the clip or not: {t x y t_up x_up y_up},
    # null for a release not logged
    set touches {}
    foreach m [touch_marks $S $tick_off [dict get $P w0] [dict get $P w1]] {
        lassign $m t x y tu xu yu path
        set row [list [_jnum [expr {$t - $F0}]] [_jnum $x] [_jnum $y]]
        if {$tu eq ""} { lappend row null null null } else {
            lappend row [_jnum [expr {$tu - $F0}]] [_jnum $xu] [_jnum $yu]
        }
        if {$path eq ""} { lappend row null } else {
            set p {}
            foreach {pt px py} $path { lappend p [_jnum [expr {$pt - $F0}]] [_jnum $px] [_jnum $py] }
            lappend row [_jarr $p]
        }
        lappend touches [_jarr $row]
    }
    set p0 [lindex [dict get $P pos] 0]
    _jobj [list \
        version 2 \
        start [_jstr [expr {[dict exists $P start] ? [dict get $P start] : "frame"}]] \
        lead_ms [_jnum [expr {$lead * $period}]] \
        first_frame_ms [_jnum [expr {max(0, $p0) * $period}]] \
        file [_jstr [file tail [set ${S}::path]]] \
        trial $trial obsid $k ntrials [llength $obs_end] \
        fps [dict get $P fps] movie_frames [dict get $P movie_frames] \
        width [dict get $r width] height [dict get $r height] \
        screen [_jobj $scr] \
        tick_off_us [_jnum $tick_off] \
        window_start_ms [_jnum [apply $clip [dict get $P w0] $tick_off $F0]] \
        obs_begin_ms [_jnum [apply $clip [lindex $obs_begin $k] $tick_off $F0]] \
        obs_end_ms [_jnum [apply $clip [lindex $obs_end $k] $tick_off $F0]] \
        em_zero_ms $em_zero \
        touches [_jarr $touches] \
        rig_frames [_jobj [list movie_ms [_jarr $movie_ms] flip_ms [_jarr $flip_ms] swap [_jarr $swap]]] \
        cmds [_jarr $cmds]]
}
