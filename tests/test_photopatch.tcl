# test_photopatch.tcl -- the photodiode patch (`photopatch`,
# docs/photopatch.md), inside stim2 (headless or windowed):
#
#     stim2 --headless -w 320 -h 180 --port-base 5610 -f /abs/path/tests/test_photopatch.tcl
#
# Checks the command's argument errors; that the patch is a square of the
# dark or light colour in its corner and nothing else on the frame
# changes; tags mode (a matching swap tag flips it, two tags on one frame
# flip it once, an unmatched one does not); manual mode and `flip`;
# toggle mode; the other corners, an inset, colours; a square too big for
# the frame; frameRepaint under gazewin showing the last level again
# without flipping; `off` leaving the scene untouched; `postpass reset`.
# Needs the photopatch, gazewin and polygon stimdlls.  The records
# (stim/photopatch) need a frame log to dserv: see docs/photopatch.md.
# Prints OK/FAIL lines and exits non-zero on any failure.

set ::log {}
proc note { s } { lappend ::log $s }
proc ck { label cond } { note "[expr {$cond ? {OK  } : {FAIL}}] $label" }
proc finish {} {
    catch { postpass reset }
    puts [join $::log \n]
    set nf [llength [lsearch -all $::log FAIL*]]
    puts "[expr {[llength $::log] - $nf}] ok, $nf FAIL"
    exit [expr {$nf > 0}]
}
proc st { key } { dict get [photopatch status] $key }

set W [screen_set WinWidth]; set H [screen_set WinHeight]

# --- the command -------------------------------------------------------------
postpass reset
ck "off to start" [expr {[st on] == 0}]
set pl [lsearch -inline -index 1 [postpass list] photopatch]
ck "postpass list has it at priority 940, off" [expr {[llength $pl] && [dict get $pl priority] == 940.0 && [dict get $pl on] == 0}]
set s [photopatch set]
ck "defaults: tags, top right, 60 px, no inset, black/white, match *" [expr {
    [dict get $s -mode] eq "tags" && [dict get $s -corner] eq "tr" && [dict get $s -size] == 60 &&
    [dict get $s -inset] == 0 && [dict get $s -dark] eq {0.0 0.0 0.0} &&
    [dict get $s -light] eq {1.0 1.0 1.0} && [dict get $s -match] eq "*"}]
ck "unknown subcommand refused" [catch { photopatch bogus }]
ck "unknown option refused" [catch { photopatch set -bogus 1 }]
ck "a bad corner refused" [catch { photopatch set -corner mid }]
ck "a bad mode refused" [catch { photopatch set -mode blink }]
ck "on with a bad mode refused" [catch { photopatch on blink }]
ck "-size 0 refused" [catch { photopatch set -size 0 }]
ck "a colour past 1 refused" [catch { photopatch set -dark {2 0 0} }]
ck "a two-value colour refused" [catch { photopatch set -light {1 1} }]
ck "flip while off refused" [catch { photopatch flip }]
ck "a refused set changes nothing" [expr {[photopatch set] eq $s && [st on] == 0}]
ck "one grey for a colour" [expr {[dict get [photopatch set -dark 0.25] -dark] eq {0.25 0.25 0.25}}]
photopatch set -dark 0

# --- on a frame ---------------------------------------------------------------
# a red screen
glistInit 1; resetObjList; setBackground 0 0 0
set full [polygon]; scaleObj $full 100 100; polycolor $full 1 0 0
glistAddObject $full 0; glistSetCurGroup 0; glistSetVisible 1

set fsz [expr {$W * $H * 4}]
set fd [file tempfile path photopatch.rgba]; close $fd
frameSink open -manual $path
set nframes 0
proc keep {} { frameSink write 1; set n $::nframes; incr ::nframes; return $n }
set t 5000
proc step {} { incr ::t 10; stepFrame $::t; keep }

stepMode 1 5000
set f_plain [step]

photopatch on
ck "on: tags mode, dark" [expr {[st on] == 1 && [st mode] eq "tags" && [st level] == 0}]
set f_on [step]
ck "the first frame: drawn, a change (the patch appeared)" [expr {[st frames] == 1 && [st changes] == 1}]
set f_untagged [step]
ck "a frame with no tag: no change" [expr {[st level] == 0 && [st changes] == 1 && [st tagged] == 0}]

swapTag {SAMPLE ON}
set f_tag [step]
ck "a tagged frame: light, a change" [expr {[st level] == 1 && [st changes] == 2 && [st tagged] == 1}]
ck "  the flip was seen" [expr {[st last_change_flip_s] > 0}]
swapTag {SAMPLE OFF}; swapTag {CHOICES ON}
set f_two [step]
ck "two tags on one frame: one change" [expr {[st level] == 0 && [st changes] == 3}]

ck "-match takes a list" [expr {[dict get [photopatch set -match {PATTERN* {TARGET SET*}}] -match] eq {PATTERN* {TARGET SET*}}}]
swapTag {SAMPLE ON}
set f_nomatch [step]
ck "an unmatched tag: no change" [expr {[st level] == 0 && [st changes] == 3}]
swapTag {TARGET SET 1 2}
set f_match2 [step]
ck "the second pattern matches" [expr {[st level] == 1 && [st changes] == 4}]
photopatch set -match *

# manual: only flip
photopatch set -mode manual
swapTag {SAMPLE OFF}
set f_man0 [step]
ck "manual: a tag does not change it" [expr {[st level] == 1 && [st changes] == 4}]
ck "flip returns the level it will show" [expr {[photopatch flip] == 0 && [st armed] == 1}]
set f_man1 [step]
ck "  and the next frame shows it" [expr {[st level] == 0 && [st changes] == 5 && [st armed] == 0}]
set f_man2 [step]
ck "  once" [expr {[st level] == 0 && [st changes] == 5}]
photopatch flip 0
step
ck "flip to the level it has: no change" [expr {[st level] == 0 && [st changes] == 5}]
ck "flip 2 refused" [catch { photopatch flip 2 }]

# toggle: every frame
photopatch set -mode toggle
set f_t1 [step]; set l1 [st level]
set f_t2 [step]; set l2 [st level]
set f_t3 [step]
ck "toggle: light, dark, light" [expr {$l1 == 1 && $l2 == 0 && [st level] == 1 && [st changes] == 8}]
photopatch set -mode tags

# another corner, an inset, colours
photopatch set -corner bl -size 10 -inset 5 -light {0 1 0} -dark {0 0 1}
swapTag {}
set f_bl [step]                       ;# level 0: blue
swapTag {}
set f_bl2 [step]                      ;# level 1: green
photopatch set -corner br -size 20 -inset 0
set f_br [step]                       ;# level 1
photopatch set -corner tl
set f_tl [step]
photopatch set -corner tr -size 60 -light 1 -dark 0

# too big
photopatch set -size 4096
set f_big [step]
ck "a square bigger than the frame: not drawn, counted" [expr {[st not_drawn] == 1}]
photopatch set -size 60

# frameRepaint under gazewin: the last frame again, so the last level
gazewin source test/gaze
gazewin set -mode window -radius 2 -edge 0 -color {0 0 0 1} -hold 0 -lost mask
gazewin datapoint test/gaze 1 0 0 "0 0"
gazewin on
photopatch set -mode toggle
set f_g1 [step]; set lg [st level]; set fr [st frames]; set ch [st changes]
frameRepaint; set f_rp1 [keep]
frameRepaint; set f_rp2 [keep]
ck "frameRepaint: drawn again, not a new frame" [expr {[st repeats] == 2 && [st frames] == $fr &&
    [st changes] == $ch && [st level] == $lg}]
set f_g2 [step]
ck "  the next real frame toggles" [expr {[st level] == !$lg}]
gazewin off
photopatch set -mode tags

# off: the scene as it was
set f_preoff [step]
photopatch off
ck "off" [expr {[st on] == 0 && [dict get [lsearch -inline -index 1 [postpass list] photopatch] on] == 0}]
set f_off [step]

# reset
photopatch on toggle; photopatch set -corner bl -size 30
set f_pre_reset [step]
postpass reset
ck "postpass reset: off, the settings back to their defaults" [expr {[st on] == 0 &&
    [photopatch set] eq $s}]
set f_reset [step]

frameSink close
set fd [open $path rb]; set d [read $fd]; close $fd
file delete $path
stepMode 0

# the R G B of pixel (x, y) of frame f (the sink's rows count from the top)
proc px { f x y } {
    binary scan $::d @[expr {$f * $::fsz + ($y * $::W + $x) * 4}]cu3 p
    return $p
}
proc frame { f } { string range $::d [expr {$f * $::fsz}] [expr {($f + 1) * $::fsz - 1}] }
# frame f is frame g everywhere outside the n x n square at (x, y)
proc only_square { f g x y n } {
    set a [frame $f]; set b [frame $g]
    for { set r $y } { $r < $y + $n } { incr r } {
        set o [expr {($r * $::W + $x) * 4}]
        set a [string replace $a $o [expr {$o + $n * 4 - 1}] [string repeat x [expr {$n * 4}]]]
        set b [string replace $b $o [expr {$o + $n * 4 - 1}] [string repeat x [expr {$n * 4}]]]
    }
    expr {$a eq $b}
}
# every pixel of the n x n square at (x, y) is rgb
proc square_is { f x y n rgb } {
    for { set r $y } { $r < $y + $n } { incr r } {
        for { set c $x } { $c < $x + $n } { incr c } {
            if { [px $f $c $r] ne $rgb } { return 0 }
        }
    }
    return 1
}
set X [expr {$W - 60}]

ck "sink: $nframes frames" [expr {[string length $d] == $nframes * $fsz}]
ck "the scene is red" [expr {[px $f_plain $X 0] eq {255 0 0}}]
ck "on: a black square in the top right" [square_is $f_on $X 0 60 {0 0 0}]
ck "  only there" [only_square $f_on $f_plain $X 0 60]
ck "  bordered by the scene" [expr {[px $f_on [expr {$X - 1}] 0] eq {255 0 0} && [px $f_on $X 60] eq {255 0 0}}]
ck "  alpha stays 255" [expr {[binary scan $d @[expr {$f_on * $fsz + $X * 4 + 3}]cu a] && $a == 255}]
ck "untagged: still black" [square_is $f_untagged $X 0 60 {0 0 0}]
ck "tagged: white" [square_is $f_tag $X 0 60 {255 255 255}]
ck "  only there" [only_square $f_tag $f_plain $X 0 60]
ck "two tags: black again" [square_is $f_two $X 0 60 {0 0 0}]
ck "unmatched: black; the second pattern: white" [expr {[px $f_nomatch $X 0] eq {0 0 0} && [px $f_match2 $X 0] eq {255 255 255}}]
ck "manual: white, then black after flip, and stays" [expr {[px $f_man0 $X 0] eq {255 255 255} &&
    [px $f_man1 $X 0] eq {0 0 0} && [px $f_man2 $X 0] eq {0 0 0}}]
ck "toggle: white, black, white" [expr {[px $f_t1 $X 0] eq {255 255 255} &&
    [px $f_t2 $X 0] eq {0 0 0} && [px $f_t3 $X 0] eq {255 255 255}}]
set by [expr {$H - 15}]
ck "bottom left, inset 5, 10 px: blue, then green" [expr {[square_is $f_bl 5 $by 10 {0 0 255}] &&
    [square_is $f_bl2 5 $by 10 {0 255 0}]}]
ck "  only there" [only_square $f_bl $f_plain 5 $by 10]
ck "bottom right, 20 px" [expr {[square_is $f_br [expr {$W - 20}] [expr {$H - 20}] 20 {0 255 0}] &&
    [only_square $f_br $f_plain [expr {$W - 20}] [expr {$H - 20}] 20]}]
ck "top left, 20 px" [expr {[square_is $f_tl 0 0 20 {0 255 0}] && [only_square $f_tl $f_plain 0 0 20]}]
ck "too big: the frame is the scene" [expr {[frame $f_big] eq [frame $f_plain]}]
set gl [expr {$lg ? {255 255 255} : {0 0 0}}]
set gn [expr {$lg ? {0 0 0} : {255 255 255}}]
ck "under gazewin: the patch over its mask" [square_is $f_g1 $X 0 60 $gl]
ck "  frameRepaint shows the same level, twice" [expr {[square_is $f_rp1 $X 0 60 $gl] && [square_is $f_rp2 $X 0 60 $gl]}]
ck "  the next frame the other" [square_is $f_g2 $X 0 60 $gn]
ck "off: the next frame is the scene" [expr {[frame $f_off] eq [frame $f_plain] && [frame $f_preoff] ne [frame $f_plain]}]
ck "before the reset: bottom left, 30 px" [expr {[px $f_pre_reset 0 [expr {$H - 1}]] in {{0 0 0} {255 255 255}}}]
ck "after postpass reset: the scene" [expr {[frame $f_reset] eq [frame $f_plain]}]

finish
