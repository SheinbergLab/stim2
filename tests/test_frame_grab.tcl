# test_frame_grab.tcl -- frameGrab and afterFrames, in a real stim2
#
#   stim2 -w 640 -h 480 -f /abs/path/to/tests/test_frame_grab.tcl
#
# frameGrab reads the next drawn frame (pre-swap: exactly what is shown)
# and hands "w h pixels swapcount" to a script after the swap; afterFrames
# runs a script once n more swaps have completed. Both keep stim2 drawing
# while they are pending, so they work on a STATIC display, where Tcl's
# after would wait for something else to wake stim2's loop.
#
# This draws known shapes on a static (non-animated) display, checks the
# grabbed pixels (values, top-down row order, -rect, -alpha), checks the
# frame counts, prints OK/FAIL lines and exits non-zero on any failure.
# The steps are chained through the commands under test.
#
# The first frame after stim2 starts is cleared black even when the script
# has already called setBackground: the window's initial RESHAPE_DISPLAY is
# queued ahead of SET_BACKGROUND and draws that frame. The run starts two
# frames in, and checks separately that a mid-run setBackground shows on
# the very next frame (pending frame work must not delay display messages).

set ::log {}
proc note { s } { lappend ::log $s }
proc ck { label cond } { note "[expr {$cond ? {OK  } : {FAIL}}] $label" }
proc finish {} {
    puts [join $::log \n]
    set nf [llength [lsearch -all $::log FAIL*]]
    puts "[expr {[llength $::log] - $nf}] ok, $nf FAIL"
    exit [expr {$nf > 0}]
}

# RGBA of pixel (x, y), top-left origin, from a grab
proc px { w bytes x y } {
    binary scan $bytes @[expr {($y * $w + $x) * 4}]cu4 rgba
    return $rgba
}

# --- argument checks --------------------------------------------------------
ck "afterFrames: none pending at start" [expr {[afterFrames] == 0}]
ck "afterFrames 0 refused" [catch { afterFrames 0 {} }]
ck "afterFrames: n must be an integer" [catch { afterFrames x {} }]
ck "frameGrab: script required" [catch { frameGrab }]
ck "frameGrab: -rect needs 4 integers" [catch { frameGrab -rect {1 2 3} {} }]
ck "frameGrab: unknown option refused" [catch { frameGrab -bogus x {} }]

# --- a static scene: red square above centre, half-transparent green
#     square below it, on a known background --------------------------------
set W [screen_set WinWidth]; set H [screen_set WinHeight]
set ppd [expr {$H / (2.0 * [screen_set HalfScreenDegreeY])}]
glistInit 1
resetObjList
setBackground 20 40 60
set red [polygon]
scaleObj $red 2 2; translateObj $red 0 3; polycolor $red 1 0 0
set grn [polygon]
scaleObj $grn 2 2; translateObj $grn 0 -3; polycolor $grn 0 1 0 0.5
glistAddObject $red 0; glistAddObject $grn 0
glistSetCurGroup 0
glistSetVisible 1
redraw

set ::cx [expr {$W / 2}]
set ::yup [expr {int(round($H / 2.0 - 3 * $ppd))}]   ;# red square's centre row
set ::ydn [expr {int(round($H / 2.0 + 3 * $ppd))}]   ;# green square's

proc step_full { w h bytes sc } {
    ck "full grab: size is the window ($w x $h)" [expr {$w == [screen_set WinWidth] && $h == [screen_set WinHeight]}]
    ck "full grab: w*h*4 bytes" [expr {[string length $bytes] == $w * $h * 4}]
    ck "corner is the background" [expr {[px $w $bytes 2 2] eq {20 40 60 255}}]
    ck "red square, upper half: rows are top-down" [expr {[px $w $bytes $::cx $::yup] eq {255 0 0 255}}]
    lassign [px $w $bytes $::cx $::ydn] r g b a
    ck "green at alpha 0.5 blends over the background ($r $g $b)" [expr {abs($g - 147) <= 2 && abs($r - 10) <= 2 && abs($b - 30) <= 2}]
    ck "alpha forced to 255 by default" [expr {$a == 255}]
    set ::full $bytes; set ::fw $w
    # dlsh: the bytes as a char list, read unsigned with dl_uint
    dl_local p [dl_clist]
    dl_fromString $bytes $p
    set i [expr {($::yup * $w + $::cx) * 4}]
    ck "dl_fromString + dl_uint: same bytes ([dl_length $p])" [expr {[dl_length $p] == $w * $h * 4 &&
        [dl_tcllist [dl_uint [dl_choose $p [dl_series $i [expr {$i + 3}]]]]] eq {255 0 0 255}}]
    frameGrab -alpha step_alpha
}

proc step_alpha { w h bytes sc } {
    lassign [px $w $bytes $::cx $::ydn] r g b a
    ck "-alpha keeps the framebuffer's blended alpha ($a)" [expr {$a < 255}]
    ck "-alpha leaves rgb alone" [expr {[list $r $g $b] eq [lrange [px $::fw $::full $::cx $::ydn] 0 2]}]
    set ::rx [expr {$::cx - 10}]; set ::ry [expr {$::yup - 5}]
    frameGrab -rect [list $::rx $::ry 20 10] step_rect
}

proc step_rect { w h bytes sc } {
    ck "-rect: 20 x 10" [expr {$w == 20 && $h == 10 && [string length $bytes] == 800}]
    set same 1
    for { set y 0 } { $y < 10 } { incr y } {
        for { set x 0 } { $x < 20 } { incr x 7 } {
            if { [px 20 $bytes $x $y] ne [px $::fw $::full [expr {$::rx + $x}] [expr {$::ry + $y}]] } { set same 0 }
        }
    }
    ck "-rect: the same pixels as the full grab, top-left origin" $same
    frameGrab -rect [list [expr {[screen_set WinWidth] - 5}] 0 50 0] step_clip
}

proc step_clip { w h bytes sc } {
    ck "-rect: clipped to the window ($w x $h)" [expr {$w == 5 && $h == [screen_set WinHeight]}]
    # frame counting on a static display: register n = 1, 3, 5 now
    set ::s0 $sc
    set ::ran {}
    foreach n {1 3 5} { afterFrames $n [list ran $n] }
    afterFrames 6 step_counts
}

# runs from afterFrames: record n and the swap count at that moment
proc ran { n } { lappend ::ran $n $::SwapCount }

proc step_counts {} {
    set ok 1; set got {}
    foreach {n sc} $::ran { lappend got $n; if { $sc - $::s0 != $n } { set ok 0 } }
    ck "afterFrames ran in order ($got)" [expr {$got eq {1 3 5}}]
    ck "afterFrames n ran n swaps later, display static" $ok
    ck "afterFrames: none pending after" [expr {[afterFrames] == 0}]
    # a mid-run background change shows on the very next frame
    setBackground 90 10 30
    frameGrab step_bg
}

proc step_bg { w h bytes sc } {
    ck "setBackground shows on the next frame" [expr {[px $w $bytes 2 2] eq {90 10 30 255}}]
    setBackground 20 40 60
    # invisible glist: the grab is the cleared background
    glistSetVisible 0
    redraw
    frameGrab step_blank
}

proc step_blank { w h bytes sc } {
    ck "glist hidden: the frame is the background" [expr {[px $w $bytes $::cx $::yup] eq {20 40 60 255}}]
    finish
}

ck "::SwapCount is linked" [info exists ::SwapCount]
afterFrames 2 { frameGrab step_full }
