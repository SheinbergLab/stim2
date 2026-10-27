#!/usr/bin/env tclsh
# test_late_draw.tcl -- exercise the `stimLateDraw` command (the opt-in
# draw-late scheduler, see docs/late-draw.md).  Runs OUTSIDE stim2, against
# a live one, over the framed message port:
#
#     tclsh tests/test_late_draw.tcl ?host? ?port?
#
# host/port default to localhost 4612.  To test a build beside a live stim2
# start it with `--port-base 5610` and pass port 5612.
#
# What is checked depends on the swap-sync strategy of the stim2 under test
# (read from `swapStats`):
#   - no flip timestamps (glfinish, e.g. a Mac): enabling MUST fail with an
#     error naming the strategy, and the status dict stays enabled 0;
#   - flip timestamps (oml / presentation, the rig): enabling succeeds, the
#     status dict reflects the floor, a dynamic group animates with the
#     scheduler on without missing frames, and disabling works.
# Argument validation and the status dict's shape are checked everywhere.
#
# The scheduler's actual effect on command latency is a rig measurement
# (docs/late-draw.md), not something this script can prove on a Mac.
#
# The pure scheduling arithmetic (target, raise, decay, floor, cap) is unit
# checked without a display by tests/latedraw_check.cpp:
#     ctest --test-dir build -R latedraw
#
# With the scheduler off, nothing in the main loop changes; to confirm,
# run the existing clients against the same stim2:
#     tclsh tests/test_ack_header.tcl ?host? ?port?
#     tclsh tests/test_frame_log.tcl  ?host? ?port?
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

set ::keys {enabled budget_ms effective_ms period_ms draws late draw_ms_med draw_ms_max}
proc is_status {st} {
    if {[catch {dict size $st}]} { return 0 }
    foreach k $::keys { if {![dict exists $st $k]} { return 0 } }
    return 1
}

# ===========================================================================
set sock [socket $host $port]
fconfigure $sock -translation binary -buffering none

set stats [msg_send $sock "swapStats"]
set strategy [expr {[dict exists $stats strategy] ? [dict get $stats strategy] : "?"}]
set have_flips [expr {[dict exists $stats lastflip] && [dict get $stats lastflip] > 0
		      && [dict get $stats refresh] > 0}]
puts "stim2 at $host:$port, swap-sync strategy $strategy,\
      flip timestamps [expr {$have_flips ? "available" : "unavailable"}]"

# ---- probe / initial state --------------------------------------------------
set st [msg_send $sock "stimLateDraw"]
check "bare stimLateDraw answers a status dict" [is_status $st] "got {$st}"
check "  enabled 0 before any enable" [expr {[dict get $st enabled] == 0}]
check "  draws 0, late 0 before any enable" \
    [expr {[dict get $st draws] == 0 && [dict get $st late] == 0}] "got {$st}"
foreach k $::keys { check "  dict has key $k" [dict exists $st $k] }

# ---- argument validation ------------------------------------------------------
set e [msg_send $sock "stimLateDraw 1 2"]
check "too many args -> !TCL_ERROR" [string match "!TCL_ERROR*" $e] "got {$e}"
set e [msg_send $sock "stimLateDraw abc"]
check "non-numeric budget -> !TCL_ERROR" [string match "!TCL_ERROR*" $e] "got {$e}"
set e [msg_send $sock "stimLateDraw -1"]
check "negative budget -> !TCL_ERROR" [string match "!TCL_ERROR*" $e] "got {$e}"
set st [msg_send $sock "stimLateDraw 0"]
check "stimLateDraw 0 (disable while disabled) answers a status dict" \
    [is_status $st] "got {$st}"
check "  still enabled 0" [expr {[dict get $st enabled] == 0}]

# ---- enable: refused or accepted depending on the strategy ------------------
if {!$have_flips} {
    # (a) a stim2 whose strategy cannot time flips must refuse to enable
    set e [msg_send $sock "stimLateDraw 3"]
    check "enable without flip timestamps -> !TCL_ERROR" \
	[string match "!TCL_ERROR*" $e] "got {$e}"
    check "  error names the strategy ($strategy)" \
	[expr {[string first $strategy $e] >= 0}] "got {$e}"
    check "  error says flip timestamps are required" \
	[expr {[string match -nocase "*flip timestamp*" $e]
	       && [string match -nocase "*required*" $e]}] "got {$e}"
    set st [msg_send $sock "stimLateDraw"]
    check "  status still enabled 0 after the refused enable" \
	[expr {[dict get $st enabled] == 0}] "got {$st}"
    check "  period_ms 0 where no refresh period is known" \
	[expr {[dict get $st period_ms] == 0}] "got [dict get $st period_ms]"

    # an animating display is untouched by a disabled scheduler: a dynamic
    # group runs for a few hundred ms and swaps keep coming
    set before [dict get [msg_send $sock "swapStats"] swapcount]
    set r [msg_send $sock {resetObjList; glistInit 1; glistSetDynamic 0 1
	glistAddObject [nullObj] 0; glistSetVisible 1; redraw}]
    check "dynamic group (one nullObj) set up" [expr {![string match "!TCL_ERROR*" $r]}] "got {$r}"
    after 300
    set after_ [dict get [msg_send $sock "swapStats"] swapcount]
    check "dynamic group still swaps with the scheduler disabled" \
	[expr {$after_ > $before + 5}] "$before -> $after_"
    # a '!' command during animation is still acked after a swap
    set r [msg_send $sock "@!set __ld_ack 1"]
    check "'@!' during animation: header + result" \
	[regexp {^ACK1 [0-9.]+ [0-9.]+ ([0-9]+) -?[0-9]+\n1$} $r -> sc] "got {$r}"
    msg_send $sock {glistSetVisible 0; redraw; resetObjList}
    puts "SKIP enable/run/disable checks: strategy $strategy has no flip timestamps"
} else {
    # (rig) enabling works, the dict reflects it, animation keeps its period
    set st [msg_send $sock "stimLateDraw 3"]
    check "enable with flip timestamps answers a status dict" [is_status $st] "got {$st}"
    check "  enabled 1" [expr {[dict get $st enabled] == 1}] "got {$st}"
    check "  budget_ms 3" [expr {[dict get $st budget_ms] == 3}] "got {$st}"
    check "  effective_ms starts at the floor" \
	[expr {[dict get $st effective_ms] == 3}] "got {$st}"
    check "  period_ms is the refresh period" \
	[expr {abs([dict get $st period_ms] - [dict get $stats refresh]) < 0.01}] \
	"got [dict get $st period_ms] vs refresh [dict get $stats refresh]"

    set missed0 [dict get [msg_send $sock "swapStats"] missed]
    set before [dict get [msg_send $sock "swapStats"] swapcount]
    set r [msg_send $sock {resetObjList; glistInit 1; glistSetDynamic 0 1
	glistAddObject [nullObj] 0; glistSetVisible 1; redraw}]
    check "dynamic group (one nullObj) set up" [expr {![string match "!TCL_ERROR*" $r]}] "got {$r}"
    after 1000
    set stats2 [msg_send $sock "swapStats"]
    set after_ [dict get $stats2 swapcount]
    set period [dict get $stats refresh]
    set expect [expr {int(1000.0 / $period)}]
    check "dynamic group swaps at about the refresh rate under late-draw" \
	[expr {$after_ - $before > 0.8 * $expect}] \
	"$before -> $after_ in 1 s, period $period ms"
    check "  no missed frames during the run" \
	[expr {[dict get $stats2 missed] == $missed0}] \
	"missed $missed0 -> [dict get $stats2 missed]"
    set st [msg_send $sock "stimLateDraw"]
    check "  status counted the governed draws" \
	[expr {[dict get $st draws] > 0.8 * $expect}] "got {$st}"
    check "  draw_ms_med > 0 and <= draw_ms_max" \
	[expr {[dict get $st draw_ms_med] > 0
	       && [dict get $st draw_ms_med] <= [dict get $st draw_ms_max]}] "got {$st}"
    check "  effective_ms within \[floor, period-1\]" \
	[expr {[dict get $st effective_ms] >= 3
	       && [dict get $st effective_ms] <= $period - 1}] "got {$st}"
    # a '!' command during governed animation: acked after a swap that
    # happened after it was dequeued, with that swap's flip time
    set r [msg_send $sock "@!set __ld_ack 1"]
    check "'@!' during governed animation: header + result" \
	[regexp {^ACK1 ([0-9.]+) ([0-9.]+) ([0-9]+) (-?[0-9]+)\n1$} $r -> dq done sc fw] \
	"got {$r}"
    if {[info exists sc]} {
	check "  swapcount advanced past the enable-time count" \
	    [expr {$sc > $after_}] "$after_ -> $sc"
	check "  flipwall is a real flip time" [expr {$fw > 0}] "got $fw"
	check "  done - dequeue is at most about two periods" \
	    [expr {$done - $dq < 2.5 * $period}] "[expr {$done - $dq}] ms"
    }
    msg_send $sock {glistSetVisible 0; redraw; resetObjList}

    set st [msg_send $sock "stimLateDraw 0"]
    check "disable answers enabled 0" [expr {[dict get $st enabled] == 0}] "got {$st}"
    check "  stats persist after disable" [expr {[dict get $st draws] > 0}] "got {$st}"
}

# ---- stim2 still answers -------------------------------------------------------
set r [msg_send $sock "set __late_draw_probe 1"]
check "stim2 still answers plain commands" [expr {$r eq "1"}] "got {$r}"
set r [msg_send $sock "!redraw"]
check "stim2 still answers a swap-acked redraw" [expr {$r eq ""}] "got {$r}"

close $sock
puts "\n$::passes passed, $::fails failed"
exit [expr {$::fails ? 1 : 0}]
