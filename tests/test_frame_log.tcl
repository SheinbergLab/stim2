#!/usr/bin/env tclsh
# test_frame_log.tcl -- exercise the per-swap frame record (`stimFrameLog`,
# dserv datapoint "stim/frame").  Runs OUTSIDE stim2, against a live one,
# and stands in for dserv: it listens on a free TCP port, speaks dserv's
# fixed 128-byte '>' datapoint frame, and checks what stim2 streams to it.
#
#     tclsh tests/test_frame_log.tcl ?host? ?port?
#
# host/port are stim2's framed message port (default localhost 4612).  The
# fake dserv always listens on 127.0.0.1, so stim2 must run on this machine.
#
# Wire format under test (fixed; dserv's DPOINT_BINARY_MSG_CHAR parser and
# qpcs::dsSocketSendBytes spell exactly this; integers little-endian):
#   byte 0      '>'          bytes 1-2    u16 varlen
#   varname     "stim/frame" then         u64 timestamp (0 = dserv stamps)
#   u32 datatype (16 = DSERV_INT64)       u32 datalen (64)
#   8 x int64, zero padding to 128 bytes:
#     frame swapcount flip_us ticks_us missed cmds_applied ds_dispatched
#     eval_us
#
# Prints OK/FAIL lines and a summary; exits non-zero on any failure.

set host [expr {$argc > 0 ? [lindex $argv 0] : "localhost"}]
set port [expr {$argc > 1 ? [lindex $argv 1] : 4612}]

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

# ---- stim2 message port ----------------------------------------------------
set ::ncmds 0      ;# commands this script has had stim2 evaluate
proc msg_send {sock s} {
    puts -nonewline $sock [binary format I [string length $s]]$s   ;# one write (Nagle)
    flush $sock
    incr ::ncmds
    set hdr [read $sock 4]
    if {[string length $hdr] != 4} { error "connection closed" }
    binary scan $hdr I n
    set n [expr {$n & 0xffffffff}]
    return [expr {$n ? [read $sock $n] : ""}]
}

# ---- fake dserv: '>' frame listener ------------------------------------------
set ::fields {frame swapcount flip_us ticks_us missed cmds_applied ds_dispatched eval_us}
array set ::conn {}      ;# listener -> accepted channel ("" until accept)
set ::frames {}          ;# decoded records, in arrival order
set ::badframes 0
set ::eof 0

proc listener {} {
    set srv [socket -server [list on_accept] -myaddr 127.0.0.1 0]
    set ::conn([listener_port $srv]) ""
    return $srv
}
proc listener_port {srv} { lindex [fconfigure $srv -sockname] 2 }

proc on_accept {chan addr p} {
    # a listener may be connected to more than once (restart tests); keep
    # the newest channel per listening port
    set ::conn([lindex [fconfigure $chan -sockname] 2]) $chan
    set ::last_accept $chan
    fconfigure $chan -translation binary -blocking 0 -buffering none
    fileevent $chan readable [list on_readable $chan]
}

proc on_readable {chan} {
    if {[eof $chan]} {
	fileevent $chan readable {}
	close $chan
	set ::eof 1
	return
    }
    append ::buf($chan) [read $chan]
    while {[string length $::buf($chan)] >= 128} {
	set f [string range $::buf($chan) 0 127]
	set ::buf($chan) [string range $::buf($chan) 128 end]
	decode_frame $f
    }
}

# Exactly dserv's parse: lead byte, u16 varlen, name, u64 ts, u32 type,
# u32 len, data.
proc decode_frame {f} {
    binary scan $f cusu lead varlen
    if {$lead != 0x3E} { incr ::badframes; return }
    binary scan $f @3a${varlen} varname
    set off [expr {3 + $varlen}]
    binary scan $f @${off}wuiuiu ts type len
    incr off 16
    binary scan $f @${off}a${len} data
    set rec [dict create varname $varname ts $ts type $type len $len]
    if {$varname eq "stim/frame" && $type == 16 && $len == 64} {
	binary scan $data w8 vals
	foreach k $::fields v $vals { dict set rec $k $v }
	lappend ::frames $rec
    } else {
	incr ::badframes
	lappend ::frames $rec
    }
}

# run the event loop until cond is true or timeout_ms elapses
proc wait_until {cond timeout_ms} {
    set ::deadline 0
    set id [after $timeout_ms {set ::deadline 1}]
    while {!$::deadline && ![uplevel 1 [list expr $cond]]} { vwait ::tick }
    after cancel $id
    return [uplevel 1 [list expr $cond]]
}
# keep vwait returning regularly so wait_until can poll its condition
proc ticker {} { set ::tick 1; after 10 ticker }
ticker

# ===========================================================================
set sock [socket $host $port]
fconfigure $sock -translation binary -buffering none

# ---- probe / initial state --------------------------------------------------
set st [msg_send $sock "stimFrameLog"]
check "bare stimFrameLog answers a status dict" \
    [expr {![catch {dict size $st}] && [dict exists $st enabled]}] "got {$st}"
check "  enabled 0 before any start" [expr {[dict get $st enabled] == 0}]
check "  connected 0 before any start" [expr {[dict get $st connected] == 0}]
foreach k {enabled host sent dropped connected} {
    check "  dict has key $k" [dict exists $st $k]
}

set r [catch {msg_send $sock "stimFrameLog a b c d"} e]
check "too many args -> !TCL_ERROR" [string match "!TCL_ERROR*" $e] "got {$e}"
set e [msg_send $sock "stimFrameLog {} 1"]
check "empty host -> !TCL_ERROR" [string match "!TCL_ERROR*" $e] "got {$e}"
set e [msg_send $sock "stimFrameLog 127.0.0.1:99999 1"]
check "bad port -> !TCL_ERROR" [string match "!TCL_ERROR*" $e] "got {$e}"
set st [msg_send $sock "stimFrameLog"]
check "  still disabled after rejected starts" [expr {[dict get $st enabled] == 0}]

# ---- start toward the fake dserv ------------------------------------------------
set srv [listener]
set dport [listener_port $srv]
set spec 127.0.0.1:$dport

set st [msg_send $sock "stimFrameLog $spec 1"]
check "start returns a dict with enabled 1" [expr {[dict get $st enabled] == 1}] \
    "got {$st}"
check "  host echoed as given" [expr {[dict get $st host] eq $spec}] \
    "got [dict get $st host]"
check "writer connects to the fake dserv" \
    [wait_until {$::conn($dport) ne ""} 3000]

set st [msg_send $sock "stimFrameLog"]
check "status shows connected 1" [expr {[dict get $st connected] == 1}] "got {$st}"

# ---- trigger swaps ----------------------------------------------------------
# A few '!redraw' swap-acked commands: each one completes a real swap.
set nswaps 8
for {set i 0} {$i < $nswaps} {incr i} { msg_send $sock "!redraw" }
check "received >= $nswaps frames" \
    [wait_until {[llength $::frames] >= $nswaps} 3000] \
    "got [llength $::frames]"

set cmds_so_far $::ncmds

# ---- the records ------------------------------------------------------------
check "no malformed frames" [expr {$::badframes == 0}] "$::badframes bad"
set f0 [lindex $::frames 0]
check "varname is stim/frame" [expr {[dict get $f0 varname] eq "stim/frame"}]
check "datatype is 16 (DSERV_INT64)" [expr {[dict get $f0 type] == 16}]
check "datalen is 64" [expr {[dict get $f0 len] == 64}]
check "timestamp is 0 (dserv stamps on receipt)" [expr {[dict get $f0 ts] == 0}]
check "first frame number is 1" [expr {[dict get $f0 frame] == 1}]

set ok_frame 1; set ok_swap 1; set ok_ticks 1; set ok_flip 1; set ok_missed 1
set ok_cmds 1; set ok_ds 1; set ok_eval 1; set ok_eval_after 1
set prev {}
foreach f $::frames {
    set flip [dict get $f flip_us]
    # -1 (strategy cannot time flips, e.g. glfinish) or epoch us roughly
    # 2020..2040
    if {!($flip == -1 || ($flip > 1577836800000000 && $flip < 2208988800000000))} {
	set ok_flip 0
    }
    if {[dict get $f missed] < 0} { set ok_missed 0 }
    if {[dict get $f ds_dispatched] < 0} { set ok_ds 0 }
    # eval_us: the instant the frame was computed at, before its flip
    if {[dict get $f eval_us] <= 0 || [dict get $f eval_us] > [dict get $f ticks_us]} { set ok_eval 0 }
    if {$prev ne {}} {
	if {[dict get $f eval_us] < [dict get $prev ticks_us]} { set ok_eval_after 0 }
	if {[dict get $f frame] != [dict get $prev frame] + 1} { set ok_frame 0 }
	if {[dict get $f swapcount] <= [dict get $prev swapcount]} { set ok_swap 0 }
	if {[dict get $f ticks_us] <= [dict get $prev ticks_us]} { set ok_ticks 0 }
	if {[dict get $f cmds_applied] < [dict get $prev cmds_applied]} { set ok_cmds 0 }
	if {[dict get $f missed] < [dict get $prev missed]} { set ok_missed 0 }
    }
    set prev $f
}
check "frame increments by exactly 1" $ok_frame
check "swapcount strictly increasing" $ok_swap
check "ticks_us strictly increasing" $ok_ticks
check "flip_us is -1 or a plausible epoch microsecond" $ok_flip
check "missed is non-negative and non-decreasing" $ok_missed
check "cmds_applied non-decreasing" $ok_cmds
check "ds_dispatched non-negative" $ok_ds
check "eval_us positive and not after the frame's own ticks_us" $ok_eval
check "eval_us not before the previous frame's ticks_us" $ok_eval_after

set last [lindex $::frames end]
# cmds_applied counts since process start, so it is at least what this
# script sent before the last swap (minus the '!redraw' that caused the
# frame itself may or may not be counted yet, hence -1)
check "cmds_applied >= commands sent before the last swap" \
    [expr {[dict get $last cmds_applied] >= $cmds_so_far - 1}] \
    "[dict get $last cmds_applied] vs $cmds_so_far"

# ticks_us is microseconds of the free-running stim clock: compare with
# stimTicks (ms) read now, which must be later and within a few seconds
set ticks_ms [msg_send $sock "set StimTicksF"]
if {[string is double -strict $ticks_ms]} {
    set d [expr {$ticks_ms * 1000.0 - [dict get $last ticks_us]}]
    check "ticks_us is in microseconds (within 10 s before now)" \
	[expr {$d >= 0 && $d < 10000000}] "delta $d us"
} else {
    puts "SKIP ticks_us unit check: StimTicksF unavailable ({$ticks_ms})"
}

# the swap-acked '!redraw' round trips serialize with the swaps, so the
# frame for the last one must have the current SwapCount
set stats [msg_send $sock "swapStats"]
check "last frame's swapcount == swapStats swapcount" \
    [expr {[dict get $last swapcount] == [dict get $stats swapcount]}] \
    "[dict get $last swapcount] vs [dict get $stats swapcount]"
if {[dict get $stats flipwall] > 0} {
    check "last frame's flip_us == swapStats flipwall" \
	[expr {[dict get $last flip_us] == [dict get $stats flipwall]}] \
	"[dict get $last flip_us] vs [dict get $stats flipwall]"
} else {
    check "flip_us is -1 where swapStats has no flip time" \
	[expr {[dict get $last flip_us] == -1}] "got [dict get $last flip_us]"
}

# ---- same host again: no restart, counts continue --------------------------------
set before [llength $::frames]
set st [msg_send $sock "stimFrameLog $spec 1"]
msg_send $sock "!redraw"
wait_until {[llength $::frames] > $before} 2000
check "re-enabling the same host keeps numbering (no restart)" \
    [expr {[dict get [lindex $::frames end] frame] == [llength $::frames]}] \
    "frame [dict get [lindex $::frames end] frame] of [llength $::frames]"

# ---- stop ---------------------------------------------------------------------
set nbefore_stop [llength $::frames]
set st [msg_send $sock "stimFrameLog $spec 0"]
check "stop returns enabled 0" [expr {[dict get $st enabled] == 0}] "got {$st}"
check "  connected 0 after stop" [expr {[dict get $st connected] == 0}]
check "writer closed its socket (EOF at the fake dserv)" [wait_until {$::eof} 2000]
check "  sent == frames received" \
    [expr {[dict get $st sent] == [llength $::frames]}] \
    "sent [dict get $st sent], received [llength $::frames]"
check "  dropped is 0" [expr {[dict get $st dropped] == 0}] "got [dict get $st dropped]"
check "  host still reported after stop" [expr {[dict get $st host] eq $spec}]

# swaps while disabled produce nothing
msg_send $sock "!redraw"
msg_send $sock "!redraw"
after 200; update
check "no frames while disabled" [expr {[llength $::frames] == $nbefore_stop}]
set st [msg_send $sock "stimFrameLog"]
check "  counts persist in status after stop" \
    [expr {[dict get $st sent] == $nbefore_stop}] "got {$st}"

# ---- dead host: render thread unaffected, drops counted -------------------------
# a port nobody listens on (take one and close it)
set tmp [listener]; set deadport [listener_port $tmp]; close $tmp
set st [msg_send $sock "stimFrameLog 127.0.0.1:$deadport 1"]
check "start toward a dead host returns enabled 1" [expr {[dict get $st enabled] == 1}]
set t0 [clock milliseconds]
for {set i 0} {$i < 5} {incr i} { msg_send $sock "!redraw" }
set dt [expr {[clock milliseconds] - $t0}]
check "5 swap-acked redraws still answer promptly with a dead sink" \
    [expr {$dt < 2000}] "${dt} ms"
after 100
set st [msg_send $sock "stimFrameLog"]
check "  connected 0" [expr {[dict get $st connected] == 0}]
check "  dropped counts the swaps (>= 5)" [expr {[dict get $st dropped] >= 5}] \
    "got [dict get $st dropped]"
check "  sent 0" [expr {[dict get $st sent] == 0}]

# ---- restart on a different host while running -------------------------------------
set srv2 [listener]
set dport2 [listener_port $srv2]
set ::frames {}
set st [msg_send $sock "stimFrameLog 127.0.0.1:$dport2 1"]
check "switching host restarts the writer (counts reset)" \
    [expr {[dict get $st enabled] == 1 && [dict get $st dropped] == 0
	   && [dict get $st host] eq "127.0.0.1:$dport2"}] "got {$st}"
check "  new connection arrives" [wait_until {$::conn($dport2) ne ""} 3000]
msg_send $sock "!redraw"
msg_send $sock "!redraw"
check "  frames flow again, numbered from 1" \
    [expr {[wait_until {[llength $::frames] >= 2} 3000]
	   && [dict get [lindex $::frames 0] frame] == 1}] \
    "got [llength $::frames] frames"
set st [msg_send $sock "stimFrameLog 127.0.0.1:$dport2 0"]
check "final stop: enabled 0" [expr {[dict get $st enabled] == 0}]

close $sock
puts "\n$::passes passed, $::fails failed"
exit [expr {$::fails ? 1 : 0}]
