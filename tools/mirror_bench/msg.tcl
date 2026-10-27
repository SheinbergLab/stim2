# msg.tcl PORT SCRIPT -- send one request to stim2's message port on
# localhost, print the reply; exit 1 on a Tcl error or no connection.
# One write per request (header and payload together): two writes meet
# Nagle and the delayed ACK on Linux.
lassign $argv port script
if {[catch {socket localhost $port} s]} { puts stderr "no stim2 on $port: $s"; exit 1 }
fconfigure $s -translation binary -buffering none
puts -nonewline $s [binary format I [string length $script]]$script
flush $s
binary scan [read $s 4] I n
set r [read $s [expr {$n & 0xffffffff}]]
close $s
puts $r
exit [string match "!TCL_ERROR*" $r]
