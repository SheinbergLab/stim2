# test_shaderpass.tcl -- shaderpass (a fragment shader as a post pass) and
# stim2's post-pass chain with more than one pass
#
#   stim2 --headless -w 320 -h 180 --port-base 5610 -f /abs/path/to/tests/test_shaderpass.tcl
#
# Stepped, into a manual frame sink, so every frame is exact: a channel
# swizzle given inline, the shipped colour matrix and noise shaders, a
# uniform change shown by frameRepaint, shaders that do not build, and the
# chain -- gazewin and a shaderpass together in either order, on a frame
# and on a repaint (where the second pass is staged again over the first's
# new output) -- and `postpass reset` deleting the passes.
# Needs the shaderpass and gazewin stimdlls.  Prints OK/FAIL lines and
# exits non-zero on any failure.

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

set W [screen_set WinWidth]; set H [screen_set WinHeight]
screen_set ScreenWidthCm 32; screen_set ScreenHeightCm 18; screen_set DistanceToMonitor 57
screen_config
proc deg2px { xd yd } {         ;# degrees -> window pixel, top-left origin
    list [expr {int(round($::W / 2.0 + $xd * $::W / (2.0 * [screen_set HalfScreenDegreeX])))}] \
         [expr {int(round($::H / 2.0 - $yd * $::H / (2.0 * [screen_set HalfScreenDegreeY])))}]
}
set passdir [file normalize [file join [file dirname [info script]] .. shaders passes]]

# --- the command -------------------------------------------------------------
postpass reset
ck "no passes to start" [expr {[shaderpass] eq ""}]
ck "unknown pass refused" [catch { shaderpass on nothere }]
ck "unknown subcommand refused" [catch { shaderpass bogus }]
ck "create needs a name" [catch { shaderpass create }]
ck "unknown option refused" [catch { shaderpass create x -bogus 1 }]
ck "missing file refused" [catch { shaderpass create x -file /nonexistent/x.glsl }]
ck "a shader that does not build is refused" [catch { shaderpass create x -fragment "this is not glsl;" } e]
ck "  naming the stage ($e)" [string match "*fragment:*" $e]
ck "  and no pass was made" [expr {[shaderpass] eq ""}]
ck "-animate bogus refused" [catch { shaderpass create x -animate maybe }]

# --- one pass ----------------------------------------------------------------
# a red screen
glistInit 1; resetObjList; setBackground 0 0 0
set full [polygon]; scaleObj $full 100 100; polycolor $full 1 0 0
glistAddObject $full 0; glistSetCurGroup 0; glistSetVisible 1

set fsz [expr {$W * $H * 4}]
set fd [file tempfile path shaderpass.rgba]; close $fd
frameSink open -manual $path
set nframes 0
proc keep {} { frameSink write 1; set n $::nframes; incr ::nframes; return $n }

stepMode 1 5000
stepFrame 5010; set f_plain [keep]

shaderpass create sw -fragment {
    uniform sampler2D scene;
    out vec4 frag;
    void main() { frag = texelFetch(scene, ivec2(gl_FragCoord.xy), 0).bgra; }
}
ck "create returns the name; off until turned on" [expr {[shaderpass] eq "sw" && [dict get [shaderpass status sw] on] == 0}]
set pl [lsearch -inline -index 1 [postpass list] shader/sw]
ck "postpass list has it as shader/sw at priority 200" [expr {[llength $pl] && [dict get $pl priority] == 200.0}]
stepFrame 5020; set f_off [keep]
shaderpass on sw
stepFrame 5030; set f_sw [keep]
ck "no `time` uniform: not animated" [expr {[dict get [shaderpass status sw] animated] == 0}]

# replaced by the shipped colour matrix; a failed replacement leaves it
ck "a failed replacement is refused" [catch { shaderpass create sw -fragment "nope;" }]
stepFrame 5040; set f_kept [keep]
shaderpass create sw -file [file join $passdir colormatrix.glsl]
ck "its Uniforms defaults are read" [expr {[shaderpass uniform sw red] eq {1.0 0.0 0.0 0.0}}]
shaderpass on sw
stepFrame 5050; set f_ident [keep]
shaderpass uniform sw green 1 0 0 0           ;# green also takes the scene's red
frameRepaint; set f_yellow [keep]             ;# a uniform change, by repaint alone
ck "uniform: 5 values refused" [catch { shaderpass uniform sw red 1 2 3 4 5 }]
shaderpass off sw
stepFrame 5060; set f_swoff [keep]
shaderpass delete sw
ck "delete: gone from both lists" [expr {[shaderpass] eq "" && [lsearch -index 1 [postpass list] shader/sw] < 0}]

# --- noise -------------------------------------------------------------------
polycolor $full 0.5 0.5 0.5
shaderpass create nz -file [file join $passdir noise.glsl]
shaderpass uniform nz amount 0.25
shaderpass on nz
ck "declares `time`: animated" [expr {[dict get [shaderpass status nz] animated] == 1}]
stepFrame 5100; set f_n1 [keep]
frameRepaint; set f_n1b [keep]                ;# the same stim time: the same field
stepFrame 5200; set f_n2 [keep]               ;# a later one: another field
ck "animate 0 overrides" [expr {[shaderpass animate nz 0] == 0}]
ck "animate auto restores" [expr {[shaderpass animate nz auto] == 1}]
shaderpass delete nz

# --- the chain: gazewin (100) and an inverting pass ---------------------------
polycolor $full 1 1 1
gazewin source test/gaze
gazewin set -mode window -radius 2 -edge 0 -color {0 0 0 1} -hold 0 -lost mask
gazewin on
proc feed { x y } { gazewin datapoint test/gaze 1 0 0 "$x $y" }
proc inverter { priority } {
    shaderpass create inv -file [file join $::passdir colormatrix.glsl] -priority $priority
    shaderpass uniform inv red -1 0 0 1; shaderpass uniform inv green 0 -1 0 1; shaderpass uniform inv blue 0 0 -1 1
    shaderpass on inv
}
inverter 200
set order_after [lmap d [postpass list] { dict get $d name }]
feed -6 0
stepFrame 5300; set f_c1 [keep]               ;# window then invert
feed 6 0
frameRepaint; set f_c2 [keep]                 ;# repaint: gazewin composited, invert staged again
inverter 50
set order_before [lmap d [postpass list] { dict get $d name }]
stepFrame 5310; set f_c3 [keep]               ;# invert then window

# --- reset -------------------------------------------------------------------
shaderpass create extra
postpass reset
ck "postpass reset: every shaderpass deleted" [expr {[shaderpass] eq ""}]
ck "  and gazewin off" [expr {[dict get [gazewin] on] == 0}]
ck "  the chain keeps gazewin only" [expr {[lmap d [postpass list] { dict get $d name }] eq "gazewin"}]
stepFrame 5320; set f_reset [keep]

frameSink close
set fd [open $path rb]; set d [read $fd]; close $fd
file delete $path
stepMode 0

proc px { f xd yd } {
    lassign [deg2px $xd $yd] x y
    binary scan $::d @[expr {$f * $::fsz + ($y * $::W + $x) * 4}]cu3 p
    return $p
}
proc frame { f } { string range $::d [expr {$f * $::fsz}] [expr {($f + 1) * $::fsz - 1}] }
proc is { f xd yd rgb } { expr {[px $f $xd $yd] eq $rgb} }

ck "sink: $nframes frames" [expr {[string length $d] == $nframes * $fsz}]
ck "the scene is red" [is $f_plain 0 0 {255 0 0}]
ck "created but off: the frame is byte-identical" [expr {[frame $f_off] eq [frame $f_plain]}]
ck "bgra swizzle: red shows blue ([px $f_sw 0 0])" [is $f_sw 0 0 {0 0 255}]
ck "after a failed replacement the old pass still draws" [is $f_kept 0 0 {0 0 255}]
ck "colour matrix, identity: byte-identical to the scene" [expr {[frame $f_ident] eq [frame $f_plain]}]
ck "uniform change by frameRepaint: yellow ([px $f_yellow 0 0])" [is $f_yellow 0 0 {255 255 0}]
ck "off: the scene again" [expr {[frame $f_swoff] eq [frame $f_plain]}]

# noise on mid grey, amount 0.25: within +/- 64 of 128, mono, and not flat
set vals {}
foreach x {-10 -5 -1 0 1 5 10} { foreach y {-4 0 4} { lappend vals [px $f_n1 $x $y] } }
set inrange 1; set mono 1
foreach v $vals {
    lassign $v r g b
    if {$r < 62 || $r > 194} { set inrange 0 }
    if {$r != $g || $g != $b} { set mono 0 }
}
ck "noise: within amount of the scene" $inrange
ck "noise: mono" $mono
ck "noise: not flat ([llength [lsort -unique $vals]] values in 21 pixels)" [expr {[llength [lsort -unique $vals]] > 10}]
ck "noise: the same stim time gives the same field" [expr {[frame $f_n1b] eq [frame $f_n1]}]
ck "noise: a later time gives another" [expr {[frame $f_n2] ne [frame $f_n1]}]

ck "chain order, priority 200: $order_after" [expr {$order_after eq "gazewin shader/inv"}]
ck "window then invert: inside black, outside white ([px $f_c1 -6 0] / [px $f_c1 6 0])" \
    [expr {[is $f_c1 -6 0 {0 0 0}] && [is $f_c1 6 0 {255 255 255}]}]
ck "repaint of the chain: the window moved, still inverted ([px $f_c2 6 0] / [px $f_c2 -6 0])" \
    [expr {[is $f_c2 6 0 {0 0 0}] && [is $f_c2 -6 0 {255 255 255}]}]
ck "chain order, priority 50: $order_before" [expr {$order_before eq "shader/inv gazewin"}]
ck "invert then window: black everywhere ([px $f_c3 -6 0] / [px $f_c3 6 0])" \
    [expr {[is $f_c3 -6 0 {0 0 0}] && [is $f_c3 6 0 {0 0 0}]}]
ck "after postpass reset: the scene untouched (white)" [expr {[is $f_reset -6 0 {255 255 255}] && [is $f_reset 6 0 {255 255 255}]}]
finish
