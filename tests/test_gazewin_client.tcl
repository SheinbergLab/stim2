#!/usr/bin/env tclsh
# test_gazewin_client.tcl -- gazewin's live path, against a running stim2.
# Runs OUTSIDE stim2 and stands in for dserv on stim2's datapoint port:
#
#     stim2 --headless -w 320 -h 180 --port-base 5610 &
#     tclsh tests/test_gazewin_client.tcl ?host? ?msgport? ?dsport?
#
# host/ports default to localhost 4612 4611 (port base + 2 and + 1); for
# the line above pass localhost 5612 5611.  Leaves the stim2 running, with
# gazewin reset.
#
# What tests/test_gazewin.tcl cannot reach from inside stim2:
#   - samples arriving as dserv sends them ("name type ts len {base64}" on
#     the datapoint socket) reach the pass without a Tcl binding, and the
#     Tcl side (::dsVals) still sees them;
#   - a STATIC display repaints for a new sample: real frames (the swap
#     count moves) with no redraw asked for, at most one per refresh
#     however fast samples come;
#   - with -hold, a sample going stale changes the display by itself (the
#     loop wakes for the deadline, no input needed);
#   - the latency record fills;
#   - after `gazewin reset`, samples on the old source do nothing and the
#     static display stays still.
#
# Prints OK/FAIL lines and a summary; exits non-zero on any failure.

set host   [expr {$argc > 0 ? [lindex $argv 0] : "localhost"}]
set mport  [expr {$argc > 1 ? [lindex $argv 1] : 4612}]
set dsport [expr {$argc > 2 ? [lindex $argv 2] : 4611}]

set ::fails 0
set ::passes 0
proc check {name cond {detail ""}} {
    if {$cond} { incr ::passes; puts "OK   $name" } \
    else { incr ::fails; puts "FAIL $name  $detail" }
}

proc msg {c} {
    puts -nonewline $::ms [binary format I [string length $c]]$c
    flush $::ms
    set hdr [read $::ms 4]
    if {[string length $hdr] != 4} { error "connection closed" }
    binary scan $hdr I n
    set n [expr {$n & 0xffffffff}]
    return [expr {$n ? [read $::ms $n] : ""}]
}
proc st {} { msg gazewin }
proc swaps {} { dict get [msg swapStats] swapcount }
proc ms_sleep {n} { after $n }

# one datapoint as dserv forwards it: two floats, base64
proc sample {name x y} {
    set b64 [binary encode base64 [binary format f2 [list $x $y]]]
    puts -nonewline $::ds "$name 2 [clock microseconds] 8 {$b64}\n"
    flush $::ds
}

# the pixel at (x, y) deg in the next frame, through frameGrab
proc pixel_at {x y} {
    msg [format {set ::cg {}; frameGrab -rect {%d %d 1 1} {apply {{w h b sc} {binary scan $b cu3 ::cg}}}} \
        [expr {int(floor([msg {screen_set WinWidth}] / 2.0 + $x * $::ppdx))}] \
        [expr {int(floor([msg {screen_set WinHeight}] / 2.0 - $y * $::ppdy))}]]
    for {set i 0} {$i < 100} {incr i} {
        set v [msg {set ::cg}]
        if {$v ne ""} { return $v }
        ms_sleep 10
    }
    return timeout
}

set ::ms [socket $host $mport]
fconfigure $::ms -translation binary -buffering none
set ::ds [socket $host $dsport]
fconfigure $::ds -translation binary -buffering none

set W [msg {screen_set WinWidth}]; set H [msg {screen_set WinHeight}]
set ::ppdx [expr {$W / (2.0 * [msg {screen_set HalfScreenDegreeX}])}]
set ::ppdy [expr {$H / (2.0 * [msg {screen_set HalfScreenDegreeY}])}]
puts "stim2 at $host:$mport (datapoints $dsport), ${W}x$H, swap-sync [dict get [msg swapStats] strategy]"

# the display's real frame rate (a Mac's swap-sync knows no refresh, and
# FrameDuration is only the nominal one): a dynamic group for half a second
msg {gazewin reset; glistInit 1; resetObjList; set ::gw_p [polygon]; glistAddObject $::gw_p 0
     glistSetCurGroup 0; glistSetDynamic 0 1; glistSetVisible 1; redraw}
ms_sleep 100
set s0 [swaps]; set t0 [clock milliseconds]
ms_sleep 500
set ::fps [expr {([swaps] - $s0) * 1000.0 / ([clock milliseconds] - $t0)}]
msg {glistSetDynamic 0 0}
puts "display: [format %.1f $::fps] frames/s"

# a static display: white everywhere
msg {gazewin reset; glistInit 1; resetObjList; setBackground 0 0 0
     set ::gw_full [polygon]; scaleObj $::gw_full 200 200; polycolor $::gw_full 1 1 1
     glistAddObject $::gw_full 0; glistSetCurGroup 0; glistSetVisible 1; redraw}
ms_sleep 100

msg {gazewin set -mode window -radius 2 -edge 0 -color {0 0 0 1} -hold 0 -lost mask}
check "source with no dservhost: not subscribed (0)" [expr {[msg {gazewin source test/gaze}] == 0}]
msg {gazewin on}
ms_sleep 100
check "on, no sample yet: gaze lost, all masked" [expr {[dict get [st] gaze_lost] == 1 && [pixel_at 0 0] eq {0 0 0}}]

# ---- one sample, static display ---------------------------------------------
set r0 [dict get [st] repaints]; set s0 [swaps]
sample test/gaze 3 0
ms_sleep 150
set g [st]
check "a forwarded sample is taken" [expr {[dict get $g samples] >= 1}] "samples [dict get $g samples]"
check "  its value {3 0}" [expr {abs([lindex [dict get $g gaze] 0] - 3) < 1e-5 && abs([lindex [dict get $g gaze] 1]) < 1e-5}] "gaze [dict get $g gaze]"
check "  ::dsVals still has it (Tcl path untouched)" [string match "test/gaze 2 *" [msg {set ::dsVals(test/gaze)}]]
check "static display repainted for it" [expr {[dict get $g repaints] > $r0}] "repaints $r0 -> [dict get $g repaints]"
check "  as a real frame (swap count moved)" [expr {[swaps] > $s0}]
set s1 [swaps]; ms_sleep 200
check "  and then stays still with no new sample" [expr {[swaps] == $s1}] "swaps $s1 -> [swaps]"
check "the window is at (3,0)" [expr {[pixel_at 3 0] eq {255 255 255} && [pixel_at 0 0] eq {0 0 0}}]

msg {gazewin point off}
check "ignores another datapoint name" [apply {{} {
    set n [dict get [st] samples]; sample other/name 0 0; ms_sleep 50
    expr {[dict get [st] samples] == $n}
}}]

# ---- a fast stream: at most one repaint per refresh --------------------------
set r0 [dict get [st] repaints]; set s0 [swaps]; set n0 [dict get [st] samples]
set t0 [clock milliseconds]
for {set i 0} {$i < 100} {incr i} {
    sample test/gaze [expr {-5 + 0.1 * $i}] 0
    ms_sleep 2
}
ms_sleep 100
set dt [expr {[clock milliseconds] - $t0}]
set g [st]
set nr [expr {[dict get $g repaints] - $r0}]
set maxr [expr {int($dt * $::fps / 1000.0 * 1.25) + 3}]
check "100 samples all taken" [expr {[dict get $g samples] - $n0 == 100}] "took [expr {[dict get $g samples] - $n0}]"
check "repaints kept to the frame rate ($nr in ${dt} ms, at most $maxr)" [expr {$nr > 3 && $nr <= $maxr && $nr < 100}]
check "the last sample is what shows (window at 4.9)" [expr {[pixel_at 4.9 0] eq {255 255 255} && [pixel_at 0 0] eq {0 0 0}}]
set lat [dict get $g arrival_to_flip_ms]
check "latency record: samples, p50 [expr {[dict exists $lat p50] ? [dict get $lat p50] : "-"}] ms" \
    [expr {[dict get $lat n] > 0 && [dict get $lat p50] >= 0 && [dict get $lat p50] < 100}]

# ---- the sample going stale ---------------------------------------------------
msg {gazewin set -hold 60}
sample test/gaze 0 0
ms_sleep 25
set r0 [dict get [st] repaints]
check "fresh sample: not lost, window at the centre" [expr {[dict get [st] gaze_lost] == 0}]
ms_sleep 200
set g [st]
check "60 ms with no sample: lost" [expr {[dict get $g gaze_lost] == 1}]
check "  repainted by itself (no input)" [expr {[dict get $g repaints] > $r0}] "repaints $r0 -> [dict get $g repaints]"
check "  the centre is masked now" [expr {[pixel_at 0 0] eq {0 0 0}}]
sample test/gaze 0 0
ms_sleep 30
check "a new sample brings it back" [expr {[pixel_at 0 0] eq {255 255 255}}]

# ---- reset ------------------------------------------------------------------------
msg {gazewin reset}
ms_sleep 100
set n0 [dict get [st] samples]; set s0 [swaps]
sample test/gaze 1 1
ms_sleep 100
check "after reset: samples on the old source are not taken" [expr {[dict get [st] samples] == $n0}]
check "  and the static display does not repaint" [expr {[swaps] == $s0}] "swaps $s0 -> [swaps]"
check "  the scene is untouched" [expr {[pixel_at 0 0] eq {255 255 255} && [pixel_at 6 3] eq {255 255 255}}]

close $::ds; close $::ms
puts "$::passes ok, $::fails FAIL"
exit [expr {$::fails > 0}]
