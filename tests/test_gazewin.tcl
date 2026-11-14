# test_gazewin.tcl -- the gaze-contingent pass (gazewin), in a real stim2
#
#   stim2 --headless -w 320 -h 180 --port-base 5610 -f /abs/path/to/tests/test_gazewin.tcl
#   stim2 -w 640 -h 480 -f /abs/path/to/tests/test_gazewin.tcl
#
# Draws known scenes, turns the pass on with a fixed point or with samples
# handed over as datapoints, and checks the grabbed pixels: window and
# scotoma, the mask colour and its alpha, lost gaze (mask / show), the
# background as the mask, a blank (invisible) display, blur, a custom
# shader file with its uniforms, a shader that fails to build, the argument
# errors, and that `gazewin reset` leaves the display exactly as it was
# before the pass was ever on.  Steps are chained through frameGrab (see
# test_frame_grab.tcl for why not Tcl's after).  The repaint path a static
# display takes between grabs, and the sample going stale, are exercised
# by tests/test_gazewin_client.tcl against a running stim2.

set ::log {}
proc note { s } { lappend ::log $s }
proc ck { label cond } { note "[expr {$cond ? {OK  } : {FAIL}}] $label" }
proc finish {} {
    catch { gazewin reset }
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
set ::W [screen_set WinWidth]; set ::H [screen_set WinHeight]
set ::ppdx [expr {$::W / (2.0 * [screen_set HalfScreenDegreeX])}]
set ::ppdy [expr {$::H / (2.0 * [screen_set HalfScreenDegreeY])}]
# RGB at (x, y) degrees, origin at the centre, y up
proc at { bytes x y } {
    set c [expr {int(floor($::W / 2.0 + $x * $::ppdx))}]
    set r [expr {int(floor($::H / 2.0 - $y * $::ppdy))}]
    lrange [px $::W $bytes $c $r] 0 2
}
proc near { rgb want {tol 3} } {
    foreach a $rgb b $want { if {abs($a - $b) > $tol} { return 0 } }
    return 1
}

# --- arguments, before anything is on ---------------------------------------
set st [gazewin]
ck "status: off, no source, default shader" [expr {[dict get $st on] == 0 &&
    [dict get $st source] eq "" && [dict get $st shader] eq "default" &&
    [dict get $st point] eq "off" && [dict get $st mode] eq "window"}]
ck "set with no options returns the settings" [dict exists [gazewin set] radius]
ck "set -mode bogus refused" [catch { gazewin set -mode bogus }]
ck "set -radius -1 refused" [catch { gazewin set -radius -1 }]
ck "set -radius needs a value" [catch { gazewin set -radius }]
ck "set -late -1 accepted (follow stimLateDraw)" [expr {![catch { gazewin set -late -1 }]}]
ck "set -late -2 refused" [catch { gazewin set -late -2 }]
ck "set -lost bogus refused" [catch { gazewin set -lost bogus }]
ck "set -color needs 3 or 4" [catch { gazewin set -color {1 0} }]
ck "unknown option refused" [catch { gazewin set -bogus 1 }]
ck "a refused set changes nothing" [expr {[dict get [gazewin set] radius] == 5.0}]
ck "uniform: unset reads empty" [expr {[gazewin uniform nothere] eq ""}]
ck "uniform: 5 values refused" [catch { gazewin uniform u 1 2 3 4 5 }]
ck "point needs x y" [catch { gazewin point 1 }]
ck "datapoint needs five words" [catch { gazewin datapoint a b }]
ck "shader: missing file refused" [catch { gazewin shader /nonexistent/x.glsl }]
ck "unknown subcommand refused" [catch { gazewin bogus }]
# the module is a post pass in stim2's chain (docs/postpass.md)
set ::pp [lsearch -inline -index 1 [postpass list] gazewin]
ck "postpass list has gazewin, off" [expr {[llength $::pp] && [dict get $::pp on] == 0}]
ck "postpass: unknown subcommand refused" [catch { postpass bogus }]

# --- scenes ------------------------------------------------------------------
# A: white over the whole screen.  B: white left half, black right half.
glistInit 1
resetObjList
setBackground 0 0 0
set ::full [polygon]; scaleObj $::full 200 200; polycolor $::full 1 1 1
set ::left [polygon]; scaleObj $::left 100 200; translateObj $::left -50 0; polycolor $::left 1 1 1
glistAddObject $::full 0; glistAddObject $::left 0
setVisible $::left 0
glistSetCurGroup 0
glistSetVisible 1
redraw
proc sceneA {} { setVisible $::full 1; setVisible $::left 0 }
proc sceneB {} { setVisible $::full 0; setVisible $::left 1 }

set ::shaderfile [file join [file dirname [info script]] gazewin_test_tint.glsl]
set f [open $::shaderfile w]
puts $f {-- Fragment
uniform sampler2D scene;
uniform vec4 tint;
out vec4 frag;
void main() { frag = vec4(tint.rgb, 1.0); }

-- Uniforms
tint "1.0 0.0 0.0 1.0"}
close $f
# a pass that shows level 3, over a pyramid filter that paints it red
set ::dsfile [file join [file dirname [info script]] gazewin_test_ds.glsl]
set f [open $::dsfile w]
puts $f {-- Fragment
uniform sampler2D scene;
uniform vec2 resolution;
out vec4 frag;
void main() { frag = textureLod(scene, gl_FragCoord.xy / resolution, 3.0); }

-- Downsample
uniform sampler2D src;
out vec4 frag;
void main() { frag = vec4(1.0, 0.0, 0.0, 1.0); }}
close $f
set ::dsbadfile [file join [file dirname [info script]] gazewin_test_dsbad.glsl]
set f [open $::dsbadfile w]
puts $f "-- Fragment\nout vec4 frag;\nvoid main() { frag = vec4(0.0); }\n-- Downsample\nthis is not glsl;\n"
close $f
set ::badfile [file join [file dirname [info script]] gazewin_test_bad.glsl]
set f [open $::badfile w]
puts $f "-- Fragment\nthis is not glsl;\n"
close $f

proc f2b64 { x y } { binary encode base64 [binary format f2 [list $x $y]] }

# Each step: a setup script, then a check run on the next grabbed frame.
# (No comments inside the list: they would be list elements.)
#
# The graded-blur steps (-falloff) use scene B's black/white edge at x = 0
# and measure the bleed 0.8 deg to its right.  Gaze at -2 (radius 1) puts
# the edge 1 deg into the treated region: uniform blur 6 smears it, graded
# hardly at all.  Gaze at -14 puts it 13 deg in: graded smears it too.
set ::steps {
    { {}
      { set ::base $bytes
        ck "off: the scene untouched" [expr {[near [at $bytes 0 0] {255 255 255}] && [near [at $bytes 6 3] {255 255 255}]}] } }

    { { gazewin set -mode window -radius 2 -edge 0 -color {0 0 0 1}; gazewin point 0 0
        ck "on: refused nothing" [expr {![catch { gazewin on }]}] }
      { ck "window: centre shows" [near [at $bytes 0 0] {255 255 255}]
        ck "window: 1 deg out shows" [near [at $bytes 1 0] {255 255 255}]
        ck "window: 3 deg out is masked" [near [at $bytes 3 0] {0 0 0}]
        ck "window: the corner is masked" [near [px $::W $bytes 2 2] {0 0 0 255}]
        ck "status: on, a stage drawn" [expr {[dict get [gazewin] on] == 1 && [dict get [gazewin] stages] > 0}] } }

    { { gazewin point 4 0 }
      { ck "point moved: the old centre is masked" [near [at $bytes 0 0] {0 0 0}]
        ck "point moved: (4,0) shows" [near [at $bytes 4 0] {255 255 255}]
        ck "point moved: (4,1.5) shows (y up)" [near [at $bytes 4 1.5] {255 255 255}]
        ck "point moved: (4,3) is masked" [near [at $bytes 4 3] {0 0 0}] } }

    { { gazewin set -mode scotoma }
      { ck "scotoma: (4,0) hidden" [near [at $bytes 4 0] {0 0 0}]
        ck "scotoma: the centre shows" [near [at $bytes 0 0] {255 255 255}] } }

    { { gazewin set -mode window -color {0 0 0 0.5} }
      { ck "mask alpha 0.5: half contrast ([at $bytes 0 0])" [near [at $bytes 0 0] {128 128 128} 3]
        ck "mask alpha 0.5: the window is untouched" [near [at $bytes 4 0] {255 255 255}] } }

    { { gazewin set -edge 1.0 -color {0 0 0 1} }
      { lassign [at $bytes 6 0] r
        ck "soft edge: mid-edge is partway ($r)" [expr {$r > 30 && $r < 225}]
        ck "soft edge: inside is full" [near [at $bytes 4 0] {255 255 255}] } }

    { { gazewin set -edge 0; gazewin point off }
      { ck "no gaze, -lost mask: all masked" [expr {[near [at $bytes 0 0] {0 0 0}] && [near [at $bytes 4 0] {0 0 0}]}]
        ck "status: gaze_lost" [expr {[dict get [gazewin] gaze_lost] == 1}] } }

    { { gazewin set -lost show }
      { ck "no gaze, -lost show: the scene" [near [at $bytes 4 0] {255 255 255}] } }

    { { gazewin set -lost mask -hold 0
        ck "source without dservhost: not subscribed" [expr {[gazewin source test/gaze] == 0}]
        ck "datapoint: another name ignored" [expr {[gazewin datapoint other/x 2 0 8 [f2b64 0 0]] == 0}]
        ck "datapoint: base64 floats taken" [expr {[gazewin datapoint test/gaze 2 1000 8 [f2b64 -4 0]] == 1}]
        set g [dict get [gazewin] gaze]
        ck "status gaze {-4 0} ($g)" [expr {abs([lindex $g 0] + 4) < 1e-6 && abs([lindex $g 1]) < 1e-6}] }
      { ck "datapoint: (-4,0) shows" [near [at $bytes -4 0] {255 255 255}]
        ck "datapoint: the centre is masked" [near [at $bytes 0 0] {0 0 0}] } }

    { { ck "datapoint: plain numbers taken" [expr {[gazewin datapoint test/gaze 1 1001 3 {2 1}] == 1}]
        set g [dict get [gazewin] gaze]
        ck "status gaze {2 1}" [expr {abs([lindex $g 0] - 2) < 1e-6 && abs([lindex $g 1] - 1) < 1e-6}] }
      { ck "plain sample: (2,1) shows" [near [at $bytes 2 1] {255 255 255}] } }

    { { gazewin datapoint test/gaze 2 1002 8 [f2b64 NaN 0] }
      { ck "a NaN sample is lost: masked" [near [at $bytes 2 1] {0 0 0}]
        ck "status counts it invalid" [expr {[dict get [gazewin] invalid] >= 1}] } }

    { { gazewin source ""; gazewin point 0 0; gazewin set -color background; setBackground 0 0 255 }
      { ck "-color background: the corner is the background" [near [px $::W $bytes 2 2] {0 0 255 255}]
        ck "-color background: the window shows" [near [at $bytes 0 0] {255 255 255}] } }

    { { ck "-color {background 2} refused" [catch { gazewin set -color {background 2} }]
        gazewin set -color {background 0.5}
        ck "settings report {background 0.5}" [expr {[dict get [gazewin set] color] eq {background 0.5}}] }
      { ck "-color {background 0.5}: halfway to the background ([px $::W $bytes 2 2])" [near [px $::W $bytes 2 2] {128 128 255 255}] } }

    { { gazewin set -color {1 0 0 1}; glistSetVisible 0; redraw }
      { ck "blank display: masked too (corner red)" [near [px $::W $bytes 2 2] {255 0 0 255}]
        ck "blank display: the window is the background" [near [at $bytes 0 0] {0 0 255}] } }

    { { glistSetVisible 1; setBackground 0 0 0; sceneB
        gazewin point -8 0; gazewin set -radius 1 -color {0 0 0 0} -blur 0 }
      { ck "blur 0, mask alpha 0: right of the edge is black" [near [at $bytes 0.4 0] {0 0 0}]
        ck "blur 0, mask alpha 0: left of the edge is white" [near [at $bytes -0.4 0] {255 255 255}] } }

    { { gazewin set -blur 6 }
      { lassign [at $bytes 0.4 0] r1; lassign [at $bytes -0.4 0] r2
        ck "blur 6: the edge bleeds right ($r1)" [expr {$r1 > 20}]
        ck "blur 6: and left ($r2)" [expr {$r2 < 235}]
        ck "blur 6: the window is exact" [near [at $bytes -8 0] {255 255 255} 0] } }

    { { ck "-falloff -1 refused" [catch { gazewin set -falloff -1 }]
        gazewin point -2 0 }
      { lassign [at $bytes 0.8 0] ::r_uni
        ck "falloff 0 (uniform blur 6), near the window: smeared ($::r_uni)" [expr {$::r_uni > 40}] } }

    { { gazewin set -falloff 0.5
        ck "settings report falloff 0.5" [expr {[dict get [gazewin set] falloff] == 0.5}] }
      { lassign [at $bytes 0.8 0] r
        ck "falloff 0.5, 1 deg past the window: nearly sharp ($r, uniform $::r_uni)" [expr {$r < 20 && $r < $::r_uni - 30}]
        ck "falloff 0.5: the window is exact" [near [at $bytes -2 0] {255 255 255} 0] } }

    { { gazewin point -14 0 }
      { lassign [at $bytes 0.8 0] r
        ck "falloff 0.5, 13 deg past the window: smeared ($r)" [expr {$r > 40}]
        ck "falloff 0.5: the window is exact there too" [near [at $bytes -14 0] {255 255 255} 0] } }

    { { gazewin point off }
      { lassign [at $bytes 0.8 0] r
        ck "falloff, gaze lost: the whole screen at blur 6 ($r)" [expr {$r > 40}] } }

    { { sceneA; gazewin point 0 0; gazewin set -blur 0 -falloff 0
        ck "shader file loads" [expr {[gazewin shader $::shaderfile] eq $::shaderfile}]
        ck "its uniform default is read" [expr {[gazewin uniform tint] eq {1.0 0.0 0.0 1.0}}] }
      { ck "custom shader: its output (red)" [near [at $bytes 0 0] {255 0 0}] } }

    { { gazewin uniform tint 0 1 0 1 }
      { ck "custom uniform set: green" [near [at $bytes 0 0] {0 255 0}] } }

    { { ck "a shader that does not build is refused" [catch { gazewin shader $::badfile } e]
        ck "  with the compiler's message" [string match "*shader*" $e]
        ck "  and the last one stays" [expr {[gazewin shader] eq $::shaderfile}] }
      { ck "the last shader still draws (green)" [near [at $bytes 0 0] {0 255 0}] } }

    { { gazewin set -blur 3
        ck "a file with a Downsample section loads" [expr {[gazewin shader $::dsfile] eq $::dsfile}] }
      { ck "its filter made the levels (red)" [near [at $bytes 0 0] {255 0 0}] } }

    { { ck "a Downsample that does not build is refused" [catch { gazewin shader $::dsbadfile } e]
        ck "  saying which section" [string match "*downsample*" $e]
        ck "  and the last file stays" [expr {[gazewin shader] eq $::dsfile}] }
      { ck "  with its filter (red)" [near [at $bytes 0 0] {255 0 0}] } }

    { { gazewin shader default }
      { ck "back to the built-in filter: the levels remade (blurred white, not red: [at $bytes 6 3])" [near [at $bytes 6 3] {255 255 255} 8] } }

    { { gazewin set -blur 0
        ck "off returns 0" [expr {[gazewin off] == 0}] }
      { ck "off: the scene untouched" [expr {[near [at $bytes 0 0] {255 255 255}] && [near [at $bytes 6 3] {255 255 255}]}] } }

    { { gazewin on; gazewin set -radius 3 -mode scotoma -falloff 1; gazewin source test/gaze
        ck "postpass list shows it on" [expr {[dict get [lsearch -inline -index 1 [postpass list] gazewin] on] == 1}]
        postpass reset            ;# what stim::prelude runs at every script load
        set st [gazewin]
        ck "postpass reset: off, no source, defaults" [expr {[dict get $st on] == 0 && [dict get $st source] eq "" &&
            [dict get $st mode] eq "window" && [dict get $st radius] == 5.0 && [dict get $st falloff] == 0.0 &&
            [dict get $st point] eq "off"}] }
      { ck "reset: the frame is byte-identical to before the pass was ever on" [expr {$bytes eq $::base}] } }
}

set ::stepi 0
proc next {} {
    if {$::stepi >= [llength $::steps]} {
        file delete $::shaderfile $::badfile $::dsfile $::dsbadfile
        finish
    }
    lassign [lindex $::steps $::stepi] setup check
    incr ::stepi
    if {[catch { uplevel #0 $setup } e]} { ck "step $::stepi setup: $e" 0 }
    frameGrab [list apply [list {w h bytes sc} "$check\nnext"]]
}
afterFrames 2 next
