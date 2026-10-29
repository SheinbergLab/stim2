# inproc.tcl -- a live mirror inside this stim2: tools/mirror/mirror.tcl,
# unchanged, in a worker thread of its own (the Thread package), talking to
# stim2's interpreter through stimSend in place of the message port.
#
#     stim2 --mirror "192.168.88.40 ?mirror.tcl options?" ...
#
# stim2 sources this and calls mirror::start once its startup files have
# run.  The worker has its own interpreter: its dserv subscription, its
# state and its timers never touch stim2's (whose ::dsCmds and bindings a
# rig reload clears), and stim2's interpreter sees only the requests a
# mirror sends any stim2: setup, frames, gaze.  Every request runs on the
# render thread at the point message-port commands do (stimSend in
# stim2.cpp), so stepping is exactly as with the separate driver.

package require Thread

namespace eval mirror {
    variable tools [file dirname [file dirname [file normalize [info script]]]]
    variable worker ""
}

# mirror::start dserv ?option ...? -- options as mirror.tcl takes them
# (-eye, -trail, -report, ...); -host, -port, -live and -launch do not
# apply.  Returns the worker's thread id.
proc mirror::start {dserv args} {
    variable tools
    variable worker
    if {$worker ne "" && [thread::exists $worker]} { error "mirror: already running (thread $worker)" }
    set script [list apply {{paths argv mirror} {
        lassign $paths ap tm
        set ::auto_path $ap
        foreach p [lreverse $tm] { catch { tcl::tm::path add $p } }
        load {} Stimsend
        set ::argv $argv
        set ::argc [llength $argv]
        set ::argv0 $mirror
        # the driver's exit ends its thread, never this stim2
        rename exit _exit
        proc exit {{code 0}} { puts "mirror: driver stopped ($code)"; thread::exit }
        if {[catch { uplevel #0 [list source $mirror] } err]} {
            puts stderr "mirror: $err"
        }
    }} [list $::auto_path [tcl::tm::path list]] [list -dserv $dserv -inproc {*}$args] \
                    [file join $tools mirror mirror.tcl]]
    set worker [thread::create $script]
    return $worker
}
