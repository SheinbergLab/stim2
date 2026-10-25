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
    puts -nonewline $sock [binary format I [string length $s]]
    puts -nonewline $sock $s
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

close $sock

puts "\n$::passes passed, $::fails failed"
exit [expr {$::fails ? 1 : 0}]
