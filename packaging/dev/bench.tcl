# bench.tcl -- render throughput: stepped frames into a frame sink, as a
# replay render makes them
#
#   stim2 --headless -w 1920 -h 1080 --port-base 5610 -f /abs/path/to/packaging/dev/bench.tcl
#   packaging/dev/bench-linux.sh          # the Linux dev build, in the image
#
# A planko-like board (14 planks and a ball), then the same with a gaze
# window (masked; graded blur), a noise pass, and both.  Prints a BENCH line
# per condition: ms per frame and frames per second.  Environment:
#
#   BENCH_N        frames timed per condition (60), after 3 to warm up
#   BENCH_SINK     where the frames go (/dev/null); a file to keep them
#   BENCH_SHADERS  a shaders/ directory holding passes/noise.glsl, if the
#                  stim2's own (its system shader directory) has none
#
# The first line, without the sink, is not a rendering time: with nothing
# read back, a renderer may put the work off (llvmpipe does).
#
# It measures stim2 alone: no video encoding, and none of what a real stim
# script does each frame (physics, video, images).  Needs the gazewin and
# shaderpass stimdlls.
set W [screen_set WinWidth]; set H [screen_set WinHeight]
screen_set ScreenWidthCm 53; screen_set ScreenHeightCm 30; screen_set DistanceToMonitor 57
screen_config
glistInit 1; resetObjList; setBackground 60 60 60
# a planko-like board: 14 planks and a ball
expr {srand(7)}
for {set i 0} {$i < 14} {incr i} {
    set p [polygon]; scaleObj $p [expr {2 + 1.5*rand()}] 0.5
    translateObj $p [expr {-12 + 24*rand()}] [expr {-7 + 14*rand()}]
    rotateObj $p [expr {180*rand()}] 0 0 1
    polycolor $p 1 1 1; glistAddObject $p 0
}
set ball [polygon]; scaleObj $ball 0.8 0.8; polycolor $ball 0 1 1; glistAddObject $ball 0
glistSetCurGroup 0; glistSetVisible 1
set out [expr {[info exists env(BENCH_SINK)] ? $env(BENCH_SINK) : "/dev/null"}]
set N [expr {[info exists env(BENCH_N)] ? $env(BENCH_N) : 60}]
stepMode 1 5000
set ::t 5000.0
proc run { label sink gaze } {
    # warm up (shader builds, textures), then time N frames
    for {set pass 0} {$pass < 2} {incr pass} {
        set n [expr {$pass ? $::N : 3}]
        set t0 [clock microseconds]
        for {set i 0} {$i < $n} {incr i} {
            set ::t [expr {$::t + 8.3333}]
            translateObj $::ball [expr {8*sin($::t/300.0)}] [expr {5*cos($::t/200.0)}]
            if {$gaze} { gazewin point [expr {10*sin($::t/500.0)}] [expr {5*cos($::t/400.0)}] }
            stepFrame $::t
            if {$sink} { frameSink write 1 }
        }
        set dt [expr {([clock microseconds] - $t0) / 1000.0 / $n}]
    }
    puts [format "BENCH %-34s %7.2f ms/frame  %6.1f frames/s" $label $dt [expr {1000.0/$dt}]]
}
puts "BENCH ${W}x$H, $N frames per condition"
run "scene, no readback" 0 0
frameSink open -manual $out
run "scene + frame sink" 1 0
gazewin set -mode window -radius 4 -edge 0.5 -color background -blur 0 -falloff 0 -hold 0
gazewin point 0 0; gazewin on
run "+ gaze window (soft, masked)" 1 1
gazewin set -color {background 0} -blur 6 -falloff 0.15
run "+ gaze window (graded blur)" 1 1
gazewin off
if {[info exists env(BENCH_SHADERS)]} {
    shaderpass create nz -file [file join $env(BENCH_SHADERS) passes noise.glsl]
} else {
    shaderpass create nz -file noise          ;# along the shader search path
}
shaderpass uniform nz amount 0.15; shaderpass on nz
run "+ noise pass" 1 0
gazewin on
run "+ graded blur + noise" 1 1
postpass reset
frameSink close
stepMode 0
exit 0
