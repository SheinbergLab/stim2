#!/usr/bin/env dlsh
# render_server.tcl -- trial clips over HTTP, for a trial viewer.
#
#     dlsh tools/render/render_server.tcl ?options?
#
# Renders any trial of a recorded session (its .obs.dgz, else its .ess)
# into a frame-locked movie on a headless stim2 of its own, with the
# timing JSON that puts the clip on the trial's timeline
# (replay::render_trial, tools/lib/replaylib.tcl), caches it, and serves
# both.  A browser asks for a trial and gets the clip's URL and timing when
# it is ready; the neighbouring trials are rendered next, so stepping
# through a session mostly finds them waiting.  See docs/render.md.
#
#   GET /session?file=NAME            load a session: trial count, screen
#   GET /trial?file=NAME&trial=N      (or &obsid=K, 0-based) render if not
#                                     cached; {clip, cached, timing}
#   GET /clips/...                    the movies and JSON, with byte ranges
#   GET /status                       stim2, sessions, queue
#
# NAME is a session's base name, with or without .obs.dgz, .trials.dgz,
# .ess: a viewer holding a trials DG passes its filename column.  It is
# looked for in the -data directories (default ~/data/obs, ~/data/ess,
# /usr/local/dserv/work/obs).
#
#   -port P        HTTP port (default 8140)
#   -host A        address to listen on (default 127.0.0.1; 0.0.0.0 for
#                  other machines)
#   -data DIR      a directory to look for sessions in (repeatable; replaces
#                  the defaults)
#   -cache DIR     where clips go (default ~/.cache/stim2-render)
#   -width W       clip width in pixels (default 960); the height follows
#                  the rig's aspect
#   -prefetch N    trials after the one asked for to render ahead (default 2)
#   -stim2 PATH    the stim2 to launch (default: as replay.tcl -launch)
#   -stim2-port P  its message port (default 5652: --port-base 5650)
#   -sessions N    sessions kept in memory (default 3)
#
# One stim2, one clip at a time: rendering runs in a coroutine that yields
# every 30 rig frames, so the server keeps answering (clips, status, new
# requests) while a trial renders.

source [file join [file dirname [file normalize [info script]]] .. lib replaylib.tcl]

# ---------------------------------------------------------------------------
# options
# ---------------------------------------------------------------------------
set opt(port) 8140
set opt(host) 127.0.0.1
set opt(data) {}
set opt(cache) [file join $::env(HOME) .cache stim2-render]
set opt(width) 960
set opt(prefetch) 2
set opt(stim2) ""
set opt(stim2port) 5652
set opt(sessions) 3
set args $argv
while {[llength $args]} {
    set a [lindex $args 0]; set v [lindex $args 1]; set args [lrange $args 2 end]
    switch -- $a {
        -port       { set opt(port) $v }
        -host       { set opt(host) $v }
        -data       { lappend opt(data) [file normalize $v] }
        -cache      { set opt(cache) [file normalize $v] }
        -width      { set opt(width) $v }
        -prefetch   { set opt(prefetch) $v }
        -stim2      { set opt(stim2) $v }
        -stim2-port { set opt(stim2port) $v }
        -sessions   { set opt(sessions) $v }
        default     { puts stderr "unknown option $a"; exit 2 }
    }
}
if {![llength $opt(data)]} {
    # (Tcl 9 does not expand ~)
    foreach d [list $::env(HOME)/data/obs $::env(HOME)/data/ess /usr/local/dserv/work/obs] {
        set d [file normalize $d]
        if {[file isdirectory $d]} { lappend opt(data) $d }
    }
}
file mkdir $opt(cache)

# the clip format; a change here starts a fresh cache
set clip_version 1

proc log {msg} { puts "[clock format [clock seconds] -format %H:%M:%S] $msg"; flush stdout }

# ---------------------------------------------------------------------------
# sessions: name -> the file, loaded once, kept while recently used
# ---------------------------------------------------------------------------
set sessions [dict create]   ;# base -> dict S path mtime size used

# the file for NAME: base.obs.dgz in a data dir, else base.ess
proc resolve {name} {
    global opt
    if {![regexp {^[A-Za-z0-9._-]+$} $name]} { http_error 400 "bad file name: $name" }
    regsub {\.(trials\.dgz|obs\.dgz|ess|dgz)$} $name {} base
    foreach ext {.obs.dgz .ess} {
        foreach d $opt(data) {
            set p [file join $d $base$ext]
            if {[file isfile $p]} { return [list $base $p] }
        }
    }
    http_error 404 "no $base.obs.dgz or $base.ess in [join $opt(data) {, }]"
}

proc session {name} {
    global sessions opt current
    lassign [resolve $name] base path
    set mtime [file mtime $path]; set size [file size $path]
    if {[dict exists $sessions $base]} {
        set s [dict get $sessions $base]
        if {[dict get $s path] eq $path && [dict get $s mtime] == $mtime && [dict get $s size] == $size} {
            dict set sessions $base used [clock milliseconds]
            return [dict get $sessions $base]
        }
        replay::delete [dict get $s S]
        dict unset sessions $base
    }
    # make room: drop the least recently used, never the one rendering now
    while {[dict size $sessions] >= $opt(sessions)} {
        set old ""; set t ""
        dict for {b s} $sessions {
            if {[string match $b/* $current]} continue
            if {$t eq "" || [dict get $s used] < $t} { set old $b; set t [dict get $s used] }
        }
        if {$old eq ""} break
        replay::delete [dict get $sessions $old S]
        dict unset sessions $old
        log "dropped session $old"
    }
    set t0 [clock milliseconds]
    if {[catch {replay::load $path} S]} { http_error 422 "$base: $S" }
    log "loaded $path ([replay::ntrials $S] trials) in [expr {[clock milliseconds] - $t0}] ms"
    dict set sessions $base [dict create S $S base $base path $path mtime $mtime size $size \
                                  used [clock milliseconds]]
    return [dict get $sessions $base]
}

# where a session's clips go: by the file's identity, the clip size and
# format and the stim2 that draws them
proc clip_dir {s} {
    global opt clip_version stim2
    lassign [clip_size $s] w h
    set v [regsub -all {[^A-Za-z0-9.]} [dict get $stim2 version] _]
    file join [dict get $s base] "c${clip_version}_${w}x${h}_[dict get $s mtime]_[dict get $s size]_s$v"
}

# the clip's pixel size: -width wide, the rig's aspect, even
proc clip_size {s} {
    global opt
    array set g [replay::get [dict get $s S] geom]
    set w $opt(width); set h [expr {$w * 9 / 16}]
    if {[info exists g(w)] && [info exists g(h)] && [string is double -strict $g(w)] \
            && [string is double -strict $g(h)] && $g(w) > 0 && $g(h) > 0} {
        set h [expr {int(round($w * double($g(h)) / $g(w)))}]
    }
    list $w [expr {$h + ($h % 2)}]
}

# ---------------------------------------------------------------------------
# the stim2: one, headless, relaunched when a session needs another size or
# refresh (the window must have the rig's aspect)
# ---------------------------------------------------------------------------
set stim2 [dict create pid "" sock "" size "" hz "" version ""]

proc stim2_stop {} {
    global stim2
    if {[dict get $stim2 sock] ne ""} { catch {close [dict get $stim2 sock]} }
    set pid [dict get $stim2 pid]
    if {$pid ne "" && [replay::alive $pid]} {
        catch {exec kill $pid}
        for {set i 0} {$i < 50 && [replay::alive $pid]} {incr i} { after 100 }
    }
    dict set stim2 pid ""; dict set stim2 sock ""
}

proc stim2_for {size geom} {
    global stim2 opt
    set hz [expr {[dict exists $geom hz] ? [dict get $geom hz] : ""}]
    if {[dict get $stim2 sock] ne "" && [dict get $stim2 size] eq $size && [dict get $stim2 hz] eq $hz \
            && [replay::alive [dict get $stim2 pid]]} {
        return [dict get $stim2 sock]
    }
    stim2_stop
    lassign $size w h
    set pid [replay::launch -port $opt(stim2port) -geom $geom -render 1 -size ${w}x${h} \
                 -exe $opt(stim2) -log log]
    set sock [replay::connect localhost $opt(stim2port) $pid]
    replay::check_stimess $sock
    set ver [framelock::send $sock {catch {stimVersion} v; set v}]
    dict set stim2 pid $pid; dict set stim2 sock $sock; dict set stim2 size $size; dict set stim2 hz $hz
    if {[dict get $stim2 version] eq ""} { dict set stim2 version $ver }
    return $sock
}

# ---------------------------------------------------------------------------
# jobs: one per clip, rendered in turn by the worker coroutine.  A request
# goes to the front of the queue; prefetches go to the back and are dropped
# when a request for somewhere else comes in.
# ---------------------------------------------------------------------------
set queue {}                 ;# job keys, next first
set jobs [dict create]       ;# key -> dict base trial status waiters progress error
set worker_idle 1
set current ""

proc job_key {base trial} { return $base/$trial }

proc clip_paths {s trial} {
    global opt
    set dir [file join $opt(cache) [clip_dir $s]]
    set f [format t%03d $trial]
    list $dir $dir/$f.mp4 $dir/$f.json /clips/[clip_dir $s]/$f.mp4
}

proc cached {s trial} { file isfile [lindex [clip_paths $s $trial] 2] }

proc want {s trial {chan ""}} {
    global queue jobs worker_idle current
    set key [job_key [dict get $s base] $trial]
    if {$chan ne ""} {
        # a request: the queued prefetches for elsewhere make way
        set keep {}
        foreach k $queue {
            if {$k eq $key || [llength [dict get $jobs $k waiters]]} { lappend keep $k } else { dict unset jobs $k }
        }
        set queue $keep
    }
    if {![dict exists $jobs $key]} {
        dict set jobs $key [dict create base [dict get $s base] trial $trial status queued \
                                waiters {} progress 0 error ""]
        if {$chan ne ""} { set queue [linsert $queue 0 $key] } else { lappend queue $key }
    } elseif {$chan ne "" && $key ne $current} {
        set i [lsearch -exact $queue $key]
        if {$i > 0} { set queue [linsert [lreplace $queue $i $i] 0 $key] }
    }
    if {$chan ne ""} { dict with jobs $key { lappend waiters $chan } }
    if {$worker_idle} { set worker_idle 0; after 0 worker }
}

proc work {} {
    global queue jobs worker_idle current sessions opt
    while 1 {
        if {![llength $queue]} { set current ""; set worker_idle 1; yield; continue }
        set key [lindex $queue 0]; set queue [lrange $queue 1 end]
        set current $key
        set job [dict get $jobs $key]
        set base [dict get $job base]; set trial [dict get $job trial]
        set t0 [clock milliseconds]
        set err ""
        if {![dict exists $sessions $base]} {
            set err "session $base is no longer loaded"
        } else {
            set s [dict get $sessions $base]
            lassign [clip_paths $s $trial] dir mp4 json url
            if {![file isfile $json]} {
                dict set jobs $key status rendering
                file mkdir $dir
                set tmp [file rootname $mp4].part
                if {[catch {
                    set sock [stim2_for [clip_size $s] [replay::get [dict get $s S] geom]]
                    set r [replay::render_trial [dict get $s S] $sock $trial $tmp.mp4 -json $tmp.json \
                               -progress [list progress $key] -log log]
                    if {[dict get $r errors]} { log "$key: [dict get $r errors] errors while rendering" }
                    if {[dict get $r bad]} { error "the movie writer failed: [dict get $r status]" }
                    file rename -force $tmp.mp4 $mp4
                    file rename -force $tmp.json $json
                    log [format "rendered %s: %d rig frames in %.2f s (%d ms in all)" $key \
                             [dict get $r drawn] [dict get $r elapsed] [expr {[clock milliseconds] - $t0}]]
                } e opts]} {
                    set err $e
                    log "FAILED $key: $e"
                    if {[lindex [dict get $opts -errorcode] 0] ne "REPLAY"} {
                        # the stim2 may have gone: start a fresh one next time
                        stim2_stop
                    }
                    file delete $tmp.mp4 $tmp.json
                }
            }
        }
        set waiters [dict get $jobs $key waiters]
        dict unset jobs $key
        foreach chan $waiters {
            if {$err ne ""} { catch { respond_json $chan 500 [json_obj [list error [replay::_jstr $err]]] } } \
            else { catch { respond_trial $chan $s $trial 0 } }
        }
    }
}
coroutine worker work

# called from inside the render: let the event loop run, then go on
proc progress {key done of} {
    global jobs
    if {[dict exists $jobs $key]} { dict set jobs $key progress [expr {double($done) / $of}] }
    after 0 [info coroutine]
    yield
}

# ---------------------------------------------------------------------------
# HTTP: one request per connection
# ---------------------------------------------------------------------------
proc json_obj {pairs} { replay::_jobj $pairs }

proc http_error {code msg} { return -code error -errorcode [list HTTP $code] $msg }

proc headers {code ctype len {extra {}}} {
    set reason [dict get {200 OK 204 {No Content} 206 {Partial Content} 400 {Bad Request} 404 {Not Found}
        405 {Method Not Allowed} 416 {Range Not Satisfiable} 422 {Unprocessable Entity}
        500 {Internal Server Error}} $code]
    set h "HTTP/1.1 $code $reason\r\n"
    append h "Content-Type: $ctype\r\nContent-Length: $len\r\n"
    append h "Access-Control-Allow-Origin: *\r\n"
    append h "Access-Control-Expose-Headers: Content-Range, Content-Length, Accept-Ranges\r\n"
    append h "Connection: close\r\n"
    foreach {k v} $extra { append h "$k: $v\r\n" }
    append h "\r\n"
}

proc respond_json {chan code body} {
    set b [encoding convertto utf-8 $body]
    fconfigure $chan -translation binary -blocking 1
    puts -nonewline $chan [headers $code application/json [string length $b] {Cache-Control no-store}]$b
    close $chan
}

proc respond_trial {chan s trial cached} {
    lassign [clip_paths $s $trial] dir mp4 json url
    set fd [open $json r]; set timing [read $fd]; close $fd
    respond_json $chan 200 [json_obj [list clip [replay::_jstr $url] cached [expr {$cached ? "true" : "false"}] \
                                         timing $timing]]
}

proc serve_file {chan path method range} {
    set size [file size $path]
    set ctype [expr {[string match *.mp4 $path] ? "video/mp4" : "application/json"}]
    set extra {Accept-Ranges bytes Cache-Control max-age=3600}
    set from 0; set to [expr {$size - 1}]; set code 200
    if {$range ne ""} {
        if {![regexp {^bytes=(\d*)-(\d*)$} $range -> a b] || ($a eq "" && $b eq "")} {
            http_error 416 "bad range $range"
        }
        if {$a eq ""} { set from [expr {max(0, $size - $b)}] } else { set from $a; if {$b ne ""} { set to [expr {min($b, $size - 1)}] } }
        if {$from > $to || $from >= $size} {
            fconfigure $chan -translation binary -blocking 1
            puts -nonewline $chan [headers 416 text/plain 0 [list Content-Range "bytes */$size"]]
            close $chan; return
        }
        set code 206
        lappend extra Content-Range "bytes $from-$to/$size"
    }
    set len [expr {$to - $from + 1}]
    fconfigure $chan -translation binary -blocking 0
    puts -nonewline $chan [headers $code $ctype $len $extra]
    if {$method eq "HEAD"} { flush $chan; close $chan; return }
    set fd [open $path rb]
    seek $fd $from
    fcopy $fd $chan -size $len -command [list apply {{fd chan args} { close $fd; catch {close $chan} }} $fd $chan]
}

proc query {q} {
    set d [dict create]
    foreach kv [split $q &] {
        lassign [split $kv =] k v
        dict set d [urldecode $k] [urldecode $v]
    }
    return $d
}
proc urldecode {s} {
    # quote what subst would act on, then let it run only the %XX formats
    set s [string map {+ " " \\ \\\\ [ \\[ ] \\] $ \\$} $s]
    subst [regsub -all {%([0-9A-Fa-f]{2})} $s {[format %c 0x\1]}]
}

proc accept {chan addr port} {
    fconfigure $chan -blocking 0 -translation {auto crlf} -encoding iso8859-1
    fileevent $chan readable [list read_request $chan {}]
}

proc read_request {chan lines} {
    if {[catch {gets $chan line} n] || ($n < 0 && [eof $chan])} { catch {close $chan}; return }
    if {$n < 0} return
    if {$line ne ""} {
        lappend lines $line
        if {[llength $lines] > 100} { catch {close $chan} }
        fileevent $chan readable [list read_request $chan $lines]
        return
    }
    fileevent $chan readable {}
    set req [lindex $lines 0]
    set hdr [dict create]
    foreach l [lrange $lines 1 end] {
        if {[regexp {^([^:]+):\s*(.*)$} $l -> k v]} { dict set hdr [string tolower $k] $v }
    }
    if {[catch {handle $chan $req $hdr} e opts]} {
        set ec [dict get $opts -errorcode]
        set code [expr {[lindex $ec 0] eq "HTTP" ? [lindex $ec 1] : 500}]
        if {$code == 500} { log "ERROR $req: [dict get $opts -errorinfo]" }
        catch { respond_json $chan $code [json_obj [list error [replay::_jstr $e]]] }
    }
}

proc handle {chan req hdr} {
    global opt stim2 sessions queue jobs current
    lassign $req method target
    if {$method eq "OPTIONS"} {
        fconfigure $chan -translation binary -blocking 1
        puts -nonewline $chan [headers 204 text/plain 0 {Access-Control-Allow-Methods {GET, HEAD, OPTIONS} Access-Control-Allow-Headers Range}]
        close $chan; return
    }
    if {$method ni {GET HEAD}} { http_error 405 "$method not supported" }
    set q ""
    regexp {^([^?]*)\??(.*)$} $target -> path q
    set q [query $q]
    switch -glob -- $path {
        /clips/* {
            set rel [string range $path 7 end]
            set p [file normalize [file join $opt(cache) $rel]]
            if {[string first [file normalize $opt(cache)]/ $p] != 0 || ![regexp {\.(mp4|json)$} $p]
                    || ![file isfile $p]} {
                http_error 404 "no clip $rel"
            }
            serve_file $chan $p $method [expr {[dict exists $hdr range] ? [dict get $hdr range] : ""}]
        }
        /session {
            if {![dict exists $q file]} { http_error 400 "/session wants file=NAME" }
            set s [session [dict get $q file]]
            set S [dict get $s S]
            set scr {}
            foreach {k v} [replay::get $S geom] {
                if {$k eq "bg"} { lappend scr bg [replay::_jarr $v] } elseif {[string is double -strict $v]} { lappend scr $k $v }
            }
            lassign [clip_size $s] w h
            respond_json $chan 200 [json_obj [list file [replay::_jstr [dict get $s base]] \
                path [replay::_jstr [dict get $s path]] ntrials [replay::ntrials $S] \
                screen [json_obj $scr] width $w height $h]]
        }
        /trial {
            if {![dict exists $q file]} { http_error 400 "/trial wants file=NAME and trial=N or obsid=K" }
            if {[dict exists $q trial]} { set trial [dict get $q trial] } \
            elseif {[dict exists $q obsid]} { set trial [expr {[dict get $q obsid] + 1}] } \
            else { http_error 400 "/trial wants trial=N (1-based) or obsid=K (0-based)" }
            if {![string is integer -strict $trial]} { http_error 400 "bad trial $trial" }
            set s [session [dict get $q file]]
            set n [replay::ntrials [dict get $s S]]
            if {$trial < 1 || $trial > $n} { http_error 400 "trial $trial out of range: the file has $n" }
            if {[cached $s $trial]} {
                respond_trial $chan $s $trial 1
            } else {
                want $s $trial $chan
            }
            for {set k 1} {$k <= $opt(prefetch)} {incr k} {
                set t [expr {$trial + $k}]
                if {$t <= $n && ![cached $s $t]} { want $s $t }
            }
        }
        /status {
            set ss {}
            dict for {b s} $sessions { lappend ss [replay::_jstr $b] }
            set qs {}
            foreach k $queue { lappend qs [replay::_jstr $k] }
            set cur [expr {$current eq "" ? "null" : [json_obj [list job [replay::_jstr $current] \
                progress [format %.2f [expr {[dict exists $jobs $current] ? [dict get $jobs $current progress] : 0}]]]]}]
            respond_json $chan 200 [json_obj [list \
                stim2 [json_obj [list pid [expr {[dict get $stim2 pid] eq "" ? "null" : [dict get $stim2 pid]}] \
                    version [replay::_jstr [dict get $stim2 version]] size [replay::_jstr [dict get $stim2 size]]]] \
                sessions [replay::_jarr $ss] rendering $cur queue [replay::_jarr $qs] \
                cache [replay::_jstr $opt(cache)] data [replay::_jarr [lmap d $opt(data) {replay::_jstr $d}]]]]
        }
        / {
            respond_json $chan 200 [json_obj [list service [replay::_jstr "stim2 trial renderer"] \
                endpoints [replay::_jarr [lmap e {/session?file=NAME /trial?file=NAME&trial=N /trial?file=NAME&obsid=K /status /clips/...} {replay::_jstr $e}]]]]
        }
        default { http_error 404 "no such endpoint $path" }
    }
}

# ---------------------------------------------------------------------------
# start: the stim2 first (its version names the cache), then listen
# ---------------------------------------------------------------------------
rename exit _exit
proc exit {{code 0}} { stim2_stop; _exit $code }

if {[catch {stim2_for [list $opt(width) [expr {$opt(width) * 9 / 16 + ($opt(width) * 9 / 16) % 2}]] {}} e]} {
    puts stderr "could not start a stim2: $e"; exit 1
}
log "stim2 [dict get $stim2 version] (pid [dict get $stim2 pid]) on port $opt(stim2port)"
if {[catch {socket -server accept -myaddr $opt(host) $opt(port)} e]} {
    puts stderr "cannot listen on $opt(host):$opt(port): $e"; exit 1
}
log "listening on http://$opt(host):$opt(port)/  data: [join $opt(data) {, }]  cache: $opt(cache)"
vwait forever
