#!/usr/bin/env tclsh
# test_photopatch_acks.tcl -- the photodiode patch's marked mode with
# swap-acked ('!') commands (docs/photopatch.md).  A client, like
# test_ack_header.tcl: '!' replies only come from the message port, so this
# runs OUTSIDE stim2, against a running one with the photopatch stimdll:
#
#     stim2 --headless -w 320 -h 180 --port-base 5610 &
#     tclsh tests/test_photopatch_acks.tcl localhost 5612
#
# Checks: a '!' command's frame changes the patch (one change per frame,
# however many '!' replies it releases); a plain command's frame does not;
# a '!' command that draws nothing gets no frame, and no change; manual
# mode ignores '!' frames.  Leaves the patch off (`postpass reset`).
# Prints OK/FAIL lines and exits non-zero on any failure.

set host [expr {$argc > 0 ? [lindex $argv 0] : "localhost"}]
set port [expr {$argc > 1 ? [lindex $argv 1] : 4612}]

set ::fails 0
set ::passes 0
proc check {name cond {detail ""}} {
    if {$cond} { incr ::passes; puts "OK   $name" } \
    else { incr ::fails; puts "FAIL $name  $detail" }
}

proc msg_write {sock s} {
    puts -nonewline $sock [binary format I [string length $s]]$s
    flush $sock
}
proc msg_read {sock} {
    set hdr [read $sock 4]
    if {[string length $hdr] != 4} { error "connection closed" }
    binary scan $hdr I n
    set n [expr {$n & 0xffffffff}]
    return [expr {$n ? [read $sock $n] : ""}]
}
proc msg_send {sock s} { msg_write $sock $s; msg_read $sock }

# the swapcount of an '@' reply's ACK1 header
proc swap_of {reply} {
    if {![regexp {^ACK1 \S+ \S+ ([0-9]+) } $reply -> s]} { return -1 }
    return $s
}
proc st {key} { dict get [msg_send $::sock "photopatch status"] $key }
proc settle {} { after 150 }

set sock [socket $host $port]
fconfigure $sock -translation binary -buffering none

if {[msg_send $sock "info commands photopatch"] ne "photopatch"} {
    puts "FAIL this stim2 has no photopatch stimdll"
    exit 1
}

msg_send $sock "postpass reset"
msg_send $sock "photopatch on"
msg_send $sock "@!redraw"                ;# the patch appears (why 1)
settle
check "on, marked mode" [expr {[st on] == 1 && [st mode] eq "marked"}]

# a '!' frame changes it
set c0 [st changes]; set m0 [st marked]; set l0 [st level]
set s1 [swap_of [msg_send $sock "@!redraw"]]
check "a '!' command's reply carries a swap" [expr {$s1 > 0}]
check "  its frame changed the patch" [expr {[st changes] == $c0 + 1 && [st level] == !$l0}]
check "  and was counted as marked" [expr {[st marked] == $m0 + 1}]

# a plain command's frame does not
set c1 [st changes]; set f1 [st frames]
msg_send $sock "redraw"
settle
check "a plain redraw: a frame, no change" [expr {[st frames] > $f1 && [st changes] == $c1}]

# a '!' command that draws nothing: no frame of its own, so no change
set c2 [st changes]; set f2 [st frames]
set sb [swap_of [msg_send $sock "@set x 1"]]
set s2 [swap_of [msg_send $sock "@!set x 2"]]
check "a '!' that draws nothing: no swap, no change" \
    [expr {$s2 == $sb && [st changes] == $c2 && [st frames] == $f2}] \
    "swap $sb -> $s2, changes $c2 -> [st changes]"

# two '!' commands at once, from two connections: one change per swap
# they landed on (the same one when dequeued in one pass)
set sock2 [socket $host $port]
fconfigure $sock2 -translation binary -buffering none
set c3 [st changes]
msg_write $sock "@!redraw"; msg_write $sock2 "@!redraw"
set a [swap_of [msg_read $sock]]; set b [swap_of [msg_read $sock2]]
settle
set nswaps [llength [lsort -unique [list $a $b]]]
check "two '!' at once: one change per swap ($nswaps)" [expr {[st changes] == $c3 + $nswaps}] \
    "swaps $a $b, changes $c3 -> [st changes]"
close $sock2

# manual: '!' frames do not change it
msg_send $sock "photopatch set -mode manual"
set c4 [st changes]
msg_send $sock "@!redraw"
settle
check "manual: a '!' frame does not change it" [expr {[st changes] == $c4}]

msg_send $sock "postpass reset"
check "postpass reset: off" [expr {[st on] == 0}]
close $sock

puts "$::passes ok, $::fails FAIL"
exit [expr {$::fails > 0}]
