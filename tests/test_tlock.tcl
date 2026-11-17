# test_tlock.tcl -- the CRS T-Lock trigger pass (`tlock`, docs/tlock.md),
# inside stim2 (headless or windowed):
#
#     stim2 --headless -w 320 -h 180 -f /abs/path/tests/test_tlock.tcl
#
# Checks the command's argument errors, that the row drawn is the packet
# byte for byte and nothing else on the frame changes, a pulse on one
# frame, on a number of frames and until stopped, the idle packet that
# follows each, toggle mode, a moved row, a packet too wide for the frame, `tlock off`
# leaving the device idle, and `postpass reset`.
# Needs the tlock and polygon stimdlls.  What a Display++ does with the
# packet needs the monitor: docs/tlock.md, "Bringing it up on a rig".
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
proc st { key } { dict get [tlock status] $key }

set W [screen_set WinWidth]; set H [screen_set WinHeight]

# --- the command -------------------------------------------------------------
postpass reset
ck "off to start" [expr {[st on] == 0 && [st state] eq "off"}]
set pl [lsearch -inline -index 1 [postpass list] tlock]
ck "postpass list has it at priority 950, off" [expr {[llength $pl] && [dict get $pl priority] == 950.0 && [dict get $pl on] == 0}]
ck "defaults: row 2, x 0, 248 slots, 508 pixels" [expr {[dict get [tlock set] -row] == 2 &&
    [dict get [tlock set] -x] == 0 && [dict get [tlock set] -slots] == 248 && [st width] == 508}]
ck "unknown subcommand refused" [catch { tlock bogus }]
ck "unknown option refused" [catch { tlock set -bogus 1 }]
ck "an odd -x refused" [catch { tlock set -x 3 }]
ck "249 slots refused" [catch { tlock set -slots 249 }]
ck "a 17-bit mask refused" [catch { tlock set -mask 0x10000 }]
ck "pulse needs bits" [catch { tlock pulse }]
ck "17 bits refused" [catch { tlock pulse 0x10000 }]
ck "-dur 0 refused" [catch { tlock pulse 1 -dur 0 }]
ck "-frames 0 refused" [catch { tlock pulse 1 -frames 0 }]
ck "a pulse starting past the packet refused" [catch { tlock pulse 1 -on 24.8 }]
ck "packet pulse with none armed refused" [catch { tlock packet pulse }]
ck "a refused set changes nothing" [expr {[dict get [tlock set] -slots] == 248}]

# the idle packet's bytes, from the layout (docs/tlock.md), at 84 slots
tlock set -slots 84
set key {69 33 56  40 230 208  19 190 102  119 84 207  52 12 192  233 108 172  41 201 80  183 124 221}
set idle [tlock packet idle]
ck "84 slots: 180 pixels" [expr {[llength $idle] == 180 * 3 && [st width] == 180}]
ck "the unlock key" [expr {[lrange $idle 0 23] eq [concat {*}$key]}]
ck "pixel 8: pairs - 1 in blue" [expr {[lrange $idle 24 26] eq {0 0 85}}]
ck "pixel 9: address 6, command 0, group 2" [expr {[lrange $idle 27 29] eq {6 0 2}}]
ck "pixel 11: address 7, mask 0xFFFF" [expr {[lrange $idle 33 35] eq {7 255 255}}]
ck "pixel 13: address 8, slot 0 low" [expr {[lrange $idle 39 41] eq {8 0 0}}]
ck "pixel 179: address 91, the last slot" [expr {[lrange $idle 537 539] eq {91 0 0}}]

# --- on a frame ---------------------------------------------------------------
# a red screen
glistInit 1; resetObjList; setBackground 0 0 0
set full [polygon]; scaleObj $full 100 100; polycolor $full 1 0 0
glistAddObject $full 0; glistSetCurGroup 0; glistSetVisible 1

set fsz [expr {$W * $H * 4}]
set fd [file tempfile path tlock.rgba]; close $fd
frameSink open -manual $path
set nframes 0
proc keep {} { frameSink write 1; set n $::nframes; incr ::nframes; return $n }
set t 5000
proc step {} { incr ::t 10; stepFrame $::t; keep }

stepMode 1 5000
set f_plain [step]

tlock on
ck "on" [expr {[st on] == 1 && [dict get [lsearch -inline -index 1 [postpass list] tlock] on] == 1}]
set f_idle [step]
ck "a frame drawn: the idle packet sent" [expr {[st sent] eq "idle" && [st frames] == 1}]

# one frame
set serial [tlock pulse 0x0400 -on 0.5 -dur 1]
set p_one [tlock packet]
ck "armed: the next packet is the pulse" [expr {[st armed] == 1 && $p_one eq [tlock packet pulse] && $p_one ne $idle}]
ck "  slot 5 (0.5 ms) carries bit 10, slot 15 does not" [expr {
    [lrange $p_one [expr {(13 + 2*5) * 3}] [expr {(13 + 2*5) * 3 + 2}]] eq {13 4 0} &&
    [lrange $p_one [expr {(13 + 2*14) * 3}] [expr {(13 + 2*14) * 3 + 2}]] eq {22 4 0} &&
    [lrange $p_one [expr {(13 + 2*15) * 3}] [expr {(13 + 2*15) * 3 + 2}]] eq {23 0 0}}]
set f_one [step]
ck "one frame carried it" [expr {[st sent] eq "pulse" && [st armed] == 0 && [st pulse_frames] == 1}]
set f_after [step]
ck "the next frame is idle again" [expr {[st sent] eq "idle"}]

# two frames
tlock pulse 0x0001 -frames 2
set p_two [tlock packet]
set f_two1 [step]; set left1 [st frames_left]
set f_two2 [step]
set f_two3 [step]
ck "-frames 2: one left after the first" [expr {$left1 == 1 && [st armed] == 0}]

# until stopped
tlock pulse 0x0002 -dur 2 -frames always
set p_alw [tlock packet]
set f_alw1 [step]; set f_alw2 [step]; set f_alw3 [step]
ck "-frames always: still armed after three" [expr {[st armed] == 1 && [st frames_left] == -1}]
tlock stop
set f_stopped [step]
ck "stop: disarmed, idle sent" [expr {[st armed] == 0 && [st sent] eq "idle"}]
ck "pulse returns its serial; three so far" [expr {$serial == 1 && [st pulses] == 3}]

# another row, another word between pulses
tlock set -row 0 -x 10 -idle 0x8000
set idle_hi [tlock packet idle]
set f_moved [step]
tlock set -row 2 -x 0 -idle 0

# a packet too wide for the frame is not drawn
set too_wide [expr {$W < 508}]
if { $too_wide } {
    tlock set -slots 248
    set f_wide [step]
    ck "508 pixels on a $W-pixel frame: not drawn, and counted" [expr {[st not_drawn] == 1}]
    tlock set -slots 84
} else {
    note "SKIP a packet too wide for the frame (this one is $W wide)"
}

# toggle mode: the level changes on every frame drawn
ck "toggle is off to start" [expr {[tlock toggle] == 0 && [st toggle] == 0}]
ck "17 toggle bits refused" [catch { tlock toggle 0x10000 }]
ck "toggle returns its bits" [expr {[tlock toggle 0x0800] == 0x0800}]
set p_hi [tlock packet idle]
ck "  the next frame is high: every slot carries the bit" [expr {[lrange $p_hi 39 41] eq {8 8 0} &&
    [lrange $p_hi 537 539] eq {91 8 0}}]
set f_t1 [step]; set hi1 [st toggle_high]
set p_lo [tlock packet idle]
set f_t2 [step]; set hi2 [st toggle_high]
set f_t3 [step]
ck "three frames: high, low, high" [expr {$hi1 == 1 && $hi2 == 0 && [st toggle_high] == 1 && [st toggles] == 3}]
ck "  the low packet is the idle one" [expr {$p_lo eq $idle}]
tlock pulse 0x0001 -dur 1                     ;# a pulse over the toggle: this frame is low
set p_tp [tlock packet]
set f_tp [step]
tlock pulse 0x0001 -dur 1                     ;# and this one high
set p_tph [tlock packet]
set f_tph [step]
ck "a pulse over the high level keeps the toggle bit in every slot" [expr {
    [lrange $p_tph 39 41] eq {8 8 1} && [lrange $p_tph [expr {(13 + 2*10) * 3}] [expr {(13 + 2*10) * 3 + 2}]] eq {18 8 0}}]
tlock toggle 0
set f_toff [step]
ck "toggle 0: off, the level low" [expr {[st toggle] == 0 && [st toggle_high] == 0}]
tlock toggle 0x0800
set f_t4 [step]                               ;# high
tlock off
ck "off with the toggle high: closing" [expr {[st state] in {closing off}}]
set f_tclose [step]
ck "  then off" [expr {[st state] eq "off"}]
tlock toggle 0
tlock on
step

# off after a pulse: one idle packet first
tlock pulse 0x0400
set f_last [step]
tlock off
ck "off after a pulse: closing, the device still to be told" [expr {[st on] == 0 && [st state] in {closing off}}]
set f_closing [step]
ck "then off" [expr {[st state] eq "off"}]
set f_offnow [step]

# reset
tlock on; tlock set -row 5 -slots 84
set f_pre_reset [step]
postpass reset
ck "postpass reset: off, the settings back to their defaults" [expr {[st state] eq "off" &&
    [dict get [tlock set] -row] == 2 && [dict get [tlock set] -slots] == 248}]
set f_reset [step]

frameSink close
set fd [open $path rb]; set d [read $fd]; close $fd
file delete $path
stepMode 0

# npix pixels of row y from x, as a flat R G B list (the sink's rows count
# from the top)
proc row { f y {x 0} {npix 180} } {
    set out {}
    for { set i 0 } { $i < $npix } { incr i } {
        binary scan $::d @[expr {$f * $::fsz + ($y * $::W + $x + $i) * 4}]cu3 p
        lappend out {*}$p
    }
    return $out
}
proc frame { f } { string range $::d [expr {$f * $::fsz}] [expr {($f + 1) * $::fsz - 1}] }
# frame f is the plain frame everywhere but npix pixels of row y from x
proc only_row { f y {x 0} {npix 180} } {
    set a [expr {$f * $::fsz + ($y * $::W + $x) * 4}]
    set b [expr {$::f_plain * $::fsz + ($y * $::W + $x) * 4}]
    set n [expr {$npix * 4}]
    expr {[string replace [frame $f] [expr {$a - $f * $::fsz}] [expr {$a - $f * $::fsz + $n - 1}]] eq
          [string replace [frame $::f_plain] [expr {$b - $::f_plain * $::fsz}] [expr {$b - $::f_plain * $::fsz + $n - 1}]]}
}

ck "sink: $nframes frames" [expr {[string length $d] == $nframes * $fsz}]
ck "the scene is red" [expr {[row $f_plain 2 0 1] eq {255 0 0}}]
ck "on: row 2 is the idle packet, byte for byte" [expr {[row $f_idle 2] eq $idle}]
ck "  and nothing else on the frame changed" [only_row $f_idle 2]
ck "  alpha stays 255" [expr {[binary scan $d @[expr {$f_idle * $fsz + (2 * $W) * 4 + 3}]cu a] && $a == 255}]
ck "pulse: row 2 is the pulse packet" [expr {[row $f_one 2] eq $p_one}]
ck "  and nothing else changed" [only_row $f_one 2]
ck "the frame after a pulse carries the idle packet" [expr {[row $f_after 2] eq $idle}]
ck "-frames 2: two frames carry it, the third is idle" [expr {[row $f_two1 2] eq $p_two &&
    [row $f_two2 2] eq $p_two && [row $f_two3 2] eq $idle}]
ck "-frames always: every frame carries it" [expr {[row $f_alw1 2] eq $p_alw &&
    [row $f_alw2 2] eq $p_alw && [row $f_alw3 2] eq $p_alw}]
ck "stop: the next frame is idle" [expr {[row $f_stopped 2] eq $idle}]
ck "-row 0 -x 10 -idle 0x8000: the packet is there, with the idle word" [expr {[row $f_moved 0 10] eq $idle_hi &&
    [lrange $idle_hi 39 41] eq {8 128 0}}]
ck "  and only there" [only_row $f_moved 0 10]
if { $too_wide } {
    ck "too wide: the frame is byte-identical to the scene" [expr {[frame $f_wide] eq [frame $f_plain]}]
}
ck "toggle: the frames carry high, low, high" [expr {[row $f_t1 2] eq $p_hi && [row $f_t2 2] eq $idle &&
    [row $f_t3 2] eq $p_hi && $p_hi ne $idle}]
ck "  and nothing else changed" [only_row $f_t1 2]
ck "a pulse on a low toggle frame, then on a high one" [expr {[row $f_tp 2] eq $p_tp && [row $f_tph 2] eq $p_tph}]
ck "toggle 0: the next frame is the idle packet" [expr {[row $f_toff 2] eq $idle}]
ck "off with the toggle high: one idle packet first" [expr {[row $f_t4 2] eq $p_hi && [row $f_tclose 2] eq $idle}]
ck "off after a pulse: the next frame carries the idle packet" [expr {[row $f_closing 2] eq $idle}]
ck "  and the one after is byte-identical to the scene" [expr {[frame $f_offnow] eq [frame $f_plain]}]
ck "before the reset: row 5" [expr {[row $f_pre_reset 5] eq $idle}]
ck "after postpass reset: byte-identical to the scene" [expr {[frame $f_reset] eq [frame $f_plain]}]

finish
