# test_gazewin_step.tcl -- gazewin under the stepped clock: a mirror and a
# replay render
#
#   stim2 --headless -w 320 -h 180 --port-base 5610 -f /abs/path/to/tests/test_gazewin_step.tcl
#
# A mirror's stim2 and a replay's are stepped: frames come only from
# stepFrame, and a mirror moves its gaze between rig frames with
# frameRepaint.  This checks that frameRepaint redraws the pass at the
# newest sample (with an overlay up and without one), and that a sample's
# age for -hold is on the stim clock, which stepFrame moves, not the wall
# clock: a render slower or faster than real time masks where the rig did.
# Prints OK/FAIL lines and exits non-zero on any failure.

set ::log {}
proc note { s } { lappend ::log $s }
proc ck { label cond } { note "[expr {$cond ? {OK  } : {FAIL}}] $label" }
proc finish {} {
    puts [join $::log \n]
    set nf [llength [lsearch -all $::log FAIL*]]
    puts "[expr {[llength $::log] - $nf}] ok, $nf FAIL"
    exit [expr {$nf > 0}]
}

set W [screen_set WinWidth]; set H [screen_set WinHeight]
screen_set ScreenWidthCm 32; screen_set ScreenHeightCm 18; screen_set DistanceToMonitor 57
screen_config
proc deg2px { xd yd } {         ;# degrees -> window pixel, top-left origin
    list [expr {int(round($::W / 2.0 + $xd * $::W / (2.0 * [screen_set HalfScreenDegreeX])))}] \
         [expr {int(round($::H / 2.0 - $yd * $::H / (2.0 * [screen_set HalfScreenDegreeY])))}]
}

# a white screen; the pass shows a 2 deg window and masks the rest black
glistInit 1; resetObjList; setBackground 0 0 0
set full [polygon]; scaleObj $full 100 100; polycolor $full 1 1 1
glistAddObject $full 0; glistSetCurGroup 0; glistSetVisible 1

gazewin source test/gaze
gazewin set -mode window -radius 2 -edge 0 -color {0 0 0 1} -hold 100 -lost mask
gazewin on
proc feed { x y } { gazewin datapoint test/gaze 1 0 0 "$x $y" }

set fsz [expr {$W * $H * 4}]
set fd [file tempfile path gazewin_step.rgba]; close $fd
frameSink open -manual $path
set nframes 0
proc keep {} { frameSink write 1; incr ::nframes }

stepMode 1 5000
feed -6 0
stepFrame 5010; keep                       ;# 0: a frame at (-6,0)
feed 6 0
frameRepaint; keep                         ;# 1: no overlay: the pass again at (6,0)
ck "frameRepaint under gazewin, no overlay: not refused" 1
overlay set dot -points {0 5} -size 1 -color {0 0 1}
stepFrame 5020; keep                       ;# 2: a frame with the overlay, gaze (6,0)
feed -6 0
frameRepaint; keep                         ;# 3: with the overlay: the pass at (-6,0)
overlay set dot -points {0 -5}
frameRepaint; keep                         ;# 4: overlay moved, no new sample: window stays

# the stim clock ages the sample, the wall clock does not
feed 6 0
stepFrame 5030; keep                       ;# 5: sample taken at 5020, frame at 5030
set lost5 [dict get [gazewin] gaze_lost]
after 300                                  ;# 300 ms of wall time pass
stepFrame 5060; keep                       ;# 6: 40 ms on the stim clock: still live
set lost6 [dict get [gazewin] gaze_lost]
stepFrame 5200; keep                       ;# 7: 180 ms on the stim clock: lost, masked
set lost7 [dict get [gazewin] gaze_lost]
overlay clear
frameSink close
set fd [open $path rb]; set d [read $fd]; close $fd
file delete $path
stepMode 0

proc px { f xd yd } {
    lassign [deg2px $xd $yd] x y
    binary scan $::d @[expr {$f * $::fsz + ($y * $::W + $x) * 4}]cu3 p
    return $p
}
proc white { f xd yd } { expr {[px $f $xd $yd] eq {255 255 255}} }
proc black { f xd yd } { expr {[px $f $xd $yd] eq {0 0 0}} }

ck "sink: $nframes frames" [expr {[string length $d] == $nframes * $fsz}]
ck "frame: the window at (-6,0) ([px 0 -6 0])" [expr {[white 0 -6 0] && [black 0 6 0]}]
ck "repaint, no overlay: the window moved to (6,0) ([px 1 6 0])" [expr {[white 1 6 0] && [black 1 -6 0]}]
ck "frame with an overlay: the window at (6,0)" [expr {[white 2 6 0] && [black 2 -6 0]}]
ck "repaint with the overlay: the window moved to (-6,0) ([px 3 -6 0])" [expr {[white 3 -6 0] && [black 3 6 0]}]
ck "  the overlay still on top" [expr {[px 3 0 5] eq {0 0 255}}]
ck "overlay-only repaint: the window stays at (-6,0), the disc moves" [expr {[white 4 -6 0] && [black 4 6 0] &&
    [px 4 0 -5] eq {0 0 255} && [black 4 0 5]}]
ck "10 ms (stim clock) after its sample: live" [expr {!$lost5 && [white 5 6 0]}]
ck "300 ms of wall time but 40 ms of stim clock: still live ([px 6 6 0])" [expr {!$lost6 && [white 6 6 0]}]
ck "180 ms of stim clock (-hold 100): lost, masked ([px 7 6 0])" [expr {$lost7 && [black 7 6 0]}]
gazewin reset
finish
