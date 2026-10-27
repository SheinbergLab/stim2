# test_headless_step.tcl -- headless rendering, step mode and the frame sink
#
#   stim2 --headless -w 320 -h 180 --port-base 5610 -f /abs/path/to/tests/test_headless_step.tcl
#
# Headless, the display is an offscreen framebuffer of exactly -w x -h;
# stepped, the stim clock moves only when stepClock/stepFrame set it, and
# each stepFrame draws one frame.  This draws a red square whose x position
# is a function of StimTimeF alone (5 degrees per 250 ms), steps it through known clock values,
# and checks the square's position in every frame both through frameGrab
# and through the frames the sink wrote to a file -- so a frame's content
# is shown to follow the clock it was stepped to and nothing else.  Prints
# OK/FAIL lines and exits non-zero on any failure.
#
# Runs from the startup file, before stim2's loop: every frame here is
# drawn by stepFrame, synchronously, which is the point.

set ::log {}
proc note { s } { lappend ::log $s }
proc ck { label cond } { note "[expr {$cond ? {OK  } : {FAIL}}] $label" }
proc finish {} {
    puts [join $::log \n]
    set nf [llength [lsearch -all $::log FAIL*]]
    puts "[expr {[llength $::log] - $nf}] ok, $nf FAIL"
    exit [expr {$nf > 0}]
}
proc near { a b {tol 1e-6} } { expr {abs($a - $b) <= $tol} }

# --- the headless display -------------------------------------------------
set W [screen_set WinWidth]; set H [screen_set WinHeight]
ck "headless reported" [dict get [stepMode] headless]
ck "window is exactly -w x -h ($W x $H)" [expr {$W == 320 && $H == 180}]
ck "no desktop scaling (ScaleX [screen_set ScaleX])" [expr {[screen_set ScaleX] == 1.0}]

# --- step mode arguments --------------------------------------------------
ck "stepFrame refused when not stepped" [catch { stepFrame 10 }]
ck "stepClock refused when not stepped" [catch { stepClock 10 }]
set st [stepMode 1 5000]
ck "stepMode 1 5000: enabled, clock at 5000" [expr {[dict get $st enabled] && [near [dict get $st clock] 5000]}]
ck "StimTicksF follows the stepped clock" [near $StimTicksF 5000]
ck "stepClock cannot go back" [catch { stepClock 4999 }]
ck "stepClock 5000 (no move) allowed" [near [stepClock 5000] 5000]
ck "stepFrame flip before frame time refused" [catch { stepFrame 5000 4999 }]

# --- a scene that depends only on the clock: x = StimTimeF / 50 degrees -----
# (a ~/.config/stim2 monitor file may have set any physical size; set one
# in the window's aspect so the degree mapping is known and square)
screen_set ScreenWidthCm 32; screen_set ScreenHeightCm 18; screen_config
set ppd [expr {$W / (2.0 * [screen_set HalfScreenDegreeX])}]
ck "square pixels: x and y degrees match the aspect" \
    [near $ppd [expr {$H / (2.0 * [screen_set HalfScreenDegreeY])}] 1e-3]
glistInit 1
resetObjList
setBackground 20 40 60
set sq [polygon]
scaleObj $sq 2 2; polycolor $sq 1 0 0
proc place { o } {
    lappend ::seen [list $::StimTimeF $::StimTicksF]
    translateObj $o [expr {$::StimTimeF / 50.0}] 0
}
addPreScript $sq [list place $sq]
glistAddObject $sq 0
glistSetCurGroup 0
glistSetDynamic 0 1
glistSetVisible 1
redraw

# the sink: raw RGBA to a file
set fd [file tempfile sinkpath headless_sink.rgba]; close $fd
set s [frameSink open $sinkpath]
ck "frameSink open: size is the window" [expr {[dict get $s open] && [dict get $s width] == $W && [dict get $s height] == $H}]

# red columns of the middle row -> the square's centre column
proc centre_of { w h bytes off } {
    set y [expr {$h / 2}]
    set xs {}
    for {set x 0} {$x < $w} {incr x} {
        binary scan $bytes @[expr {$off + ($y * $w + $x) * 4}]cu4 rgba
        if {$rgba eq {255 0 0 255}} { lappend xs $x }
    }
    if {![llength $xs]} { return -1 }
    expr {([lindex $xs 0] + [lindex $xs end] + 1) / 2.0}
}
proc grabbed { w h bytes sc } { set ::grab [list $w $h $bytes $sc] }

set ::seen {}
set frames {}
# frame 1: the held redraw; newly visible, so StimTime restarts at 0 here
frameGrab grabbed
set r [stepFrame 5000]
ck "frame 1 is the held redraw" [expr {[dict get $r kind] eq "redraw" && [dict get $r swapped]}]
ck "frame 1: frameGrab delivered within stepFrame" [info exists ::grab]
lappend frames [list 0.0 $r]
# frame 2: dynamic, so the group steps; 250 ms on
set r [stepFrame 5250]
ck "frame 2 is a dynamic update" [expr {[dict get $r kind] eq "update"}]
lappend frames [list 250.0 $r]
# frame 3: with a flip time; post-frame the clock stands at the flip
set r [stepFrame 5500 5516.5]
ck "frame 3: clock left at its flip time" [near [dict get $r ticks] 5516.5]
ck "frame 3: stepClock agrees" [near [stepClock] 5516.5]
lappend frames [list 500.0 $r]
# frame 4: no time given -- one FrameDuration on from the flip
set fd_ms [screen_set FrameDuration]
set r [stepFrame]
ck "frame 4: default is one FrameDuration ($fd_ms ms) on" [near [dict get $r ticks] [expr {5516.5 + $fd_ms}] 1e-3]
lappend frames [list [expr {5516.5 + $fd_ms - 5000.0}] $r]

ck "swapcount advanced once per frame" [expr {[dict get [lindex $frames end 1] swapcount] - [dict get [lindex $frames 0 1] swapcount] == 3}]
ck "the draw saw the stepped ticks each frame ([lmap e $::seen {format %.1f [lindex $e 1]}])" \
    [expr {[llength $::seen] == 4 && [near [lindex $::seen 1 1] 5250] && [near [lindex $::seen 2 1] 5500]}]

# the square's position in each frame from the sink file
set s [frameSink close]
ck "frameSink close: 4 frames, status 0" [expr {[dict get $s frames] == 4 && [dict get $s status] == 0 && ![dict get $s failed]}]
set fd [open $sinkpath rb]; set data [read $fd]; close $fd
file delete $sinkpath
set fsz [expr {$W * $H * 4}]
ck "sink file is 4 raw frames ([string length $data] bytes)" [expr {[string length $data] == 4 * $fsz}]
set i 0
foreach f $frames {
    lassign $f stimtime r
    set want [expr {$W / 2.0 + $stimtime / 50.0 * $ppd}]
    set got [centre_of $W $H $data [expr {$i * $fsz}]]
    ck [format "frame %d (StimTimeF %.1f): square centred at x %.1f, want %.1f" [incr i] $stimtime $got $want] \
        [expr {abs($got - $want) <= 0.5}]
}
# frameGrab's copy of frame 1 matches the sink's
lassign $::grab gw gh gbytes
ck "frameGrab frame 1 == sink frame 1" [expr {$gbytes eq [string range $data 0 [expr {$fsz - 1}]]}]
binary scan $data cu4 corner
ck "background is the set colour ($corner)" [expr {$corner eq {20 40 60 255}}]

# --- manual sink: frames are held, written only by "frameSink write n" -------
set fd [file tempfile mpath headless_manual.rgba]; close $fd
set s [frameSink open -manual $mpath]
ck "frameSink open -manual: manual 1" [dict get $s manual]
ck "write before any frame is refused" [catch { frameSink write 1 }]
set r [stepFrame]                        ;# held, not written
ck "a manual sink writes nothing on present" [expr {[dict get [frameSink] frames] == 0}]
frameSink write 0                        ;# dropped
set r [stepFrame]
set s [frameSink write 3]                ;# held for three movie ticks
ck "write 0 then write 3: three frames" [expr {[dict get $s frames] == 3}]
ck "write -1 refused" [catch { frameSink write -1 }]
set s [frameSink close]
set fd [open $mpath rb]; set mdata [read $fd]; close $fd
file delete $mpath
ck "manual file is three identical frames" [expr {[string length $mdata] == 3 * $fsz &&
    [string range $mdata 0 [expr {$fsz - 1}]] eq [string range $mdata $fsz [expr {2 * $fsz - 1}]]}]

# --- back to real time ------------------------------------------------------
set st [stepMode 0]
ck "stepMode 0: disabled" [expr {![dict get $st enabled]}]

finish
