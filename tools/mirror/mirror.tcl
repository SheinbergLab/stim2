#!/usr/bin/env dlsh
# mirror.tcl -- a live mirror of a rig's display: a stim2 of its own (on its
# own monitor, or a window) drawing every frame the rig's stim2 draws, from
# the rig's dserv, with the gaze over it.
#
#     dlsh tools/mirror/mirror.tcl -dserv 192.168.88.40 ?options?
#     dlsh tools/mirror/mirror.tcl -dserv 192.168.88.40 -launch
#
#       -dserv H       the rig's dserv (required)
#       -ws            subscribe over dserv's WebSocket (port -wsport, 2565)
#                      from the start; without it the driver uses dserv's
#                      %reg call-back and moves to the WebSocket by itself
#                      when dserv cannot reach it (NAT, a VPN, a cloud dserv)
#       -wss           the same with TLS (a dserv whose TXT says ssl=1); needs
#                      the Tcl tls package
#       -inproc        run inside stim2 (stim2 --mirror, which embeds this
#                      file): requests go by stimSend, not a port
#       -launch        start the mirror's stim2 too (on -port's base, a
#                      window half the rig's size, ess/screen_w x _h), and
#                      stop it on exit; the driver exits when it is closed
#       -size WxH      with -launch, the window size
#       -fullscreen    with -launch, fullscreen instead of a window
#       -stim2 PATH    with -launch, the stim2 to start (default: the app
#                      this script is bundled in, else the installed one,
#                      /Applications/stim2.app on a Mac,
#                      /usr/local/stim2/stim2 elsewhere)
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
#       -touch on|off  draw ESS's touches (ess/touch_press_deg,
#                      ess/touch_release_deg) on the overlay (default on)
#       -hold MS       keep the gaze up this long without a new sample
#                      (default 500)
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
#     rig on a 60 Hz monitor while something moves), every ready frame is
#     still computed -- updated, drawn, its scripts run (stepFrame
#     -nopresent, stim2 0.28.7) -- and only the newest is shown, so a
#     script that latches state in a particular frame sees every one.
#     With an older stim2 only the newest is computed, after the inputs of
#     all of them.
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
# embedded in stim2 (--mirror, src/mirror.cpp), framelock is evaluated
# first and there is no script file
set mirror_script [info script]
if {$mirror_script ne ""} { set mirror_script [file normalize $mirror_script] }
if {[info commands framelock::send] eq ""} {
    source [file join [file dirname $mirror_script] .. lib framelock.tcl]
}

set opt(dserv)   ""
set opt(host)    localhost
set opt(port)    5612
set opt(live)    0
set opt(wait)    50
set opt(eye)     eyetracking/position
set opt(trail)   100
set opt(touch)   on
set opt(hold)    500
set opt(report)  10
set opt(late)    5
set opt(verbose) 0
set opt(launch)  0
set opt(inproc)  0
set opt(size)    ""
set opt(full)    0
set opt(stim2)   ""
set opt(ws)      0
set opt(wss)     0
set opt(wsport)  2565

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
        -touch   { set opt(touch) [lindex $args 0]; set args [lrange $args 1 end] }
        -hold    { set opt(hold)  [lindex $args 0]; set args [lrange $args 1 end] }
        -report  { set opt(report) [lindex $args 0]; set args [lrange $args 1 end] }
        -late    { set opt(late)  [lindex $args 0]; set args [lrange $args 1 end] }
        -verbose { set opt(verbose) 1 }
        -launch  { set opt(launch) 1 }
        -inproc  { set opt(inproc) 1 }
        -size    { set opt(size)  [lindex $args 0]; set args [lrange $args 1 end] }
        -fullscreen { set opt(full) 1 }
        -stim2   { set opt(stim2) [lindex $args 0]; set args [lrange $args 1 end] }
        -ws      { set opt(ws) 1 }
        -wss     { set opt(ws) 1; set opt(wss) 1 }
        -wsport  { set opt(wsport) [lindex $args 0]; set args [lrange $args 1 end] }
        default  { puts stderr "unknown option $a"; exit 2 }
    }
}
if {$opt(dserv) eq ""} {
    puts stderr "usage: dlsh mirror.tcl -dserv HOST ?-host H? ?-port P? ?-live? ?-wait MS?\
 ?-eye NAME|off? ?-trail MS? ?-hold MS? ?-report S? ?-late MS? ?-verbose?\
 ?-launch ?-size WxH? ?-fullscreen? ?-stim2 PATH??"
    exit 2
}
if {$opt(inproc) && $opt(launch)} { puts stderr "-inproc runs inside a stim2: no -launch"; exit 2 }
if {$opt(port) == 4612 && !$opt(live) && !$opt(inproc)} {
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
set nopresent 0             ;# stepFrame -nopresent (stim2 0.28.7)
set saw_reload 0            ;# saw the empty stim/script of a reload
set screen_pre [dict create] ;# ess/screen_* to apply with the next frame
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
set gaze_shown ""         ;# the overlay commands last sent (gaze, touch)
set gaze_last ""          ;# the gaze part of them, for counting hides
set touch ""              ;# {press|release x y arrival_us}, "" for none
set last_present 0
set period_ms 16.667        ;# the mirror display's refresh period
set pump_after ""

# stats since the last report
proc stats_reset {} {
    global st
    array set st {drawn 0 dropped 0 missing 0 timeouts 0 errors 0 repaints 0 hides 0 eye 0 noflip 0 lag {} arr {} ready {} draw {}}
    set st(t0) [now_us]
}
stats_reset
array set total {drawn 0 dropped 0 missing 0 errors 0}

# ---------------------------------------------------------------------------
# dserv, by one of two transports:
#   reg  one-shot requests on 4620 (%reg, %match, %get); dserv opens a
#        connection back to our listener and sends one text line per
#        datapoint:  <name> <type> <timestamp> <length> {<value>}
#   ws   dserv's WebSocket (/ws, port -wsport, 2565), the way ess_control.html
#        subscribes: everything on one connection we open, so it works where
#        dserv cannot reach us (NAT, a VPN, a cloud dserv).  JSON requests
#        (subscribe, get, eval) and {"type":"datapoint",...} pushes, turned
#        back into the reg transport's form for on_dp.
# reg is tried first; when dserv cannot register us (it cannot connect back,
# or 4620 is not reachable) the driver moves to ws, or uses it from the start
# with -ws.
# ---------------------------------------------------------------------------
set transport [expr {$opt(ws) ? "ws" : "reg"}]

# One request on 4620, answered within ms (a dserv that cannot connect
# back, or a port that is filtered, must not hang the driver: for %reg a
# timeout is what sends it to the WebSocket).  The wait runs the event loop.
proc ds_req {line {ms 3000}} {
    set t [socket -async $::dserv_ip 4620]
    fconfigure $t -blocking 0 -buffering line -encoding utf-8 -translation lf
    try {
        if {[chan_wait $t writable $ms] ne "ok" || [fconfigure $t -error] ne ""} {
            error "no connection to $::opt(dserv):4620"
        }
        set me [lindex [fconfigure $t -sockname] 0]
        puts $t [string map [list @ME@ "$me $::listen_port"] $line]
        flush $t
        set deadline [expr {[clock milliseconds] + $ms}]
        while {[gets $t r] < 0} {
            if {[eof $t]} { error "dserv closed the request" }
            set left [expr {$deadline - [clock milliseconds]}]
            if {$left <= 0 || [chan_wait $t readable $left] ne "ok"} { error "no answer from dserv in $ms ms" }
        }
        return $r
    } finally {
        close $t
    }
}
set chan_waits 0
proc chan_wait {chan ev ms} {
    set v ::chan_wait_[incr ::chan_waits]
    set $v ""
    fileevent $chan $ev [list set $v ok]
    set id [after $ms [list set $v timeout]]
    vwait $v
    after cancel $id
    catch { fileevent $chan $ev {} }
    set r [set $v]
    unset $v
    return $r
}
proc ds_match {name} {
    if {$::transport eq "ws"} {
        ws_send [dict_to_json [dict create cmd subscribe match $name every 1]]
    } else {
        ds_req "%match @ME@ $name 1"
    }
}

# a line (from a subscription, or a %get reply without its leading status)
proc ds_parse {line} {
    if {![regexp {^(\S+) (\d+) (\d+) (\d+) \{(.*)\}$} $line -> name type ts len data]} { return "" }
    list $name $type $ts $data
}
# the current value as {name type ts data}, "" when there is none
proc ds_get {name} {
    if {$::transport eq "ws"} {
        set d [ws_call [dict_to_json [dict create cmd get name $name]]]
        if {[dict exists $d error] || ![dict exists $d dtype]} { return "" }
        return [ws_dp $d]
    }
    set r [ds_req "%get $name"]
    if {![regexp {^1 (.*)$} $r -> line]} { return "" }
    ds_parse $line
}
# the names matching a glob
proc ds_keys {pattern} {
    if {$::transport eq "ws"} {
        set d [ws_call [dict_to_json [dict create cmd eval script [list dservKeys $pattern]]]]
        if {![dict exists $d result]} { log "dserv's keys: $d"; return {} }
        return [dict get $d result]
    }
    # %getkeys answers "1 key key ..."
    set keys [ds_req "%getkeys"]
    if {[lindex $keys 0] ne "1"} { log "dserv's key list: $keys"; return {} }
    lsearch -all -inline -glob [lrange $keys 1 end] $pattern
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

# ---- the WebSocket client (RFC 6455, client side: text frames out, masked;
#      text, continuation, ping and close in) ----
set ws_sock ""
set ws_buf ""
set ws_frag ""
set ws_reply ""
set ws_joining 0
set ws_pending {}

proc ws_connect {host port} {
    global opt ws_sock ws_buf ws_frag
    if {$opt(wss)} {
        if {[catch {package require tls}]} { error "-wss needs the Tcl tls package, which this dlsh.zip does not have" }
        set s [tls::socket -servername $host $::dserv_ip $port]
    } else {
        set s [socket $::dserv_ip $port]
    }
    fconfigure $s -translation binary -blocking 1 -buffering none
    set key [binary encode base64 [binary format c* [lmap i {0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15} {expr {int(rand() * 256)}}]]]
    puts -nonewline $s "GET /ws HTTP/1.1\r\nHost: $host:$port\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: $key\r\nSec-WebSocket-Version: 13\r\n\r\n"
    flush $s
    set hdr ""
    while {![string match "*\r\n\r\n" $hdr]} {
        set c [read $s 1]
        if {$c eq ""} break
        append hdr $c
        if {[string length $hdr] > 8192} break
    }
    if {![regexp {^HTTP/1\.1 101} $hdr]} {
        close $s
        if {$hdr eq "" || [string index $hdr 0] ne "H"} {
            error "no WebSocket handshake from $host:$port (it may want TLS: -wss, which needs the Tcl tls package)"
        }
        error "WebSocket handshake refused: [lindex [split $hdr \n] 0]"
    }
    set ws_sock $s; set ws_buf ""; set ws_frag ""
    fconfigure $s -blocking 0
    fileevent $s readable ws_read
}

proc ws_frame {op payload} {
    set n [string length $payload]
    set h [binary format c [expr {0x80 | $op}]]
    if {$n < 126} { append h [binary format c [expr {0x80 | $n}]] } \
    elseif {$n < 65536} { append h [binary format cS 0xFE $n] } \
    else { append h [binary format cW 0xFF $n] }
    set mask [lmap i {0 1 2 3} {expr {int(rand() * 256)}}]
    append h [binary format c4 $mask]
    binary scan $payload cu* b
    set i 0
    set m [lmap x $b { set y [expr {$x ^ [lindex $mask [expr {$i & 3}]]}]; incr i; set y }]
    return $h[binary format c* $m]
}
proc ws_send {text} {
    puts -nonewline $::ws_sock [ws_frame 1 [encoding convertto utf-8 $text]]
    flush $::ws_sock
}

proc ws_read {} {
    global ws_sock ws_buf ws_frag
    if {[catch {append ws_buf [read $ws_sock]}] || [eof $ws_sock]} { ws_lost; return }
    while {[string length $ws_buf] >= 2} {
        binary scan $ws_buf cucu b0 b1
        set op [expr {$b0 & 0x0f}]; set fin [expr {$b0 & 0x80}]
        set len [expr {$b1 & 0x7f}]; set off 2
        if {$len == 126} {
            if {[string length $ws_buf] < 4} break
            binary scan $ws_buf @2Su len; set off 4
        } elseif {$len == 127} {
            if {[string length $ws_buf] < 10} break
            binary scan $ws_buf @2Wu len; set off 10
        }
        if {$b1 & 0x80} { incr off 4 }      ;# a server does not mask
        if {[string length $ws_buf] < $off + $len} break
        set payload [string range $ws_buf $off [expr {$off + $len - 1}]]
        set ws_buf [string range $ws_buf [expr {$off + $len}] end]
        switch -- $op {
            0 - 1 {
                append ws_frag $payload
                if {$fin} {
                    set msg [encoding convertfrom utf-8 $ws_frag]
                    set ws_frag ""
                    ws_message $msg
                }
            }
            8 { ws_lost; return }
            9 { catch { puts -nonewline $ws_sock [ws_frame 10 $payload]; flush $ws_sock } }
        }
    }
    if {!$::ws_joining} pump
}

proc ws_message {msg} {
    if {[catch {json_to_dict $msg} d]} return
    if {[dict exists $d type] && [dict get $d type] eq "datapoint"} {
        if {$::ws_joining} { lappend ::ws_pending $d; return }
        set dp [ws_dp $d]
        if {$dp ne ""} {
            if {[catch {on_dp {*}$dp} err]} { log "error handling [lindex $dp 0]: $err\n$::errorInfo" }
        }
    } elseif {[dict exists $d action]} {
        # a subscribe's acknowledgement
    } else {
        set ::ws_reply $d
    }
}

# a request answered by the next message that is not a push or an ack
proc ws_call {json} {
    set ::ws_reply ""
    ws_send $json
    set id [after 5000 {set ::ws_reply {error "no reply from dserv"}}]
    vwait ::ws_reply
    after cancel $id
    return $::ws_reply
}

# a datapoint's JSON as the reg transport's {name type ts data}: binary
# types as base64 of their bytes, as dserv's text lines carry them
proc ws_dp {d} {
    set type [dict get $d dtype]
    set v [expr {[dict exists $d data] ? [dict get $d data] : ""}]
    switch -- $type {
        0  { set data [binary encode base64 [binary format c* $v]] }
        2  { set data [binary encode base64 [binary format f* $v]] }
        3  { set data [binary encode base64 [binary format d* $v]] }
        4  { set data [binary encode base64 [binary format s* $v]] }
        5  { set data [binary encode base64 [binary format i* $v]] }
        16 { set data [binary encode base64 [binary format w* $v]] }
        9  { return "" }
        default { set data $v }
    }
    # strings and JSON as they are; DG and the like arrive in base64 already
    list [dict get $d name] $type [dict get $d timestamp] $data
}

proc ws_lost {} {
    global ws_sock
    catch { close $ws_sock }
    set ws_sock ""
    log "dserv's WebSocket closed; connecting again"
    after 1000 ds_join
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
# dserv's address, looked up once per join: a .local name can cost seconds
# a lookup (an AAAA query no one answers), and the reg transport opens a
# connection per request
set dserv_ip ""
proc resolve_dserv {} {
    global opt
    if {[regexp {^[0-9.]+$} $opt(dserv)]} { return $opt(dserv) }
    foreach port [list 4620 $opt(wsport)] {
        if {![catch {socket $opt(dserv) $port} s]} {
            set ip [lindex [fconfigure $s -peername] 0]
            close $s
            return $ip
        }
    }
    return $opt(dserv)
}

proc ds_join {} {
    global opt rig followed transport
    set ::dserv_ip [resolve_dserv]
    if {$transport eq "reg"} {
        if {[catch {
            if {![info exists ::ds_srv]} {
                set ::ds_srv [socket -server ds_accept 0]
                set ::listen_port [lindex [fconfigure $::ds_srv -sockname] 2]
            }
            set r [ds_req "%reg @ME@ 0"]
            if {[string trim $r] ne "1"} { error "%reg answered \"$r\"" }
        } err]} {
            log "dserv at $opt(dserv) cannot register this mirror ($err): subscribing over its WebSocket"
            set transport ws
        }
    }
    if {$transport eq "ws"} {
        if {[catch { ws_connect $opt(dserv) $opt(wsport) } err]} {
            log "cannot reach dserv's WebSocket at $opt(dserv):$opt(wsport): $err; trying again"
            if {!$opt(ws)} { set transport reg }
            after 2000 ds_join
            return
        }
        set ::ws_joining 1
        set ::ws_pending {}
    }
    set followed {}
    set base {stim/script stim/lib/* stim/bind stim/cmd stim/frame stimdg
        ess/screen_* ess/in_obs ess/status ess/rmt_connected ess/system ess/protocol ess/variant}
    if {$opt(eye) ne "off"} { lappend base $opt(eye) }
    if {$opt(touch) ne "off"} { lappend base ess/touch_press_deg ess/touch_release_deg }
    follow $base
    # current values
    foreach n {ess/system ess/protocol ess/variant ess/rmt_connected ess/status ess/in_obs
        ess/screen_halfx ess/screen_halfy ess/screen_w ess/screen_h ess/screen_refresh_rate ess/screen_bg
        stimdg stim/bind} {
        set d [ds_get $n]
        if {$d ne ""} { note_value {*}$d }
    }
    # the script's libraries: those published with it (configure_stim sends
    # them just before the script, and a file open re-emits them just
    # after it; an older load's stay in dserv, minutes or more away)
    set d [ds_get stim/script]
    set sts [expr {$d ne "" ? [lindex $d 2] : 0}]
    foreach k [ds_keys stim/lib/*] {
        set l [ds_get $k]
        if {$l ne "" && abs($sts - [lindex $l 2]) < 60000000} { note_value {*}$l }
    }
    foreach n $rig(bind) {
        set l [ds_get $n]
        if {$l ne ""} { note_value {*}$l }
    }
    if {$d ne ""} { note_value {*}$d }
    log "joined dserv $opt(dserv) ([expr {$transport eq "ws" ? "WebSocket, port $opt(wsport)" : "callback port $::listen_port"}]): $rig(load), stim2 [expr {$rig(connected) eq "1" ? "connected" : "NOT connected"}],\
 [expr {$rig(script) ne "" ? "script [file tail $rig(path)]" : "no script"}], [llength $rig(bind)] bound streams,\
 [expr {$rig(status) eq "running" ? "running, in_obs $rig(in_obs)" : $rig(status)}]"
    to_waiting [expr {$rig(script) ne "" ? "ready" : "waiting"}] "joined mid-session"
    if {$transport eq "ws"} {
        # what was pushed while joining, in order
        set ::ws_joining 0
        foreach d $::ws_pending {
            set dp [ws_dp $d]
            if {$dp ne ""} { catch {on_dp {*}$dp} }
        }
        set ::ws_pending {}
        pump
    }
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
                set ::saw_reload 1
                to_waiting waiting "the rig is loading [lindex [split $rig(load) /] 0]"
            } elseif {$mode eq "waiting" || $rig(hash) ne $old} {
                to_waiting ready "new script [file tail $rig(path)]"
                # A reload seen from its start leaves the rig in a known
                # state -- this script, its stim_init, nothing else -- so
                # mirror from here, not from the next trial: what the
                # variant's init sends next (planko: setBackground) and the
                # loaders' stimdg go in in order.
                if {$::saw_reload} { set ::saw_reload 0; boundary $ts "load" }
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
    if {$name in {ess/touch_press_deg ess/touch_release_deg}} {
        lassign [string trim $data] x y
        if {[string is double -strict $x] && [string is double -strict $y]} {
            set ::touch [list [expr {$name eq "ess/touch_press_deg" ? "press" : "release"}] $x $y [now_us]]
        }
        return
    }
    if {$name eq $opt(eye)} {
        if {[binary scan [binary decode base64 $data] f2 v] == 1 && [llength $v] == 2} {
            lappend ::gaze [list [now_us] $ts {*}$v]
            incr ::st(eye)
        }
        return
    }
    note_value $name $type $ts $data
    if {[string match ess/screen_* $name]} { note_screen $name; return }
    if {$name in {ess/system ess/protocol ess/variant}} { retitle; publish_status; return }
    if {$name in $rig(bind) && $mode eq "run" && $ts > $start_ts} {
        framelock::plan_dp P $ts $name $type $data
    }
}

# ess/screen_* while running: ESS republishes them after each load and at
# each file open.  Applied with the next frame, and only where this stim2
# differs -- a needless setBackground would hold a redraw and cost a rig
# frame its update.
proc note_screen {name} {
    global rig mode
    if {$mode ne "run"} return
    set g $rig(geom)
    switch -- $name {
        ess/screen_bg {
            if {[dict exists $g bg]} {
                set bg [dict get $g bg]
                dict set ::screen_pre bg "if {\[setBackground\] ne [list $bg]} { setBackground $bg }\n"
            }
        }
        ess/screen_halfx - ess/screen_halfy {
            if {[dict exists $g halfx] && [dict exists $g halfy]} {
                set hx [dict get $g halfx]; set hy [dict get $g halfy]
                dict set ::screen_pre deg "if {\[screen_set HalfScreenDegreeX\] != $hx ||\
 \[screen_set HalfScreenDegreeY\] != $hy} { screen_set HalfScreenDegreeX $hx; screen_set HalfScreenDegreeY $hy }\n"
            }
        }
    }
}
proc screen_pre {} {
    set s [join [dict values $::screen_pre] ""]
    set ::screen_pre [dict create]
    return $s
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
    # the rig's flip on its wall clock; a rig whose swap sync has no flip
    # times (glfinish, a Mac) reports -1: then dserv's receipt of the record
    if {$flip_us <= 0} {
        if {!$::noflip_said} {
            log "the rig's stim2 reports no flip times (its swap sync has none): lag is from dserv's receipt of each frame record"
            set ::noflip_said 1
        }
        set flip_us $ts; incr ::st(noflip)
    }
    lappend ::st(arr) [expr {([now_us] - $flip_us) / 1000.0}]
    lset f 3 $flip_us
    lappend queue [list $f [now_us]]
}

# ---------------------------------------------------------------------------
# the mirror's stim2
# ---------------------------------------------------------------------------
proc stim_connect {} {
    global sock opt
    if {$sock ne ""} { return 1 }
    if {$opt(inproc)} {
        set s inproc
    } elseif {[catch {framelock::connect $opt(host) $opt(port)} s]} { return 0 }
    set v [framelock::send $s {package require stimess}]
    if {[framelock::is_error $v] || [catch {package vcompare $v 1.1} c] || $c < 0} {
        if {$v ne $::stimess_said} { log "stim2 at $opt(host):$opt(port) has no stimess 1.1 (got: $v)"; set ::stimess_said $v }
        catch {close $s}; return 0
    }
    if {[framelock::send $s {info commands stepFrame}] ne "stepFrame"} {
        log "stim2 at $opt(host):$opt(port) has no stepFrame (stim2 0.28.4 or newer)"; catch {close $s}; return 0
    }
    set sock $s
    # a fresh driver starts its stim2 unstepped; then ask whether stepFrame
    # can compute a frame without showing it
    framelock::send $s {stepMode 0}
    set ::stepped 0
    set ::nopresent [framelock::has_nopresent $s]
    set ::period_ms [framelock::send $s {screen_set FrameDuration}]
    if {![string is double -strict $::period_ms] || $::period_ms <= 0} { set ::period_ms 16.667 }
    log "stim2 [framelock::send $s {catch {stimVersion} v; set v}] [expr {$opt(inproc) ? "(this stim2: in-process)" : "at $opt(host):$opt(port)"}]\
 ([expr {$::nopresent ? "computes every rig frame" : "no -nopresent: computes only the newest frame when behind"}]),\
 [framelock::send $s {screen_set WinWidth}]x[framelock::send $s {screen_set WinHeight}]"
    if {$opt(eye) ne "off"} { framelock::send $sock [framelock::gaze_style] }
    framelock::send $sock {overlay set touch -size 1.2 -color {0.25 0.85 1 0.85} -show 0}
    # what the stim2 shows of the overlay, switched in its imgui panel
    # (::stim2_mirror_show(gaze|touch)); checked where the commands run,
    # so a switch needs nothing from the driver
    framelock::send $sock {proc ::mirror_shows {k} { expr {![info exists ::stim2_mirror_show($k)] || $::stim2_mirror_show($k)} }}
    return 1
}
# a request; a lost stim2 drops back to waiting (reconnected at the next try)
proc stim {cmd} {
    global sock
    if {$sock eq "" && ![stim_connect]} { error "no stim2" }
    if {[catch {framelock::send $sock $cmd} r]} {
        catch {close $sock}
        set sock ""; set ::stepped 0
        if {$::launched ne "" && ![alive $::launched]} {
            log "the mirror's stim2 (pid $::launched) has exited"
            exit 0
        }
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
    publish_status
}

# Inside stim2 (-inproc), the driver's state as stim2's ::stim2_mirror_status,
# which `stimMirror` (no args) and the imgui panel show: the rig, mode,
# why, the load, and the last report's lag and counts.
set last_report [dict create]
proc publish_status {} {
    global opt mode why rig sock last_report
    if {!$opt(inproc) || $sock eq ""} return
    set d [dict create dserv $opt(dserv) mode $mode why $why load $rig(load) \
               updated [clock seconds] {*}$last_report]
    catch { framelock::send $sock [list set ::stim2_mirror_status $d] }
}

# The window's title names the rig and its load as it is now: ESS publishes
# system, protocol and variant on every load -- around the new script, not
# in a fixed order with it -- and a variant reload changes the variant
# alone, with no new setup.  Waiting, the waiting screen's title stays.
proc retitle {} {
    global mode opt rig sock
    if {$sock eq "" || $mode ne "run"} return
    catch { stim [list setWindowTitle "mirror of $opt(dserv): $rig(load)"] }
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
    publish_status
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
    # every ready frame: each computed (stepFrame -nopresent), only the
    # newest shown; with a stim2 before 0.28.7, only the newest is computed,
    # after the inputs of all of them
    set inputs {}; set taken {}; set taken_at {}; set taken_in {}
    while {[llength $queue] && [head_ready]} {
        lassign [lindex $queue 0] f at
        lappend taken_at $at
        set queue [lrange $queue 1 end]
        set fi [framelock::plan_frame P $f]
        lappend taken_in $fi
        lappend inputs {*}$fi
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
        set f0 [lindex $taken 0]
        set t0 [expr {[lindex $f0 4] / 1000.0}]
        if {[lindex $f0 8] ne ""} { set t0 [expr {min($t0, [lindex $f0 8] / 1000.0)}] }
        if {[llength $inputs]} { set t0 [expr {min($t0, [lindex [lsort -real -index 0 $inputs] 0 0])}] }
        set t0 [expr {$t0 - 1.0}]
        if {[catch {stim [list stepMode 1 $t0]}]} return
        set clock_ms $t0; set prevflip $t0; set stepped 1
    }
    if {$::nopresent} {
        set fl {}
        foreach tf $taken fi $taken_in {
            set ev_us [lindex $tf 8]
            lappend fl [list $fi [expr {[lindex $tf 4] / 1000.0}] [expr {$ev_us ne "" ? $ev_us / 1000.0 : ""}]]
        }
        set req [framelock::frames_request $fl clock_ms prevflip "[screen_pre][gaze_pre]"]
    } else {
        set inputs [lsort -real -index 0 $inputs]
        set ev_us [lindex $f 8]
        set req [framelock::frame_request $inputs clock_ms $prevflip $flip "[screen_pre][gaze_pre]" "" \
                     [expr {$ev_us ne "" ? $ev_us / 1000.0 : ""}]]
    }
    if {$opt(verbose)} {
        foreach ev $inputs { if {[lindex $ev 1] eq "cmd"} { log "frame $sc cmd [string range [lindex $ev 2] 0 70]" } }
    }
    set t_draw [now_us]
    if {[catch {stim $req} r]} return
    set now [now_us]
    presented $now
    if {$now - $t_draw > 100000} {
        log [format "slow frame request: %.0f ms for %d rig frame(s) ending at swap %d" \
                 [expr {($now - $t_draw) / 1000.0}] [llength $taken] $sc]
    }
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
# Samples are {arrival_us dserv_ts x y}.  The trail is the samples within
# -trail ms of the newest on dserv's clock, so a burst after a delivery
# stall draws as the path it was; the disc stays up through a stall and
# hides only when nothing has arrived for -hold ms.  (dserv, busy between
# trials, has held eyetracking/position back by 250 ms and then sent it all
# at once: timed by arrival with a 100 ms window, the disc blinked.)
proc gaze_cmd_now {} {
    global gaze opt
    if {![llength $gaze]} { return [framelock::gaze_cmd "" ""] }
    lassign [lindex $gaze end] arrived newest x y
    if {[now_us] - $arrived > $opt(hold) * 1000} {
        set gaze {}
        return [framelock::gaze_cmd "" ""]
    }
    set cut [expr {$newest - $opt(trail) * 1000}]
    set i 0
    while {$i < [llength $gaze] && [lindex $gaze $i 1] < $cut} { incr i }
    set gaze [lrange $gaze $i end]
    set trail {}
    for {set k [expr {[llength $gaze] - 1}]} {$k >= 0} {incr k -1} {
        lappend trail [lindex $gaze $k 2] [lindex $gaze $k 3]
    }
    framelock::gaze_cmd [list $x $y] $trail
}
proc gaze_hidden {g} { string match "overlay set gaze -show 0*" $g }

# a touch: a disc where the finger is, from the press to the release (3 s
# at most: ESS publishes touches inside observation periods only), then a
# fainter one at the release point for 300 ms
proc touch_cmd_now {} {
    global touch
    if {$touch eq ""} { return "overlay set touch -show 0" }
    lassign $touch kind x y t
    set age [expr {([now_us] - $t) / 1000.0}]
    if {($kind eq "press" && $age > 3000) || ($kind eq "release" && $age > 300)} {
        set touch ""
        return "overlay set touch -show 0"
    }
    set alpha [expr {$kind eq "press" ? 0.85 : 0.35}]
    return "overlay set touch -show 1 -points {[format %.3f $x] [format %.3f $y]} -color {0.25 0.85 1 $alpha}"
}

# The overlay as it should be now -- gaze and touch, each only if this
# stim2 shows it (::mirror_shows) -- and the gaze part alone
proc overlay_now {} {
    global opt
    set g ""; set s ""
    if {$opt(eye) ne "off"} {
        set g [gaze_cmd_now]
        append s "if {\[::mirror_shows gaze\]} {$g} else {overlay set gaze -show 0; overlay set gaze_trail -show 0}\n"
    }
    if {$opt(touch) ne "off"} {
        append s "if {\[::mirror_shows touch\]} {[touch_cmd_now]} else {overlay set touch -show 0}\n"
    }
    list $s $g
}
proc gaze_pre {} {
    global gaze_shown gaze_last
    lassign [overlay_now] o g
    if {$o eq $gaze_shown} { return "" }
    if {[gaze_hidden $g] && ![gaze_hidden $gaze_last]} { incr ::st(hides) }
    set gaze_shown $o; set gaze_last $g
    return $o
}
proc gaze_repaint {} {
    global gaze_shown gaze_last sock
    if {$sock eq ""} return
    lassign [overlay_now] o g
    if {$o eq "" || $o eq $gaze_shown} return
    # frameRepaint needs a frame drawn with the overlay up; until then the
    # next frame shows it
    if {[catch {stim "${o}frameRepaint"} r]} return
    if {![framelock::is_error $r]} {
        if {[gaze_hidden $g] && ![gaze_hidden $gaze_last]} { incr ::st(hides) }
        set gaze_shown $o; set gaze_last $g
        presented [now_us]
        incr ::st(repaints)
    } elseif {![string match "*empty*" $r]} {
        set gaze_shown $o; set gaze_last $g
    } elseif {!$::stepped} {
        # nothing kept to repaint over yet (waiting, unstepped): draw.
        # Stepped, a redraw would be held and turn the next rig frame's
        # update into a plain redraw; the next frame carries the overlay.
        if {![catch {stim "${o}redraw"}]} { set gaze_shown $o; set gaze_last $g }
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
        set from [expr {$st(noflip) ? "dserv receipt" : "flip"}]
        log [format "%s: %d frames shown, %d computed but not shown (behind), %d not received; lag $from->present ms p50 %s p95 %s max %s\
 (p50: [expr {$st(noflip) ? "receipt" : "flip"}]->arrival %s, arrival->ready %s, ready->present %s); %d command waits ran out; %d errors;\
 gaze %d repaints, %d hides, %.0f samples/s" \
                 $mode $st(drawn) $st(dropped) $st(missing) \
                 [pct $st(lag) 0.5] [pct $st(lag) 0.95] [pct $st(lag) 1.0] \
                 [pct $st(arr) 0.5] [pct $st(ready) 0.5] [pct $st(draw) 0.5] \
                 $st(timeouts) $st(errors) $st(repaints) $st(hides) [expr {$st(eye) / $dt}]]
        # a record cannot arrive before its flip: this host's clock is
        # behind the rig's (chrony still slewing after a boot), and only
        # ready->present, timed here alone, means anything
        if {[llength $st(arr)] && [pct $st(arr) 0.5] < -5.0} {
            log "  NOTE: the clocks disagree by about [format %.0f [expr {-[pct $st(arr) 0.5]}]] ms (this host behind the rig):\
 the lag is not measurable until they agree (chronyc tracking)"
        }
    }
    set ::last_report [dict create shown $st(drawn) behind $st(dropped) errors $st(errors) \
                           lag_from [expr {$st(noflip) ? "receipt" : "flip"}] \
                           lag_p50 [pct $st(lag) 0.5] lag_p95 [pct $st(lag) 0.95] \
                           eye_hz [format %.0f [expr {$st(eye) / $dt}]]]
    publish_status
    stats_reset
    after [expr {int($opt(report) * 1000)}] report
}

# ---------------------------------------------------------------------------
# -launch: the mirror's own stim2, started here and stopped on exit
# ---------------------------------------------------------------------------
set launched ""
set stimess_said ""
set noflip_said 0
proc alive {pid} { expr {![catch {exec kill -0 $pid}]} }
proc launch_stim2 {} {
    global opt launched
    if {$opt(host) ni {localhost 127.0.0.1}} { puts stderr "-launch starts stim2 here: -host must be localhost"; exit 2 }
    set exe $opt(stim2)
    # the stim2 these tools came with: an app bundle's own binary when run
    # from its Resources/tools, else the installed one
    if {$exe eq "" && [regexp {^(.*\.app)/Contents/Resources/tools/} $::mirror_script -> app]} {
        set exe $app/Contents/MacOS/stim2
    }
    if {$exe eq ""} {
        set exe [expr {$::tcl_platform(os) eq "Darwin" ? "/Applications/stim2.app/Contents/MacOS/stim2" : "/usr/local/stim2/stim2"}]
    }
    if {![file executable $exe]} { puts stderr "-launch: no stim2 at $exe (-stim2 PATH)"; exit 2 }
    if {![catch {socket localhost $opt(port)} s]} {
        close $s
        puts stderr "-launch: a stim2 already answers on port $opt(port); stop it, or run without -launch"; exit 2
    }
    set args [list --port-base [expr {$opt(port) - 2}] --title "stim2 mirror"]
    if {$opt(full)} {
        lappend args -F
    } else {
        # the rig's display in its aspect, no wider than 960, unless
        # -size says
        set w 960; set h 540
        if {[regexp {^(\d+)x(\d+)$} $opt(size) -> w h]} {
        } else {
            set rw [lindex [ds_get ess/screen_w] 3]; set rh [lindex [ds_get ess/screen_h] 3]
            if {[string is integer -strict $rw] && [string is integer -strict $rh] && $rw > 0 && $rh > 0} {
                if {$rw > 960} { set w 960; set h [expr {round($rh * 960.0 / $rw)}] } else { set w $rw; set h $rh }
            }
        }
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
    set launched [exec $exe {*}$args >/dev/null 2>/dev/null &]
    dict for {v val} $saved { set ::env($v) $val }
    log "launched stim2 (pid $launched): $exe $args"
}
proc stop_launched {} {
    if {$::launched ne "" && [alive $::launched]} { catch {exec kill $::launched} }
}
if {$opt(launch)} {
    launch_stim2
    # a terminal's Ctrl-C reaches stim2 too (same process group); a plain
    # exit of the driver stops it here
    rename exit _exit
    proc exit {{code 0}} { stop_launched; _exit $code }
}

log "mirror: rig dserv $opt(dserv), stim2 [expr {$opt(inproc) ? "in-process" : "$opt(host):$opt(port)"}], command wait $opt(wait) ms, gaze $opt(eye)"
while {![stim_connect]} {
    if {![info exists warned]} { log "waiting for the mirror's stim2 at $opt(host):$opt(port)"; set warned 1 }
    if {$launched ne "" && ![alive $launched]} { log "the launched stim2 exited before answering"; exit 1 }
    after [expr {$launched ne "" ? 200 : 1000}]
}
show_waiting
ds_join
after [expr {int($opt(report) * 1000)}] report
gaze_tick
vwait forever
