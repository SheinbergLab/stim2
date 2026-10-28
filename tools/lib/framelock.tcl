# framelock.tcl -- the frame-locked core shared by tools/replay/replay.tcl
# (a recorded session, from its .ess or obs DG) and tools/mirror/mirror.tcl
# (a live one, from the rig's dserv):
#
#   - the stim2 message port client (one write per request)
#   - the stim/cmd and stim/frame record decoders
#   - setup, as ESS's configure_stim does it in playback form: prelude,
#     libraries, script, stim_init, stimdg, the rig's degrees, background
#   - the planner: which commands and datapoints the rig had applied before
#     each of its frames, fed incrementally (a replay feeds it everything
#     up front, a mirror as records arrive)
#   - the frame request: a rig frame's inputs, the gaze and stepFrame as ONE
#     message-port request, with per-input errors collected
#
#     source [file join [file dirname [info script]] .. lib framelock.tcl]
#
# Placement, from stim/frame (one record per rig swap: frame, swapcount,
# flip_us, ticks_us, missed, cmds_applied, ds_dispatched and, from stim2
# 0.28.5, eval_us):
#   commands    by their ACK stamp: applied with SwapCount = swap, so first
#               shown in frame swap+1; a '!' command is answered after its
#               swap, with a flip, and shows in frame swap. Each goes in
#               the first frame at or after that swapcount, clock at its
#               dq tick.
#   datapoints  by the frame's ds_dispatched advance: that many records, in
#               order, among those dserv had before the frame's record
#               (a shortfall is a forwarded datapoint not being followed),
#               clock at its dserv time on the rig clock (tick_off).
# then stepFrame at the instant the rig computed the frame (eval_us), or,
# for a record without it, at max(clock, previous flip), with the frame's
# own flip.

namespace eval framelock {
    # configure-time commands that may be in stim/cmd and that a playback
    # must not repeat
    variable skip_prefixes {stim::prelude stimFrameLog {set dservhost}}
}

# ---------------------------------------------------------------------------
# stim2 message port: 4-byte big-endian length, payload, same framing back
# ---------------------------------------------------------------------------
proc framelock::connect {host port} {
    set s [socket $host $port]
    fconfigure $s -translation binary -buffering none
    return $s
}
proc framelock::send {sock cmd} {
    # one write: header and payload as two writes on an unbuffered socket
    # meet Nagle and the server's delayed ACK on Linux, ~40 ms per command
    set cmd [encoding convertto utf-8 $cmd]
    puts -nonewline $sock [binary format I [string length $cmd]]$cmd
    flush $sock
    set hdr [read $sock 4]
    if {[string length $hdr] != 4} { error "stim2 closed the connection" }
    binary scan $hdr I n
    set n [expr {$n & 0xffffffff}]
    return [expr {$n ? [encoding convertfrom utf-8 [read $sock $n]] : ""}]
}
proc framelock::is_error {reply} { string match "!TCL_ERROR*" $reply }

proc framelock::skipped_cmd {cmd} {
    variable skip_prefixes
    foreach pre $skip_prefixes { if {[string match "$pre*" $cmd]} { return 1 } }
    return 0
}

# ---------------------------------------------------------------------------
# records
# ---------------------------------------------------------------------------
# A stim/cmd record (JSON) as {t_send cmd seq {swap flip dq}}, or "" for a
# hash-only one (a command longer than 4096 bytes: the script load). The
# ACK1 stamps are "" when the command went out without them.
proc framelock::cmd_record {json} {
    set j [json_to_dict $json]
    if {![dict exists $j cmd]} { return "" }
    set ack {}
    foreach k {swap flip dq} {
        set v [expr {[dict exists $j $k] ? [dict get $j $k] : ""}]
        lappend ack [expr {$v eq "null" ? "" : $v}]
    }
    list [dict get $j t_send] [dict get $j cmd] [dict get $j seq] $ack
}

# The swapcount of the first frame that shows a command: swap+1, or swap
# for a '!' command, which stim2 answers after the swap it caused.  A '!'
# is known by its text: its flip time is null where the rig's swap sync
# has none (glfinish, a Mac), and a '!' taken for a plain command waits
# for, then lands in, the frame after its own.
proc framelock::cmd_target {swap flip {cmd ""}} {
    expr {($flip ne "" && $flip != 0) || [string match {@!*} $cmd] || [string match {!*} $cmd]
          ? $swap : $swap + 1}
}

# stim/frame's values: 8 (64 bytes), or 7 (56, before eval_us) with ""
# for eval_us; "" when malformed
proc framelock::frame_decode {bytes} {
    set n [expr {[string length $bytes] / 8}]
    if {$n in {7 8} && [binary scan $bytes w$n v] == 1} {
        if {$n == 7} { lappend v "" }
        return $v
    }
    return ""
}

# ---------------------------------------------------------------------------
# setup: ESS's configure_stim in playback form.  d holds
#   libs     dict name -> body           script   the stim/script body
#   stimdg   base64 of the DG ("" none)   geom     dict halfx halfy ?w h hz bg?
#   title    window title ("" leaves it)
# log is a command prefix given one line of text.  Errors (as a Tcl error)
# when the prelude or the script fails; a library, stimdg or geometry
# failure is logged and setup goes on.  Returns the window size {w h}.
# ---------------------------------------------------------------------------
proc framelock::setup {sock d log} {
    set libs   [expr {[dict exists $d libs]   ? [dict get $d libs]   : {}}]
    set geom   [expr {[dict exists $d geom]   ? [dict get $d geom]   : {}}]
    set stimdg [expr {[dict exists $d stimdg] ? [dict get $d stimdg] : ""}]
    set title  [expr {[dict exists $d title]  ? [dict get $d title]  : ""}]
    send $sock {package require stimess}
    set r [send $sock {stim::prelude playback}]
    if {[is_error $r]} { error "prelude failed: $r" }
    dict for {name body} $libs {
        send $sock "catch {package forget $name}"
        set r [send $sock $body]
        if {[is_error $r]} {
            {*}$log "library $name failed to load: $r"
        } else {
            {*}$log "sourced lib $name"
        }
    }
    set r [send $sock [dict get $d script]]
    if {[is_error $r]} { error "stim script failed to load: $r" }
    send $sock {catch {stim_init}}
    {*}$log "bindings: [send $sock {stim::bindings_json}]"
    if {$stimdg ne ""} {
        set r [send $sock [list stim::inject stimdg $stimdg 6]]
        if {[is_error $r]} {
            {*}$log "stimdg inject failed: $r"
        } else {
            {*}$log "stimdg loaded: [send $sock {dl_length stimdg:stimtype}] trials"
        }
    }
    # the rig's degree mapping onto this window; the pixel size is stim2's
    # own, so say when the aspect does not match the rig's
    set win_w [send $sock {screen_set WinWidth}]
    set win_h [send $sock {screen_set WinHeight}]
    if {[dict exists $geom halfx] && [dict exists $geom halfy]} {
        set hx [dict get $geom halfx]; set hy [dict get $geom halfy]
        set r1 [send $sock [list screen_set HalfScreenDegreeX $hx]]
        set r2 [send $sock [list screen_set HalfScreenDegreeY $hy]]
        if {[is_error $r1] || [is_error $r2]} {
            {*}$log "WARNING: could not set the rig's degree mapping: $r1 $r2"
        } else {
            {*}$log "geometry: +/-$hx x +/-$hy deg onto this ${win_w}x${win_h} window"
            if {[dict exists $geom w] && [dict exists $geom h] && $win_h > 0 && [dict get $geom h] > 0} {
                set gw [dict get $geom w]; set gh [dict get $geom h]
                set rig_aspect [expr {double($gw) / $gh}]
                set win_aspect [expr {double($win_w) / $win_h}]
                if {abs($rig_aspect - $win_aspect) / $rig_aspect > 0.05} {
                    {*}$log [format "WARNING: window aspect %.2f vs rig %.2f -- circles will be ellipses;\
 start stim2 with -w %d -h %d (or any %dx%d multiple)" $win_aspect $rig_aspect \
                                 [expr {int($gw/2)}] [expr {int($gh/2)}] $gw $gh]
                }
            }
        }
    }
    # the background the display had (ess/screen_bg): a script that never
    # sets one drew on it; one that does sets it again itself
    if {[dict exists $geom bg]} {
        set r [send $sock [list setBackground {*}[dict get $geom bg]]]
        if {[is_error $r]} { {*}$log "WARNING: could not set the background [dict get $geom bg]: $r" }
    }
    # an older stim2 has no setWindowTitle, which is fine
    if {$title ne ""} { catch { send $sock [list setWindowTitle $title] } }
    install $sock
    list $win_w $win_h
}

# the helpers a frame request calls in stim2: each input runs at global
# level, as it would from the message port, and an error is collected
# instead of ending the request
proc framelock::install {sock} {
    send $sock {
        proc ::framelock_do {i script} {
            if {[catch {uplevel #0 $script} e]} { lappend ::framelock_err [list $i $e] }
        }
        proc ::framelock_inject {i args} {
            if {[catch {stim::inject {*}$args} e]} { lappend ::framelock_err [list $i $e] }
        }
    }
}

# ---------------------------------------------------------------------------
# the planner.  A dict, kept by the caller in a variable:
#   cmds      {target t cmd} waiting for the frame that shows them
#   dps       {ts name type data} not yet placed, in arrival order
#   prime     inputs for the next frame regardless of counts
#   ds_prev   the previous frame's ds_dispatched ("" before the first)
#   tick_off  stim clock - dserv clock, us
# ---------------------------------------------------------------------------
proc framelock::plan_new {{tick_off 0}} {
    dict create cmds {} dps {} prime {} ds_prev "" tick_off $tick_off foreign 0
}

# a command with its ACK stamps; dq is the rig tick (ms) it was dequeued
proc framelock::plan_cmd {pv swap flip dq cmd} {
    upvar $pv P
    dict lappend P cmds [list [cmd_target $swap $flip $cmd] $dq $cmd]
}

# a datapoint as dserv stamped it (ts, us on the dserv clock)
proc framelock::plan_dp {pv ts name type data} {
    upvar $pv P
    dict lappend P dps [list $ts $name $type $data]
}

# a datapoint that goes in with the next frame whatever its counts say:
# a stream's value as it stood when the plan starts mid-session
proc framelock::plan_prime {pv ts name type data} {
    upvar $pv P
    dict lappend P prime [list [expr {($ts + [dict get $P tick_off]) / 1000.0}] dp $name $type $data $ts]
}

# commands waiting with a target at or before swapcount sc
proc framelock::plan_waiting {pv sc} {
    upvar $pv P
    set n 0
    foreach c [dict get $P cmds] { if {[lindex $c 0] <= $sc} { incr n } }
    return $n
}

# The inputs of the frame f ({dserv_ts frame swapcount flip_us ticks_us
# missed cmds ds ?eval_us?}), in time order: {t cmd text} and {t dp name type data ts}.
# Removes them from the plan.
proc framelock::plan_frame {pv f} {
    upvar $pv P
    lassign $f fts - sc - - - - ds
    set ev [dict get $P prime]
    dict set P prime {}
    set keep {}
    foreach c [dict get $P cmds] {
        lassign $c tf dq cmd
        if {$tf <= $sc} { lappend ev [list $dq cmd $cmd] } else { lappend keep $c }
    }
    dict set P cmds $keep
    set n [expr {[dict get $P ds_prev] eq "" ? -1 : $ds - [dict get $P ds_prev]}]
    dict set P ds_prev $ds
    set off [dict get $P tick_off]
    set dps [dict get $P dps]
    set k 0; set ndps [llength $dps]
    while {$k < $ndps && $n != 0 && [lindex $dps $k 0] < $fts} {
        lassign [lindex $dps $k] ts name type data
        lappend ev [list [expr {($ts + $off) / 1000.0}] dp $name $type $data $ts]
        incr k; incr n -1
    }
    dict set P dps [lrange $dps $k end]
    dict incr P foreign [expr {$n > 0 ? $n : 0}]
    lsort -real -index 0 $ev
}

# ---------------------------------------------------------------------------
# the frame request: the frame's inputs (stepClock to each one's time
# first), then pre (the gaze), stepFrame with the flip, then post (frame
# sink writes), as one script.  The frame is evaluated at eval_ms, the
# instant the rig computed it (stim/frame's eval_us / 1000), when given:
# every input was applied before it, so none moves the clock past it (a
# datapoint's time is dserv's receipt on the rig clock, an estimate).
# Without it, at max(clock, prevflip).  clockvar holds the
# stepped clock and is left at the flip.  The reply is {result errors}:
# stepFrame's dict and a list of {input-index message}; a refused
# stepFrame makes the whole request a !TCL_ERROR.
# ---------------------------------------------------------------------------
proc framelock::frame_request {inputs clockvar prevflip flip {pre ""} {post ""} {eval_ms ""}} {
    upvar $clockvar clock
    set s "set ::framelock_err {}\n"
    append s [frame_body $inputs clock $prevflip $flip $pre $post $eval_ms 1 0]
    append s {list $::framelock_r $::framelock_err}
    return $s
}

# Several rig frames as one request: frames is a list of {inputs flip
# eval_ms}, and every one but the last is computed with stepFrame
# -nopresent (stim2 0.28.7): updated, drawn and its scripts run, not shown.
# A mirror on a display slower than the rig's computes every rig frame
# this way, so a script that latches state in a frame (a run's time zero,
# a frame count) sees each one, and presents only the newest.  pre and
# post go with the last.  Errors index the frames' inputs taken in order.
proc framelock::frames_request {frames clockvar prevflipvar {pre ""} {post ""}} {
    upvar $clockvar clock
    upvar $prevflipvar prevflip
    set s "set ::framelock_err {}\n"
    set base 0; set n [llength $frames]; set k 0
    foreach fr $frames {
        lassign $fr inputs flip eval_ms
        set last [expr {[incr k] == $n}]
        append s [frame_body $inputs clock $prevflip $flip \
                      [expr {$last ? $pre : ""}] [expr {$last ? $post : ""}] $eval_ms $last $base]
        set prevflip $clock
        incr base [llength $inputs]
    }
    append s {list $::framelock_r $::framelock_err}
    return $s
}

proc framelock::frame_body {inputs clockvar prevflip flip pre post eval_ms present base} {
    upvar $clockvar clock
    set s ""
    set i $base
    foreach ev $inputs {
        lassign $ev t kind p1 p2 p3 p4
        if {$eval_ms ne "" && $t > $eval_ms} { set t $eval_ms }
        if {$t > $clock} { append s [list stepClock $t] \n; set clock $t }
        if {$kind eq "cmd"} {
            # the '@' (ACK header) and '!' (reply after the swap) prefixes
            # are the message port's, not Tcl
            regexp {^@?!?(.*)$} $p1 -> p1
            append s [list ::framelock_do $i $p1] \n
        } else {
            append s [list ::framelock_inject $i $p1 $p3 $p2 $p4] \n
        }
        incr i
    }
    set eval [expr {$eval_ms ne "" ? max($clock, $eval_ms) : max($clock, $prevflip)}]
    if {$flip < $eval} { set flip $eval }
    append s $pre
    append s "set ::framelock_r \[stepFrame $eval $flip[expr {$present ? "" : " -nopresent"}]\]\n"
    append s $post
    set clock $flip
    return $s
}

# 1 when stim2's stepFrame takes -nopresent (0.28.7).  Ask an unstepped
# stim2: stepped, the probe would draw a frame.
proc framelock::has_nopresent {sock} {
    set r [send $sock {catch {stepFrame -nopresent} e; set e}]
    expr {![string match "*-nopresent*" $r]}
}

# ---------------------------------------------------------------------------
# the gaze on stim2's overlay layer: a disc at the newest sample, a fading
# trail behind (docs/headless.md, "Overlay")
# ---------------------------------------------------------------------------
proc framelock::gaze_style {} {
    return {overlay set gaze_trail -size 0.25 -color {1 0.55 0.1 0.35} -show 0;
        overlay set gaze -size 0.7 -color {1 0.55 0.1 0.9} -show 0}
}
# xy the newest sample {x y}, trail {x y x y ...} newest first; "" hides
proc framelock::gaze_cmd {xy trail} {
    if {$xy eq ""} { return "overlay set gaze -show 0; overlay set gaze_trail -show 0" }
    lassign $xy x y
    return "overlay set gaze -show 1 -points {[format %.3f $x] [format %.3f $y]};\
 overlay set gaze_trail -show 1 -points {[lmap v $trail {format %.3f $v}]}"
}
