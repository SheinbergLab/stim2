# eye.tcl PORT RATE NAME -- a synthetic eye tracker: write datapoint lines
# to stim2's dataserver port (--port-base + 1) on localhost, in the form
# dserv forwards them,
#     <name> <type> <timestamp_us> <length> {<x>,<y>}
# at RATE per second until killed, then print how many went out.  The
# path is the one a live mirror's own eye subscription takes: stim2's
# processDSCommands -> ::dsCmds(name) -> stim::dispatch -> handler.
# The gaze drifts on a slow Lissajous path, in degrees, with a jump
# (a stand-in saccade) every 300 ms.
lassign $argv port rate name
if {[catch {socket localhost $port} s]} { puts stderr "no stim2 on $port: $s"; exit 1 }
fconfigure $s -translation binary -buffering full -buffersize 65536
set n 0
set t0 [clock microseconds]
set jx 0.0; set jy 0.0; set next_jump 300000
proc stop {} { global n t0; puts "eye: $n sent in [format %.1f [expr {([clock microseconds] - $t0) / 1e6}]] s"; exit 0 }
fconfigure stdin -blocking 0
fileevent stdin readable stop        ;# the driver closes our stdin to stop us
proc tick {} {
    global s n t0 rate name jx jy next_jump
    set now [clock microseconds]
    set due [expr {int(($now - $t0) * $rate / 1e6)}]
    while {$n < $due} {
        set t [expr {$t0 + $n * 1e6 / $rate}]
        if {$t - $t0 >= $next_jump} {
            set jx [expr {rand() * 10 - 5}]; set jy [expr {rand() * 6 - 3}]
            incr next_jump 300000
        }
        set ts [expr {$t / 1e6}]
        set v [format %.3f,%.3f [expr {$jx + 2 * sin($ts * 1.3)}] [expr {$jy + 1.5 * sin($ts * 1.7)}]]
        puts -nonewline $s "$name 1 [expr {wide($t)}] [string length $v] {$v}\n"
        incr n
    }
    flush $s
    after 2 tick
}
tick
vwait forever
