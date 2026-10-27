#!/usr/bin/env tclsh
# test_ack_header.tcl -- exercise the opt-in ACK1 reply header on stim2's
# framed message port (4612).  Runs OUTSIDE stim2, against a live one:
#
#     tclsh tests/test_ack_header.tcl ?host? ?port?
#
# Wire format under test (fixed; ESS parses exactly this):
#   request  : 4-byte big-endian length, payload
#   reply    : 4-byte big-endian length, payload
#   payload prefixes: '@'  -> prepend header     '!'  -> ack after swap
#                     '@!' -> both (in that order)
#   header   : ACK1 <ticks_dequeue> <ticks_done> <swapcount> <flipwall_us>\n
#              followed by the usual result text (possibly empty or
#              "!TCL_ERROR ...").  ticks are StimTicksF ms (%.3f); flipwall_us
#              is CLOCK_REALTIME us of the flip for a swap-ack reply, else 0.
#   stimClock: "<ticks_ms> <wall_us> <span_us>" -- tick clock and
#              CLOCK_REALTIME sampled together (see section (e) below).
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

proc msg_send {sock s} {
    puts -nonewline $sock [binary format I [string length $s]]$s   ;# one write (Nagle)
    flush $sock
    set hdr [read $sock 4]
    if {[string length $hdr] != 4} { error "connection closed" }
    binary scan $hdr I n
    set n [expr {$n & 0xffffffff}]
    return [expr {$n ? [read $sock $n] : ""}]
}

# Split "ACK1 a b c d\n<result>" into a dict; returns {} if no header.
proc parse_ack {reply} {
    # flipwall is whatever swapsyncLastFlipWallUs() returns: -1 when the
    # strategy cannot time flips (e.g. glfinish), so allow a sign
    if {![regexp {^ACK1 ([0-9.]+) ([0-9.]+) ([0-9]+) (-?[0-9]+)\n(.*)$} \
	      $reply -> deq done swap flip result]} {
	return {}
    }
    return [dict create dequeue $deq done $done swapcount $swap \
		flipwall $flip result $result]
}

set sock [socket $host $port]
fconfigure $sock -translation binary -buffering none

# ---- (d) capability probe -------------------------------------------------
set r [msg_send $sock "stimAckHeader"]
check "stimAckHeader returns 1" [expr {$r eq "1"}] "got {$r}"

# ---- (a) plain command: byte-identical to before -------------------------
set r [msg_send $sock "set x 1"]
check "plain 'set x 1' -> '1'" [expr {$r eq "1"}] "got {$r}"

set r [msg_send $sock "!set x 2"]
check "plain '!set x 2' (swap ack, no header) -> '2'" [expr {$r eq "2"}] \
    "got {$r}"

# a '!' command whose script happens to start with '@' is NOT a header
# request: prefix order is '@' then '!'
set r [msg_send $sock "!@set x 3"]
check "'!@...' is not a header request" \
    [expr {[parse_ack $r] eq {} && [string match "!TCL_ERROR*" $r]}] \
    "got {$r}"

# ---- (b) '@' immediate reply ---------------------------------------------
set r [msg_send $sock "@set x 1"]
set a [parse_ack $r]
check "'@set x 1' carries an ACK1 header" [expr {$a ne {}}] "got {$r}"
if {$a ne {}} {
    check "  result is '1'" [expr {[dict get $a result] eq "1"}]
    check "  ticks_done >= ticks_dequeue" \
	[expr {[dict get $a done] >= [dict get $a dequeue]}] \
	"[dict get $a dequeue] -> [dict get $a done]"
    check "  flipwall is 0 for a non-swap reply" \
	[expr {[dict get $a flipwall] == 0}] "got [dict get $a flipwall]"
    check "  ticks printed with 3 decimals" \
	[regexp {^[0-9]+\.[0-9]{3}$} [dict get $a dequeue]]
}
set swap_before [expr {$a ne {} ? [dict get $a swapcount] : -1}]

# header + Tcl error: header first, then the usual "!TCL_ERROR ..." text
set r [msg_send $sock "@no_such_command_xyz"]
set e [parse_ack $r]
check "'@<bad cmd>' -> header + !TCL_ERROR" \
    [expr {$e ne {} && [string match "!TCL_ERROR*" [dict get $e result]]}] \
    "got {$r}"

# header + empty result
set r [msg_send $sock "@set y {}"]
set e [parse_ack $r]
check "'@' with empty result -> header + ''" \
    [expr {$e ne {} && [dict get $e result] eq ""}] "got {$r}"

# ---- (c) '@!' swap-ack reply ----------------------------------------------
# Does this stim2's swapsync strategy know the hardware flip time?
set stats [msg_send $sock "swapStats"]
set have_flipwall [expr {[dict exists $stats flipwall]
			 && [dict get $stats flipwall] > 0}]

set r [msg_send $sock "@!redraw"]
set s [parse_ack $r]
check "'@!redraw' carries an ACK1 header" [expr {$s ne {}}] "got {$r}"
if {$s ne {}} {
    check "  result is ''" [expr {[dict get $s result] eq ""}]
    check "  swapcount advanced past the preceding '@' command" \
	[expr {[dict get $s swapcount] > $swap_before}] \
	"$swap_before -> [dict get $s swapcount]"
    check "  ticks_done >= ticks_dequeue" \
	[expr {[dict get $s done] >= [dict get $s dequeue]}]
    if {$have_flipwall} {
	check "  flipwall nonzero (swapsync reports flips)" \
	    [expr {[dict get $s flipwall] > 0}] "got [dict get $s flipwall]"
	# must be the flip that released this ack, i.e. the latest one
	set stats2 [msg_send $sock "swapStats"]
	check "  flipwall <= swapStats flipwall taken afterwards" \
	    [expr {[dict get $s flipwall] <= [dict get $stats2 flipwall]}] \
	    "[dict get $s flipwall] vs [dict get $stats2 flipwall]"
    } else {
	puts "SKIP flipwall check: swapStats reports no flip time here\
	      (strategy [dict get $stats strategy])"
    }
}

# two '@!' in a row: strictly increasing swapcount, non-decreasing clocks
set r1 [parse_ack [msg_send $sock "@!redraw"]]
set r2 [parse_ack [msg_send $sock "@!redraw"]]
check "consecutive '@!redraw' swapcounts increase" \
    [expr {$r1 ne {} && $r2 ne {} &&
	   [dict get $r2 swapcount] > [dict get $r1 swapcount]}]
check "consecutive '@!redraw' dequeue stamps increase" \
    [expr {$r1 ne {} && $r2 ne {} &&
	   [dict get $r2 dequeue] > [dict get $r1 done]}]

# ---- (e) stimClock: tick clock and wall clock sampled together -----------
# "<ticks_ms> <wall_us> <span_us>": ticks_ms is the ACK1 tick clock (%.3f),
# wall_us CLOCK_REALTIME us (midpoint of two reads around the tick read),
# span_us the difference of those two reads.  Needs stim2 >= 0.28.
proc numeric {s} { return [string is double -strict $s] }

set r [msg_send $sock "stimClock"]
set n [llength $r]
set ok [expr {($n == 2 || $n == 3) && [numeric [lindex $r 0]]
	      && [string is wideinteger -strict [lindex $r 1]]
	      && ($n == 2 || [string is wideinteger -strict [lindex $r 2]])}]
check "stimClock returns 2 or 3 numeric fields" $ok "got {$r}"
if {$ok} {
    check "  ticks_ms printed with 3 decimals" \
	[regexp {^[0-9]+\.[0-9]{3}$} [lindex $r 0]] "got [lindex $r 0]"
    set here [clock microseconds]
    check "  wall_us within 1 s of this tclsh's clock microseconds" \
	[expr {abs([lindex $r 1] - $here) < 1000000}] \
	"stim2 [lindex $r 1] vs here $here"
    if {$n == 3} {
	check "  span_us small (0 <= span < 1000)" \
	    [expr {[lindex $r 2] >= 0 && [lindex $r 2] < 1000}] \
	    "got [lindex $r 2]"
    }
}

# the sample is taken while the '@stimClock' command runs, so its tick
# must sit between that command's own dequeue and done stamps
set r [msg_send $sock "@stimClock"]
set c [parse_ack $r]
check "'@stimClock' carries an ACK1 header" [expr {$c ne {}}] "got {$r}"
if {$c ne {} && [numeric [lindex [dict get $c result] 0]]} {
    set t [lindex [dict get $c result] 0]
    check "  dequeue <= ticks_ms <= done" \
	[expr {$t >= [dict get $c dequeue] && $t <= [dict get $c done]}] \
	"[dict get $c dequeue] <= $t <= [dict get $c done]"
    check "  ticks_ms within 5 ms of the dequeue stamp" \
	[expr {$t - [dict get $c dequeue] < 5.0}] \
	"[dict get $c dequeue] -> $t"
}

close $sock

puts "\n$::passes passed, $::fails failed"
exit [expr {$::fails ? 1 : 0}]
