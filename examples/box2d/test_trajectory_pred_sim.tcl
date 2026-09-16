# test_trajectory_pred_sim.tcl -- the example's sandbox simulation, headless.
#
#   dlsh test_trajectory_pred_sim.tcl            (run from this directory)
#
# Loads the SIMULATION half of trajectory_pred.tcl (everything before the
# WORKSPACE INTERFACE section, which needs stim2) into a child interpreter
# with the few stim2 commands it touches stubbed, and checks the sampler
# entry points and the sim wrappers against known outcomes. If an older
# revision of the file is given as an argument (e.g. one saved with
# `git show <rev>:examples/box2d/trajectory_pred.tcl > old.tcl`), it is loaded
# into a second interpreter and the two are compared on identical inputs.

catch { source /usr/local/dlsh/dlsh_setup.tcl }
package require dlsh
set here [file dirname [file normalize [info script]]]
# the on-disk b2world shadows any copy in dlsh.zip
lappend ::auto_path [file join $here .. .. .. dlsh vfs lib b2world]

set ::nfail 0
proc check { label cond } {
    set ok [uplevel 1 [list expr $cond]]
    if { $ok } { puts "  ok   $label" } else { puts "  FAIL $label"; incr ::nfail }
}

# Load the sim half of a trajectory_pred.tcl into a fresh interp.
proc load_sim_interp { path } {
    set f [open $path]; set src [read $f]; close $f
    set cut [string first "# WORKSPACE INTERFACE" $src]
    set cut [string last "\n# ====" [string range $src 0 $cut]]
    set src [string range $src 0 $cut]
    set i [interp create]
    $i eval [list set ::auto_path $::auto_path]
    $i eval { catch { source /usr/local/dlsh/dlsh_setup.tcl }; package require dlsh }
    # source the ON-DISK engine: a `package require` would find whatever
    # copy dlsh.zip carries first, and auto_path does not shadow the zip
    $i eval [list source [file join $::here .. .. .. dlsh vfs lib b2world b2world.tcl]]
    $i eval { set ::pi 3.14159265358979 }
    $i eval { proc screen_set { what } { return 16.6667 } }
    $i eval $src
    return $i
}

set new [load_sim_interp [file join $here trajectory_pred.tcl]]
check "new file loads headless" {[$new eval {info procs tpred_sim_trajectory}] ne ""}
check "old private loops are gone" {[$new eval {info procs tpred_sim_trajectory_wind}] ne "" && [$new eval {info body tpred_sim_trajectory_wind}] ne "" && [string match *tpred_world_spec* [$new eval {info body tpred_sim_trajectory_wind}]]}

# fixed inputs (no rand): a rightward launch from the left
set step 0.0166667
set cases {
    plain     {tpred_sim_trajectory -6.5 0.0 6.36 6.36 -5.5 0.0166667 0}
    catch_ok  {tpred_sim_catch_test -6.5 0.0 6.36 6.36 LAND -5.5 0.0166667 0}
    catch_no  {tpred_sim_catch_test -6.5 0.0 6.36 6.36 -20.0 -5.5 0.0166667 0}
    wind      {tpred_sim_trajectory_wind -6.5 0.0 6.36 6.36 -5.5 0.0166667 0.0 0.0 4.0 4.0 1.5}
    wind_c    {tpred_sim_catch_test_wind -6.5 0.0 6.36 6.36 LAND -5.5 0.0166667 0.0 0.0 4.0 4.0 1.5}
    blockers  {tpred_sim_trajectory_blockers -6.5 0.0 6.36 6.36 -5.5 0.0166667 {{1.0 0.5} {6.0 -2.0}}}
    blockers_c {tpred_sim_catch_test_blockers -6.5 0.0 6.36 6.36 LAND -5.5 0.0166667 {{1.0 0.5} {6.0 -2.0}}}
}

proc run_case { i cmd land } {
    set cmd [string map [list LAND $land] $cmd]
    return [$i eval $cmd]
}

puts "\nnew: outcomes on fixed inputs"
set r [run_case $new [dict get $cases plain] 0]
check "plain: ok + crossed" {[dict get $r ok] && [dict get $r crossed]}
set land [dict get $r land_x]
check "plain: lands to the right of the launch" {$land > -6.5}
check "plain: path recorded" {[llength [dict get $r path_x]] > 10}
check "catcher at the landing x catches" {[run_case $new [dict get $cases catch_ok] $land] == 1}
check "catcher far away does not"        {[run_case $new [dict get $cases catch_no] $land] == 0}
set rw [run_case $new [dict get $cases wind] 0]
check "wind: time_in_zone reported" {[dict exists $rw time_in_zone] && [dict get $rw time_in_zone] > 0}
check "wind: pushed further than plain" {[dict get $rw land_x] > $land + 0.5}
set landw [dict get $rw land_x]
check "wind: catcher at its landing x catches" {[run_case $new [dict get $cases wind_c] $landw] == 1}
set rb [run_case $new [dict get $cases blockers] 0]
check "blockers: n_contacts + hit_names" {[dict get $rb n_contacts] == [llength [dict get $rb hit_names]]}
check "blockers: a blocker in the path was hit" {[dict get $rb n_contacts] >= 1}
if { [dict get $rb crossed] } {
    set landb [dict get $rb land_x]
    check "blockers: catcher at its landing x catches" {[run_case $new [dict get $cases blockers_c] $landb] == 1}
}

puts "\nnew: the samplers run end to end (seeded)"
$new eval { expr {srand(7)} }
check "launch sampler (catch mode)" {[$new eval { set tpred::mode catch; tpred_sample_launch_trial }] == 1}
check "control sampler"             {[$new eval { set tpred::mode control; tpred_sample_control_trial }] == 1}
check "blockers sampler"            {[$new eval { set tpred::mode blockers; tpred_sample_blockers_trial }] == 1}
check "wind sampler"                {[$new eval { set tpred::mode wind; tpred_sample_wind_trial }] == 1}
$new eval { set tpred::plank_enabled 1 }
check "launch sampler with the plank" {[$new eval { set tpred::mode catch; tpred_sample_launch_trial }] == 1}
check "...and the accepted flight hit an arm" {[$new eval { set r [tpred_sim_trajectory $tpred::ball_x0 $tpred::ball_y0 $tpred::launch_vx $tpred::launch_vy $tpred::catch_strip_y 0.0166667 1]; dict get $r hit_plank }] == 1}

# ---- optional: compare against an older revision on identical inputs ----
if { [llength $argv] } {
    set old [load_sim_interp [lindex $argv 0]]
    puts "\nold vs new on identical inputs:"
    foreach { name cmd } $cases {
        set land 0.0
        if { [string match *LAND* $cmd] } {
            set base [string map {catch_test trajectory} $cmd]
            # land x from the new trajectory of the same flight
            regsub {LAND } $base {} base
            set land [dict get [$new eval $base] land_x]
        }
        set a [run_case $old $cmd $land]
        set b [run_case $new $cmd $land]
        if { [string is integer -strict $a] } {
            check "$name: same verdict ($a)" {$a == $b}
        } else {
            foreach k {ok crossed} { check "$name: $k agrees" {[dict get $a $k] == [dict get $b $k]} }
            if { [dict get $a crossed] } {
                check "$name: land_x agrees" {abs([dict get $a land_x] - [dict get $b land_x]) < 1e-4}
                # the old loop stamped land_t with the PRE-step time; the engine
                # stamps the post-step time the crossing was observed at
                check "$name: land_t agrees (new = old + one step)" {abs([dict get $b land_t] - [dict get $a land_t] - 0.0166667) < 1e-4}
            }
            foreach k {hit_plank n_contacts time_in_zone} {
                if { [dict exists $a $k] && [dict exists $b $k] } {
                    check "$name: $k agrees" {abs([dict get $a $k] - [dict get $b $k]) < 1e-6}
                }
            }
        }
    }
}

puts ""
if { $::nfail } { puts "FAILURES: $::nfail"; exit 1 }
puts "all checks passed"
