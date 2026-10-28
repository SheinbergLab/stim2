#!/usr/bin/env dlsh
# mirror.tcl -- a live mirror of a rig's display: a stim2 of its own (on its
# own monitor, or a window) drawing every frame the rig's stim2 draws, from
# the rig's dserv, with the gaze over it.
#
#     dlsh tools/mirror/mirror.tcl -dserv 192.168.88.40 ?options?
#
#       -dserv H       the rig's dserv (required)
#       -host H        the mirror's stim2 (default localhost)
#       -port P        its message port (default 5612: a stim2 started with
#                      --port-base 5610). 4612 is refused without -live, so
#                      a mirror run on a rig's own display box cannot draw
#                      over the experiment.
#       -live          allow -port 4612
#       -wait MS       longest a frame waits for its commands (default 50)
#       -eye NAME      the gaze datapoint, degrees (default
#                      eyetracking/position; "off" for none)
#       -trail MS      gaze trail length (default 100)
#       -report S      print the lag and frame counts every S s (default 10)
#       -late MS       repaint the gaze this long before the next vsync
#                      (default 5; see presented below)
#       -verbose       print every frame's commands
#
# It is tools/replay/replay.tcl -render with dserv as the source: the same
# setup and frame-locked core (tools/lib/framelock.tcl), fed as records
# arrive rather than from a file:
#
#   - it subscribes to the rig's stim records (stim/script, stim/lib/*,
#     stim/bind, stim/cmd, stim/frame, stimdg), the streams stim/bind names,
#     ess/screen_*, ess/in_obs and ess/status, and the gaze. The rig's
#     stim2 never gets eye data; the mirror does.
#   - joining, it takes the current values (stim/script and its libraries,
#     stimdg, the geometry) and shows "waiting" until the next trial
#     boundary: the end of an observation period, or a start. The trial
#     before it was set up by commands the mirror never saw.
#   - at the boundary it sets its stim2 up as configure_stim does (prelude,
#     libraries, script, stim_init, stimdg, degrees, background) and puts
#     it in step mode on the rig's clock. From then on, each rig frame
#     (a stim/frame record) is drawn once its commands are in: each
#     command's ACK stamp names the frame that first showed it, and the
#     frame's cmds_applied says how many to expect. A '!' command's record
#     only arrives after that frame flipped, so the frame waits for it, up
#     to -wait. The frame's ds_dispatched advance places the datapoints.
#     Its inputs, the gaze and stepFrame go as one request.
#   - behind (frames ready faster than this display shows them: a 120 Hz
#     rig on a 60 Hz monitor while something moves), only the newest ready
#     frame is drawn, after the inputs of all of them; the others are
#     counted as dropped.
#   - between rig frames the gaze is repainted over the last frame
#     (frameRepaint) whenever it moves.
#   - a reload on the rig (configure_stim publishes an empty stim/script
#     first, the new one last) or a restart of the rig's stim2 (swapcount
#     going back) returns to "waiting" until the next boundary after the
#     new script is in.
#
# Lag, reported every -report seconds: rig flip (stim/frame flip_us, the
# rig's CLOCK_REALTIME) to the mirror's present (this host's clock when
# stepFrame returns, after the swap), and to the record's arrival here.
# Both need the two hosts' clocks in step (chrony/PTP); stim2.service
# already waits for chrony.

package require tcljson
source [file join [file dirname [file normalize [info script]]] .. lib framelock.tcl]

set opt(dserv)   ""
set opt(host)    localhost
set opt(port)    5612
set opt(live)    0
set opt(wait)    50
set opt(eye)     eyetracking/position
set opt(trail)   100
set opt(report)  10
set opt(late)    5
set opt(verbose) 0

set args $argv
while {[llength $args]} {
    set a [lindex $args 0]; set args [lrange $args 1 end]
    switch -- $a {
        -dserv   { set opt(dserv) [lindex $args 0]; set args [lrange $args 1 end] }
        -host    { set opt(host)  [lindex $args 0]; set args [lrange $args 1 end] }
        -port    { set opt(port)  [lindex $args 0]; set args [lrange $args 1 end] }
        -live    { set opt(live) 1 }
        -wait    { set opt(wait)  [lindex $args 0]; set args [lrange $args 1 end] }
        -eye     { set opt(eye)   [lindex $args 0]; set args [lrange $args 1 end] }
        -trail   { set opt(trail) [lindex $args 0]; set args [lrange $args 1 end] }
        -report  { set opt(report) [lindex $args 0]; set args [lrange $args 1 end] }
        -late    { set opt(late)  [lindex $args 0]; set args [lrange $args 1 end] }
        -verbose { set opt(verbose) 1 }
        default  { puts stderr "unknown option $a"; exit 2 }
    }
}
if {$opt(dserv) eq ""} {
    puts stderr "usage: dlsh mirror.tcl -dserv HOST ?-host H? ?-port P? ?-live? ?-wait MS?\
 ?-eye NAME|off? ?-trail MS? ?-report S? ?-late MS? ?-verbose?"
    exit 2
}
if {$opt(port) == 4612 && !$opt(live)} {
    puts stderr "mirror: port 4612 is a live display's message port; start the mirror's stim2\
 with --port-base 5610 and use -port 5612 (the default), or pass -live if you mean it"
    exit 2
}

proc log {msg} { puts "[clock format [clock seconds] -format %H:%M:%S] $msg"; flush stdout }
proc now_us {} { clock microseconds }
proc median {l} { set l [lsort -real $l]; lindex $l [expr {[llength $l] / 2}] }
proc pct {l p} {
    if {![llength $l]} { return "-" }
    set l [lsort -real $l]
    format %.1f [lindex $l [expr {min([llength $l] - 1, int([llength $l] * $p))}]]
}

# ---------------------------------------------------------------------------
# the rig, as its dserv describes it
# ---------------------------------------------------------------------------
set rig(script) ""          ;# stim/script body ("" while none or reloading)
set rig(hash) ""
set rig(path) ""
set rig(script_ts) 0
set rig(libs) [dict create] ;# name -> {ts body}
set rig(bind) {}            ;# stream names stim/bind lists
set rig(stimdg) ""          ;# base64
set rig(geom) [dict create]
set rig(last) [dict create] ;# bound stream -> {ts type data}, the latest
set rig(in_obs) 0
set rig(status) ""
set rig(load) ""            ;# system/protocol/variant
set rig(connected) ""       ;# ess/rmt_connected

# ---------------------------------------------------------------------------
# the mirror's state
#   mode    waiting: no script, or after a reload/restart, until one is in
#           ready:   a script, waiting for the next trial boundary
#           run:     drawing the rig's frames
# ---------------------------------------------------------------------------
set mode waiting
set why "joining"
set sock ""                 ;# the mirror's stim2
set stepped 0
set P ""                    ;# the planner (framelock)
set queue {}                ;# rig frames not yet drawn, {f arrival_us}
set prev_cmds ""            ;# cmds_applied of the last frame taken
set last_sc ""              ;# the rig's last swapcount, any mode
set last_frame ""
set start_ts 0              ;# the boundary: records after it are planned
set clock_ms 0              ;# the mirror's stepped clock
set prevflip 0
set offs {}                 ;# recent ticks_us - dserv ts, for tick_off
set gaze {}                 ;# {t_us x y} newest last
set gaze_shown ""
set last_present 0
set period_ms 16.667        ;# the mirror display's refresh period
set pump_after ""

# stats since the last report
proc stats_reset {} {
    global st
    array set st {drawn 0 dropped 0 missing 0 timeouts 0 errors 0 repaints 0 eye 0 lag {} arr {} ready {} draw {}}
    set st(t0) [now_us]
}
stats_reset
array set total {drawn 0 dropped 0 missing 0 errors 0}

# ---------------------------------------------------------------------------
# dserv: one-shot requests on 4620; datapoints arrive on a connection dserv
# opens back to our listener, one text line each:
#     <name> <type> <timestamp> <length> {<value>}
# ---------------------------------------------------------------------------
proc ds_req {line} {
    set t [socket $::opt(dserv) 4620]
    fconfigure $t -buffering line -encoding utf-8 -translation lf
    set me [lindex [fconfigure $t -sockname] 0]
    puts $t [string map [list @ME@ "$me $::listen_port"] $line]
    gets $t r
    close $t
    return $r
}
proc ds_match {name} { ds_req "%match @ME@ $name 1" }
proc ds_unmatch {name} { ds_req "%unmatch @ME@ $name" }

# a line (from a subscription, or a %get reply without its leading status)
proc ds_parse {line} {
    if {![regexp {^(\S+) (\d+) (\d+) (\d+) \{(.*)\}$} $line -> name type ts len data]} { return "" }
    list $name $type $ts $data
}
proc ds_get {name} {
    set r [ds_req "%get $name"]
    if {![regexp {^1 (.*)$} $r -> line]} { return "" }
    ds_parse $line
}

set listen_port 0
proc ds_accept {s addr port} {
    fconfigure $s -blocking 0 -buffering line -encoding utf-8 -translation lf
    fileevent $s readable [list ds_read $s]
    set ::ds_conn $s
}
proc ds_read {s} {
    while {[gets $s line] >= 0} {
        set d [ds_parse $line]
        if {$d ne ""} {
            if {[catch {on_dp {*}$d} err]} { log "error handling [lindex $d 0]: $err\n$::errorInfo" }
        }
    }
    if {[eof $s]} {
        close $s
        log "dserv closed the subscription; registering again"
        after 1000 ds_join
        return
    }
    pump
}

set followed {}
proc follow {names} {
    global followed
    foreach n $names {
        if {$n ni $followed} { ds_match $n; lappend followed $n }
    }
}

# register, subscribe, then read the current values: a mirror started (or
# restarted) mid-session draws from the next trial boundary
proc ds_join {} {
    global opt rig followed
    if {[catch {
        if {![info exists ::ds_srv]} {
            set ::ds_srv [socket -server ds_accept 0]
            set ::listen_port [lindex [fconfigure $::ds_srv -sockname] 2]
        }
        set r [ds_req "%reg @ME@ 0"]
        if {[string trim $r] ne "1"} { error "%reg answered \"$r\"" }
    } err]} {
        log "cannot register with dserv at $opt(dserv): $err; trying again"
        after 2000 ds_join
        return
    }
    set followed {}
    set base {stim/script stim/lib/* stim/bind stim/cmd stim/frame stimdg
        ess/screen_* ess/in_obs ess/status ess/rmt_connected ess/system ess/protocol ess/variant}
    if {$opt(eye) ne "off"} { lappend base $opt(eye) }
    follow $base
    # current values
    foreach n {ess/system ess/protocol ess/variant ess/rmt_connected ess/status ess/in_obs
        ess/screen_halfx ess/screen_halfy ess/screen_w ess/screen_h ess/screen_refresh_rate ess/screen_bg
        stimdg stim/bind} {
        set d [ds_get $n]
        if {$d ne ""} { note_value {*}$d }
    }
    # the script's libraries: those published with it (configure_stim sends
    # them just before the script; an older load's stay in dserv)
    set d [ds_get stim/script]
    set sts [expr {$d ne "" ? [lindex $d 2] : 0}]
    foreach k [split [ds_req "%keys"] " "] {
        if {[string match stim/lib/* $k]} {
            set l [ds_get $k]
            if {$l ne "" && [lindex $l 2] <= $sts && $sts - [lindex $l 2] < 60000000} { note_value {*}$l }
        }
    }
    foreach n $rig(bind) {
        set l [ds_get $n]
        if {$l ne ""} { note_value {*}$l }
    }
    if {$d ne ""} { note_value {*}$d }
    log "joined dserv $opt(dserv) (callback port $::listen_port): $rig(load), stim2 [expr {$rig(connected) eq "1" ? "connected" : "NOT connected"}],\
 [expr {$rig(script) ne "" ? "script [file tail $rig(path)]" : "no script"}], [llength $rig(bind)] bound streams,\
 [expr {$rig(status) eq "running" ? "running, in_obs $rig(in_obs)" : $rig(status)}]"
    to_waiting [expr {$rig(script) ne "" ? "ready" : "waiting"}] "joined mid-session"
}

# ---------------------------------------------------------------------------
# records
# ---------------------------------------------------------------------------
# a value that describes the rig, whether it came by subscription or %get
proc note_value {name type ts data} {
    global rig
    switch -glob -- $name {
        stim/script {
            set j [json_to_dict $data]
            set rig(script) [dict get $j body]
            set rig(hash) [dict get $j hash]
            set rig(path) [dict get $j path]
            set rig(script_ts) $ts
        }
        stim/lib/* {
            set j [json_to_dict $data]
            dict set rig(libs) [dict get $j name] [list $ts [dict get $j body]]
        }
        stim/bind {
            set rig(bind) {}
            catch {
                foreach b [dict get [json_to_dict $data] bindings] { lappend rig(bind) [dict get $b name] }
            }
            follow $rig(bind)
        }
        stimdg { set rig(stimdg) $data }
        ess/screen_bg { if {[llength $data] == 3} { dict set rig(geom) bg $data } }
        ess/screen_refresh_rate { dict set rig(geom) hz [string trim $data] }
        ess/screen_* { dict set rig(geom) [string range $name 11 end] [string trim $data] }
        ess/in_obs { set rig(in_obs) [string trim $data] }
        ess/status { set rig(status) [string trim $data] }
        ess/rmt_connected { set rig(connected) [string trim $data] }
        ess/system { set rig(load) [string trim $data] }
        ess/protocol { set rig(load) "[lindex [split $rig(load) /] 0]/[string trim $data]" }
        ess/variant { set rig(load) "[join [lrange [split $rig(load) /] 0 1] /]/[string trim $data]" }
        default {
            if {$name in $rig(bind)} { dict set rig(last) $name [list $ts $type $data] }
        }
    }
}

proc on_dp {name type ts data} {
    global rig mode P start_ts opt
    switch -glob -- $name {
        stim/frame {
            set v [framelock::frame_decode [binary decode base64 $data]]
            if {$v ne ""} { on_frame [list $ts {*}$v] }
            return
        }
        stim/cmd {
            if {$mode ne "run" || $ts <= $start_ts} return
            set c [framelock::cmd_record $data]
            if {$c eq ""} return                    ;# hash-only: a script load
            lassign $c t_send cmd seq ack
            lassign $ack swap flip dq
            if {$swap eq "" || $dq eq ""} {
                # no ACK stamps: ESS has no stim2 to send to
                return
            }
            if {[framelock::skipped_cmd $cmd]} return
            framelock::plan_cmd P $swap $flip $dq $cmd
            return
        }
        stim/script {
            set old $rig(hash)
            note_value $name $type $ts $data
            if {$rig(script) eq ""} {
                # configure_stim publishes the empty record first, the new
                # script last: a reload is under way
                set rig(libs) [dict create]
                to_waiting waiting "the rig is loading [lindex [split $rig(load) /] 0]"
            } elseif {$mode eq "waiting" || $rig(hash) ne $old} {
                to_waiting ready "new script [file tail $rig(path)]"
            }
            return
        }
        ess/in_obs {
            set was $rig(in_obs)
            note_value $name $type $ts $data
            if {$was eq "1" && $rig(in_obs) eq "0"} { boundary $ts "end of observation" }
            return
        }
        ess/status {
            set was $rig(status)
            note_value $name $type $ts $data
            if {$rig(status) eq "running" && $was ne "running"} { boundary $ts "start" }
            return
        }
        stimdg {
            note_value $name $type $ts $data
            # the rig's stim2 follows stimdg too (readdg): a new table
            # mid-session reaches it as a forwarded datapoint
            if {$mode eq "run" && $ts > $start_ts} { framelock::plan_dp P $ts stimdg $type $data }
            return
        }
    }
    if {$name eq $opt(eye)} {
        if {[binary scan [binary decode base64 $data] f2 v] == 1 && [llength $v] == 2} {
            lappend ::gaze [list [now_us] {*}$v]
            incr ::st(eye)
        }
        return
    }
    note_value $name $type $ts $data
    if {$name in $rig(bind) && $mode eq "run" && $ts > $start_ts} {
        framelock::plan_dp P $ts $name $type $data
    }
}

proc on_frame {f} {
    global mode queue last_sc last_frame offs P
    lassign $f ts frame sc flip_us ticks_us
    # a restart of the rig's stim2: its swapcount starts again
    if {$last_sc ne "" && $sc < $last_sc} {
        set last_sc $sc
        to_waiting waiting "the rig's stim2 restarted"
        return
    }
    if {$last_sc ne "" && $mode eq "run" && $sc > $last_sc + 1} { incr ::st(missing) [expr {$sc - $last_sc - 1}] }
    set last_sc $sc
    set last_frame $f
    # stim clock - dserv clock, over the recent frames
    lappend offs [expr {$ticks_us - $ts}]
    if {[llength $offs] > 41} { set offs [lrange $offs end-40 end] }
    if {$mode ne "run"} return
    lappend ::st(arr) [expr {([now_us] - $flip_us) / 1000.0}]
    lappend queue [list $f [now_us]]
}

# ---------------------------------------------------------------------------
# the mirror's stim2
# ---------------------------------------------------------------------------
proc stim_connect {} {
    global sock opt
    if {$sock ne ""} { return 1 }
    if {[catch {framelock::connect $opt(host) $opt(port)} s]} { return 0 }
    set v [framelock::send $s {package require stimess}]
    if {[framelock::is_error $v] || [catch {package vcompare $v 1.1} c] || $c < 0} {
        log "stim2 at $opt(host):$opt(port) has no stimess 1.1 (got: $v)"; close $s; return 0
    }
    if {[framelock::send $s {info commands stepFrame}] ne "stepFrame"} {
        log "stim2 at $opt(host):$opt(port) has no stepFrame (stim2 0.28.4 or newer)"; close $s; return 0
    }
    set sock $s
    set ::period_ms [framelock::send $s {screen_set FrameDuration}]
    if {![string is double -strict $::period_ms] || $::period_ms <= 0} { set ::period_ms 16.667 }
    log "stim2 [framelock::send $s {catch {stimVersion} v; set v}] at $opt(host):$opt(port),\
 [framelock::send $s {screen_set WinWidth}]x[framelock::send $s {screen_set WinHeight}]"
    if {$opt(eye) ne "off"} { framelock::send $sock [framelock::gaze_style] }
    return 1
}
# a request; a lost stim2 drops back to waiting (reconnected at the next try)
proc stim {cmd} {
    global sock
    if {$sock eq "" && ![stim_connect]} { error "no stim2" }
    if {[catch {framelock::send $sock $cmd} r]} {
        catch {close $sock}
        set sock ""; set ::stepped 0
        log "lost the mirror's stim2: $r"
        to_waiting [expr {$::rig(script) ne "" ? "ready" : "waiting"}] "the mirror's stim2 restarted"
        after 1000 stim_reconnect
        error "no stim2"
    }
    return $r
}
proc stim_reconnect {} {
    if {$::sock ne ""} return
    if {[stim_connect]} { show_waiting } else { after 1000 stim_reconnect }
}

# the waiting screen: nothing of the rig's, the reason in words
proc show_waiting {} {
    global mode why rig opt stepped
    if {$stepped} { catch {stim {stepMode 0}}; set stepped 0 }
    set what [expr {$mode eq "ready" ? "waiting for the next trial" : "waiting for the rig"}]
    set line2 [expr {$rig(load) ne "" ? "$rig(load)  --  $why" : $why}]
    set s {
        resetObjList; glistInit 1
        if {[catch {textFont mirror_sans NotoSans-Regular.ttf}] && [lsearch [textFonts] mirror_sans] < 0} return
        set o [text %TEXT% -font mirror_sans -size 0.9]
        textColor $o 0.6 0.6 0.6 1
        glistAddObject $o 0
        set o [text %SUB% -font mirror_sans -size 0.5]
        textColor $o 0.45 0.45 0.45 1
        translateObj $o 0 -1.5
        glistAddObject $o 0
        glistSetVisible 1; glistSetCurGroup 0; redraw
    }
    catch {
        stim [string map [list %TEXT% [list "mirror of $opt(dserv): $what"] %SUB% [list $line2]] $s]
        stim [list setWindowTitle "mirror of $opt(dserv) ($what)"]
    }
}

proc to_waiting {m reason} {
    global mode why queue P
    set mode $m; set why $reason
    set queue {}; set P ""
    log "$m: $reason"
    show_waiting
}

# a trial boundary: from waiting-for-a-trial, set up and start drawing
proc boundary {ts what} {
    global mode rig start_ts P prev_cmds queue clock_ms prevflip stepped opt offs
    if {$mode ne "ready"} return
    if {$rig(connected) eq "0"} { log "boundary ($what), but ESS has no stim2: staying ready"; return }
    set libs [dict create]
    dict for {n lb} $rig(libs) { dict set libs $n [lindex $lb 1] }
    set d [dict create libs $libs script $rig(script) stimdg $rig(stimdg) geom $rig(geom) \
               title "mirror of $opt(dserv): $rig(load)"]
    set t0 [now_us]
    if {[catch {
        stim {resetObjList; glistInit 1; redraw}
        framelock::setup $::sock $d log
    } err]} {
        log "setup failed: $err"
        to_waiting waiting "setup failed"
        return
    }
    set tick_off [expr {[llength $offs] ? [median $offs] : 0}]
    set P [framelock::plan_new $tick_off]
    # each bound stream as it stood: a cursor stands where it stood
    dict for {n v} $rig(last) {
        if {$n in $rig(bind)} { lassign $v vts type data; framelock::plan_prime P $vts $n $type $data }
    }
    set start_ts $ts
    set prev_cmds ""
    set queue {}
    set mode run
    set stepped 0
    log "run from the $what: set up in [format %.0f [expr {([now_us] - $t0) / 1000.0}]] ms"
}

# ---------------------------------------------------------------------------
# drawing
# ---------------------------------------------------------------------------
# the frame at the head of the queue is ready when the commands its
# cmds_applied advance promises are in, or it has waited -wait ms
proc head_ready {} {
    global queue P prev_cmds opt
    lassign [lindex $queue 0] f arrived
    lassign $f - - sc - - - cmds
    if {$prev_cmds eq ""} { return 1 }
    set need [expr {$cmds - $prev_cmds}]
    if {$need <= 0 || [framelock::plan_waiting P $sc] >= $need} { return 1 }
    if {[now_us] - $arrived >= $opt(wait) * 1000} {
        # a command the rig applied that stim/cmd does not carry (ESS's
        # raw queries are not recorded), or one still on its way
        incr ::st(timeouts)
        return 1
    }
    return 0
}

proc pump {} {
    global mode queue P prev_cmds clock_ms prevflip stepped opt pump_after
    if {$pump_after ne ""} { after cancel $pump_after; set pump_after "" }
    if {$mode ne "run"} return
    # every ready frame's inputs; only the newest is drawn
    set inputs {}; set taken {}; set taken_at {}
    while {[llength $queue] && [head_ready]} {
        lassign [lindex $queue 0] f at
        lappend taken_at $at
        set queue [lrange $queue 1 end]
        lappend inputs {*}[framelock::plan_frame P $f]
        set prev_cmds [lindex $f 6]
        lappend taken $f
    }
    if {[llength $queue]} {
        # the head is waiting for a command: look again at its deadline
        set left [expr {$opt(wait) - ([now_us] - [lindex $queue 0 1]) / 1000}]
        set pump_after [after [expr {max(1, int($left))}] pump]
    }
    if {![llength $taken]} return
    set f [lindex $taken end]
    set arrived [lindex $taken_at end]
    lassign $f ts frame sc flip_us ticks_us
    set flip [expr {$ticks_us / 1000.0}]
    # the first frame: onto the rig's clock, before its first input
    if {!$stepped} {
        set t0 $flip
        if {[llength $inputs]} { set t0 [expr {min($t0, [lindex [lsort -real -index 0 $inputs] 0 0])}] }
        set t0 [expr {$t0 - 1.0}]
        if {[catch {stim [list stepMode 1 $t0]}]} return
        set clock_ms $t0; set prevflip $t0; set stepped 1
    }
    set inputs [lsort -real -index 0 $inputs]
    if {$opt(verbose)} {
        foreach ev $inputs { if {[lindex $ev 1] eq "cmd"} { log "frame $sc cmd [string range [lindex $ev 2] 0 70]" } }
    }
    set req [framelock::frame_request $inputs clock_ms $prevflip $flip [gaze_pre]]
    set t_draw [now_us]
    if {[catch {stim $req} r]} return
    set now [now_us]
    presented $now
    if {[framelock::is_error $r]} {
        incr ::st(errors)
        log "ERROR frame $sc: $r"
        # the clock cannot go back: start again from this frame's clock
        if {[string match "*backwards*" $r] || [string match "*before*" $r]} {
            catch {stim {stepMode 0}}; set stepped 0
        }
        return
    }
    lassign $r res errs
    foreach e $errs {
        lassign $e n msg
        lassign [lindex $inputs $n] - kind p1
        incr ::st(errors)
        log "ERROR [expr {$kind eq "cmd" ? [string range $p1 0 60] : "inject $p1"}] -> $msg"
    }
    set prevflip $clock_ms
    set ::last_present $now
    incr ::st(drawn)
    incr ::st(dropped) [expr {[llength $taken] - 1}]
    if {$flip_us > 0} { lappend ::st(lag) [expr {($now - $flip_us) / 1000.0}] }
    lappend ::st(ready) [expr {($t_draw - $arrived) / 1000.0}]
    lappend ::st(draw) [expr {($now - $t_draw) / 1000.0}]
    # anything that arrived while this frame was being drawn
    after idle pump
}

# the gaze: the newest sample and a trail, as overlay commands ("" when
# unchanged since the last present)
proc gaze_cmd_now {} {
    global gaze opt
    set now [now_us]
    set cut [expr {$now - $opt(trail) * 1000}]
    set i 0
    while {$i < [llength $gaze] && [lindex $gaze $i 0] < $cut} { incr i }
    set gaze [lrange $gaze $i end]
    if {![llength $gaze]} {
        # no sample for a trail's length: hide
        return [framelock::gaze_cmd "" ""]
    }
    set trail {}
    for {set k [expr {[llength $gaze] - 1}]} {$k >= 0} {incr k -1} {
        lappend trail [lindex $gaze $k 1] [lindex $gaze $k 2]
    }
    framelock::gaze_cmd [lrange [lindex $gaze end] 1 2] $trail
}
proc gaze_pre {} {
    global opt gaze_shown
    if {$opt(eye) eq "off"} { return "" }
    set g [gaze_cmd_now]
    if {$g eq $gaze_shown} { return "" }
    set gaze_shown $g
    return "$g\n"
}
proc gaze_repaint {} {
    global opt gaze_shown sock
    if {$opt(eye) eq "off" || $sock eq ""} return
    set g [gaze_cmd_now]
    if {$g eq $gaze_shown} return
    # frameRepaint needs a frame drawn with the overlay up; until then the
    # next frame shows the gaze
    if {[catch {stim "$g\nframeRepaint"} r]} return
    if {![framelock::is_error $r]} {
        set gaze_shown $g
        presented [now_us]
        incr ::st(repaints)
    } elseif {![string match "*empty*" $r]} {
        set gaze_shown $g
    } else {
        # nothing kept to repaint over yet (waiting, unstepped): draw
        if {![catch {stim "$g\nredraw"}]} { set gaze_shown $g }
    }
}
# The gaze moves between rig frames: repaint it once a refresh, LATE in
# the refresh, -late ms before the next vsync. A present waits for its
# vsync, and a rig frame that arrives while a repaint holds the driver
# goes out a vsync later; a repaint near the end of the interval leaves
# the earlier part of it to the rig's frames. The vsync phase is where the
# last present returned.
set gaze_after ""
proc presented {t} {
    global gaze_after period_ms opt
    set ::last_present $t
    if {$gaze_after ne ""} { after cancel $gaze_after }
    set wait [expr {$period_ms - $opt(late)}]
    set gaze_after [after [expr {max(1, int($wait))}] gaze_tick]
}
proc gaze_tick {} {
    global gaze_after period_ms last_present opt
    set gaze_after ""
    if {$::mode ne "run" || ![llength $::queue]} { gaze_repaint }
    if {$gaze_after eq ""} {
        # nothing presented: keep the phase of the last present
        set now [now_us]
        set next [expr {$last_present / 1000.0 + $period_ms - $opt(late)}]
        while {$next < $now / 1000.0 + 1} { set next [expr {$next + $period_ms}] }
        set gaze_after [after [expr {max(1, int($next - $now / 1000.0))}] gaze_tick]
    }
}

# ---------------------------------------------------------------------------
# reports
# ---------------------------------------------------------------------------
proc report {} {
    global st opt mode total
    set dt [expr {([now_us] - $st(t0)) / 1e6}]
    foreach k {drawn dropped missing errors} { incr total($k) $st($k) }
    if {$mode eq "run" || $st(drawn)} {
        log [format "%s: %d frames drawn, %d dropped (behind), %d not received; lag flip->present ms p50 %s p95 %s max %s\
 (p50: flip->arrival %s, arrival->ready %s, ready->present %s); %d command waits ran out; %d errors;\
 gaze %d repaints, %.0f samples/s" \
                 $mode $st(drawn) $st(dropped) $st(missing) \
                 [pct $st(lag) 0.5] [pct $st(lag) 0.95] [pct $st(lag) 1.0] \
                 [pct $st(arr) 0.5] [pct $st(ready) 0.5] [pct $st(draw) 0.5] \
                 $st(timeouts) $st(errors) $st(repaints) [expr {$st(eye) / $dt}]]
        # a record cannot arrive before its flip: this host's clock is
        # behind the rig's (chrony still slewing after a boot), and only
        # ready->present, timed here alone, means anything
        if {[llength $st(arr)] && [pct $st(arr) 0.5] < -5.0} {
            log "  NOTE: the clocks disagree by about [format %.0f [expr {-[pct $st(arr) 0.5]}]] ms (this host behind the rig):\
 the lag is not measurable until they agree (chronyc tracking)"
        }
    }
    stats_reset
    after [expr {int($opt(report) * 1000)}] report
}

# ---------------------------------------------------------------------------
log "mirror: rig dserv $opt(dserv), stim2 $opt(host):$opt(port), command wait $opt(wait) ms, gaze $opt(eye)"
while {![stim_connect]} {
    if {![info exists warned]} { log "waiting for the mirror's stim2 at $opt(host):$opt(port)"; set warned 1 }
    after 1000
}
show_waiting
ds_join
after [expr {int($opt(report) * 1000)}] report
gaze_tick
vwait forever
