# discover.tcl -- the dservs on this link, by mDNS: `stimMirror discover`
# (stim2 evaluates this embedded copy), or standalone:
#
#     dlsh tools/mirror/discover.tcl ?ms?
#
# Every dserv advertises one `_dserv._tcp` record through the host's mDNS
# responder (dserv docs/discovery.md): instance = its hostname, port 2560,
# TXT dp (the %reg/%match datapoint port a mirror subscribes on), ver, wg,
# web, newline, ssl.  This browses with the system's own tool, so stim2
# needs no DNS-SD library: dns-sd on a Mac (mDNSResponder), avahi-browse
# on Linux (avahi-utils).  It waits ms (default 1500) for answers; that
# long, the calling thread is busy.
#
# Returns a list of dicts: name host port and the TXT keys present.

namespace eval mirror {}

proc mirror::browse_cmd {} {
    if {$::tcl_platform(os) eq "Darwin"} {
        return [list dns-sd -Z _dserv._tcp local.]
    } elseif {[auto_execok avahi-browse] ne ""} {
        return [list avahi-browse -rpt _dserv._tcp]
    }
    error "no mDNS browser here: dns-sd (macOS) or avahi-browse (avahi-utils)"
}

proc mirror::discover {{ms 1500}} {
    set ch [open |[concat [browse_cmd] [list 2>@1]] r]
    fconfigure $ch -blocking 0
    set out ""
    set t0 [clock milliseconds]
    while {[clock milliseconds] - $t0 < $ms} {
        append out [read $ch]
        if {[eof $ch]} break                   ;# avahi-browse -t ends itself
        after 50
    }
    catch { exec kill {*}[pid $ch] }           ;# dns-sd browses until killed
    catch { close $ch }
    parse $out
}

# The same without waiting, for a caller whose event loop runs (stim2's
# services Tcl events every pass): the answer lands in mirror::found, and
# mirror::browsing is 1 meanwhile.  The imgui "Mirror a rig" panel uses it.
namespace eval mirror { variable found {}; variable browsing 0; variable buf "" }
proc mirror::discover_async {{ms 1500}} {
    variable browsing
    variable buf
    if {$browsing} return
    set browsing 1
    set buf ""
    if {[catch { open |[concat [browse_cmd] [list 2>@1]] r } ch]} {
        set browsing 0
        error $ch
    }
    fconfigure $ch -blocking 0
    fileevent $ch readable [list mirror::browse_read $ch]
    after $ms [list mirror::browse_done $ch]
}
proc mirror::browse_read {ch} {
    variable buf
    append buf [read $ch]
    if {[eof $ch]} { fileevent $ch readable {} }
}
proc mirror::browse_done {ch} {
    variable buf
    variable found
    variable browsing
    catch { append buf [read $ch] }
    catch { exec kill {*}[pid $ch] }
    catch { close $ch }
    set found [parse $buf]
    set browsing 0
}

# the browser's output as a list of dicts: name host port, TXT keys
proc mirror::parse {out} {
    set found [dict create]
    foreach line [split $out \n] {
        if {[string index $line 0] eq "="} {
            # avahi: =;iface;proto;name;type;domain;host;address;port;"k=v" ...
            set f [split $line ";"]
            if {[lindex $f 2] ne "IPv4" && [dict exists $found [unescape [lindex $f 3]]]} continue
            set name [unescape [lindex $f 3]]
            dict set found $name name $name
            dict set found $name host [lindex $f 7]
            dict set found $name port [lindex $f 8]
            foreach {- t} [regexp -all -inline {"([^"]*)"} [join [lrange $f 9 end] ";"]] {
                if {[regexp {^([^=]+)=(.*)$} $t -> k v]} { dict set found $name $k $v }
            }
        } elseif {[regexp {^(\S+)\._dserv\._tcp\s+SRV\s+\d+\s+\d+\s+(\d+)\s+(\S+)} $line -> inst port host]} {
            # dns-sd -Z: zone-file lines, SRV then TXT for each instance
            set name [unescape $inst]
            dict set found $name name $name
            dict set found $name host [string trimright $host .]
            dict set found $name port $port
        } elseif {[regexp {^(\S+)\._dserv\._tcp\s+TXT\s+(.*)$} $line -> inst txt]} {
            set name [unescape $inst]
            foreach {- t} [regexp -all -inline {"([^"]*)"} $txt] {
                if {[regexp {^([^=]+)=(.*)$} $t -> k v]} { dict set found $name $k $v }
            }
        }
    }
    dict values $found
}

# DNS-SD escapes: \032 (decimal) and \.
proc mirror::unescape {s} {
    regsub -all {\\(\d\d\d)} $s {[format %c [scan \1 %d]]} s
    regsub -all {\\(.)} [subst -novariables -nobackslashes $s] {\1} s
    return $s
}

if {[info exists ::argv0] && [file tail $::argv0] eq "discover.tcl"} {
    foreach r [mirror::discover {*}$::argv] { puts $r }
}
