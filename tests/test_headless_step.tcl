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

# --- stepFrame -nopresent: computed (update, draw, scripts), not shown ----
set npath [file join [file dirname [info script]] _hl_nopresent.rgba]
frameSink open $npath
set nseen [llength $::seen]
set clk [stepClock]
set sc0 [dict get [stepFrame [expr {$clk + 10}]] swapcount]
set r [stepFrame [expr {$clk + 20}] [expr {$clk + 25}] -nopresent]
ck "-nopresent: swapped 0" [expr {[dict get $r swapped] == 0}]
ck "-nopresent: swapcount still advances" [expr {[dict get $r swapcount] == $sc0 + 1}]
ck "-nopresent: the draw ran at the frame's time" \
    [expr {[llength $::seen] == $nseen + 2 && [near [lindex $::seen end 1] [expr {$clk + 20}]]}]
ck "-nopresent: clock left at the flip" [near [stepClock] [expr {$clk + 25}]]
ck "-nopresent: nothing to the sink" [expr {[dict get [frameSink] frames] == 1}]
set r [stepFrame [expr {$clk + 30}]]
ck "the next presented frame is shown and written" [expr {[dict get $r swapped] && [dict get [frameSink] frames] == 2}]
ck "-nopresent alone takes the default time" [expr {![catch {stepFrame -nopresent} r] && ![dict get $r swapped]}]
frameSink close
file delete $npath

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

# --- overlay: discs in degrees over the scene, kept across object resets ----
proc px_at { w bytes off x y } {
    binary scan $bytes @[expr {$off + ($y * $w + $x) * 4}]cu4 rgba
    return $rgba
}
proc deg2px { xd yd } {         ;# degrees -> window pixel, top-left origin
    list [expr {int(round($::W / 2.0 + $xd * $::W / (2.0 * [screen_set HalfScreenDegreeX])))}] \
         [expr {int(round($::H / 2.0 - $yd * $::H / (2.0 * [screen_set HalfScreenDegreeY])))}]
}
ck "overlay: empty at start" [expr {[overlay names] eq ""}]
ck "overlay: bad -points refused" [catch { overlay set bad -points {1 2 3} }]
ck "overlay: bad -color refused" [catch { overlay set bad -color {1 0} }]
overlay set gaze -points {-10 5} -size 3 -color {0 0 1}
ck "overlay get: 1 point, size 3" [expr {[dict get [overlay get gaze] points] == 1 && [dict get [overlay get gaze] size] == 3.0}]
# a script resetting its world every trial must not touch the overlay
resetObjList; glistInit 1; setBackground 20 40 60
glistSetVisible 1; redraw
ck "overlay survives resetObjList/glistInit" [expr {[overlay names] eq "gaze"}]
set fd [file tempfile opath headless_overlay.rgba]; close $fd
frameSink open -manual $opath
set r [stepFrame]
set sc_drawn [dict get $r swapcount]
frameSink write 1
lassign [deg2px -10 5] gx gy
# move the disc and repaint the same scene: no new frame, new position
overlay set gaze -points {10 -5}
frameRepaint
frameSink write 1
ck "frameRepaint is not a frame (SwapCount unchanged)" [expr {$SwapCount == $sc_drawn}]
lassign [deg2px 10 -5] hx hy
overlay clear
set r [stepFrame]                        ;# overlay empty: no scene kept
ck "frameRepaint refused when the overlay was empty" [catch { frameRepaint }]
frameSink close
set fd [open $opath rb]; set odata [read $fd]; close $fd
file delete $opath
ck "overlay file: 2 frames" [expr {[string length $odata] == 2 * $fsz}]
ck "frame 1: blue disc at (-10, 5) deg ([px_at $W $odata 0 $gx $gy])" [expr {[px_at $W $odata 0 $gx $gy] eq {0 0 255 255}}]
ck "frame 1: background at (10, -5) deg" [expr {[px_at $W $odata 0 $hx $hy] eq {20 40 60 255}}]
ck "repaint: disc moved to (10, -5) deg" [expr {[px_at $W $odata $fsz $hx $hy] eq {0 0 255 255}}]
ck "repaint: (-10, 5) deg is background again (scene restored)" [expr {[px_at $W $odata $fsz $gx $gy] eq {20 40 60 255}}]

# --- overlay text: a label anchored at a point, over an optional box ---------
# pixels in [x0,x1) x [y0,y1) of frame `off` matching {r g b} within 60
proc count_px { w bytes off x0 y0 x1 y1 rgb } {
    set n 0
    for {set y $y0} {$y < $y1} {incr y} {
        for {set x $x0} {$x < $x1} {incr x} {
            binary scan $bytes @[expr {$off + ($y * $w + $x) * 4}]cu3 p
            set d 0
            foreach a $p b $rgb { set d [expr {max($d, abs($a - $b))}] }
            if {$d <= 60} { incr n }
        }
    }
    return $n
}
ck "overlay: bad -anchor refused" [catch { overlay set bad -text x -anchor middle }]
ck "overlay: bad -pos refused" [catch { overlay set bad -text x -pos {1} }]
ck "overlay: bad -bg refused" [catch { overlay set bad -text x -bg {1 0} }]
catch { overlay delete bad }
set hx [screen_set HalfScreenDegreeX]; set hy [screen_set HalfScreenDegreeY]
set lsize [expr {0.25 * $hy}]            ;# a line an eighth of the height
overlay set lbl -text "HI\nthere" -size $lsize -color {1 1 0} -anchor nw \
    -pos [list [expr {-$hx + 0.5}] [expr {$hy - 0.5}]]
set g [overlay get lbl]
ck "overlay get: text, anchor, pos" [expr {[dict get $g text] eq "HI\nthere" && [dict get $g anchor] eq "nw"
    && [near [lindex [dict get $g pos] 0] [expr {-$hx + 0.5}] 1e-4]}]
set fd [file tempfile tpath headless_text.rgba]; close $fd
frameSink open -manual $tpath
stepFrame; frameSink write 1
overlay set lbl -bg {1 0 0 1}
frameRepaint; frameSink write 1
overlay set lbl -text {}
frameRepaint; frameSink write 1
frameSink close
set fd [open $tpath rb]; set tdata [read $fd]; close $fd
file delete $tpath
set hw [expr {$W / 2}]; set hh [expr {$H / 2}]
set ytl [count_px $W $tdata 0 0 0 $hw $hh {255 255 0}]
ck "label: yellow text in the top-left quarter ($ytl px)" [expr {$ytl > 20}]
ck "label: none in the bottom-right quarter" [expr {[count_px $W $tdata 0 $hw $hh $W $H {255 255 0}] == 0}]
set red [count_px $W $tdata $fsz 0 0 $hw $hh {255 0 0}]
ck "label -bg: a red box behind it ($red px)" [expr {$red > 4 * $ytl}]
ck "label -bg: the text stays over the box" [expr {[count_px $W $tdata $fsz 0 0 $hw $hh {255 255 0}] > 20}]
ck "-text {}: the label is gone" [expr {[count_px $W $tdata [expr {2 * $fsz}] 0 0 $hw $hh {255 255 0}] == 0
    && [count_px $W $tdata [expr {2 * $fsz}] 0 0 $hw $hh {255 0 0}] == 0}]
overlay clear

# --- overlay lines: a polyline, each pixel once; a faded trail ---------------
ck "overlay: bad -mode refused" [catch { overlay set bad -mode dots }]
catch { overlay delete bad }
# (-10,0) -> (10,0) -> (0,6) -> (0,-6): the last segment crosses the first at 0,0
overlay set cross -mode line -size 1 -color {0 1 0 0.5} -points {-10 0 10 0 0 6 0 -6}
# newest first: full at (-12,-8), nothing by (12,-8)
overlay set trail -mode line -fade 1 -size 1 -color {1 1 1 1} -points {-12 -8 12 -8}
set g [overlay get trail]
ck "overlay get: mode line, fade 1" [expr {[dict get $g mode] eq "line" && [dict get $g fade] == 1}]
ck "overlay get: a new item is discs" [expr {[dict get [overlay get cross] mode] eq "line"
    && ![catch {overlay set dots -points {0 0}}] && [dict get [overlay get dots] mode] eq "discs"}]
overlay delete dots
set fd [file tempfile lpath headless_line.rgba]; close $fd
frameSink open -manual $lpath
stepFrame; frameSink write 1
frameSink close
set fd [open $lpath rb]; set ldata [read $fd]; close $fd
file delete $lpath
lassign [deg2px 0 0] cx cy
lassign [deg2px -5 0] ax ay
set at_cross [px_at $W $ldata 0 $cx $cy]
set on_line [px_at $W $ldata 0 $ax $ay]
ck "line: drawn ($on_line at (-5, 0) deg; background is 20 40 60)" [expr {[lindex $on_line 1] > 100}]
set same 1
foreach a $at_cross b $on_line { if {abs($a - $b) > 3} { set same 0 } }
ck "line: no darker where it crosses itself ($at_cross vs $on_line)" $same
lassign [deg2px -11 -8] f0x f0y
lassign [deg2px 0 -8] f1x f1y
lassign [deg2px 11 -8] f2x f2y
set r0 [lindex [px_at $W $ldata 0 $f0x $f0y] 0]
set r1 [lindex [px_at $W $ldata 0 $f1x $f1y] 0]
set r2 [lindex [px_at $W $ldata 0 $f2x $f2y] 0]
ck "line -fade: bright at its first point, dim at its last ($r0 > $r1 > $r2)" [expr {$r0 > $r1 + 40 && $r1 > $r2 + 40}]
overlay clear

# --- back to real time ------------------------------------------------------
set st [stepMode 0]
ck "stepMode 0: disabled" [expr {![dict get $st enabled]}]

finish
