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
#                                     cached; {clip, cached, timing}.
#                                     &then=K,K: obsids to render next
#                                     (default: the next -prefetch trials)
#   GET /clips/...                    the movies and JSON, with byte ranges
#   GET /status                       stim2, sessions, queue
#
# NAME is a session's base name, with or without .obs.dgz, .trials.dgz,
# .ess: a viewer holding a trials DG passes its filename column.  It is
# looked for in the -data directories (default ~/data/obs, ~/data/ess,
# /usr/local/dserv/work/obs), then on each -dserv: its /data/obs/ route,
# fetched into the cache in the background (curl) and checked again (HEAD)
# at most every 30 s.  So a render host on the lab network serves a rig
# whose dserv is elsewhere, e.g. a Pi with a stim box on its own network.
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
#   -dserv URL     a dserv to fetch sessions from, e.g. http://rpi500.local:2565
#                  (repeatable)
#   -guard H:P     the dserv text port whose ess/status holds rendering while
#                  it is "running" (default localhost:2570, this machine's
#                  dserv; "none" to render regardless)
#   -cache-max MB  the cache's size: past it, the clips used longest ago go
#                  (default 2000; 0 for no limit)
#
# One stim2, one clip at a time: rendering runs in a coroutine that yields
# every 10 rig frames, so the server keeps answering (clips, status, new
# requests) while a trial renders.  While the rig runs an experiment no
# clip is rendered: a viewer asking for one gets a 503, a render under way
# waits, and the queue goes on when the experiment stops.  Cached clips are
# still served.

source [file join [file dirname [file normalize [info script]]] .. lib replaylib.tcl]

# ---------------------------------------------------------------------------
# options
# ---------------------------------------------------------------------------
set opt(port) 8140
set opt(host) 127.0.0.1
set opt(data) {}
# (a service may run with no HOME)
set home [expr {[info exists ::env(HOME)] ? $::env(HOME) : ""}]
set opt(cache) [expr {$home ne "" ? [file join $home .cache stim2-render] : "/var/cache/stim2-render"}]
set opt(guard) localhost:2570
set opt(cachemax) 2000
set opt(width) 960
set opt(prefetch) 2
set opt(stim2) ""
set opt(stim2port) 5652
set opt(sessions) 3
set opt(dserv) {}
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
        -dserv      {
            if {![regexp {^https?://[A-Za-z0-9.:\[\]-]+$} [string trimright $v /]]} {
                puts stderr "-dserv wants http(s)://host:port, not $v"; exit 2
            }
            lappend opt(dserv) [string trimright $v /]
        }
        -guard      {
            if {$v ne "none" && ![regexp {^[A-Za-z0-9.-]+:\d+$} $v]} {
                puts stderr "-guard wants host:port or none, not $v"; exit 2
            }
            set opt(guard) $v
        }
        -cache-max  {
            if {![string is integer -strict $v] || $v < 0} { puts stderr "-cache-max wants MB, not $v"; exit 2 }
            set opt(cachemax) $v
        }
        default     { puts stderr "unknown option $a"; exit 2 }
    }
}
if {![llength $opt(data)]} {
    # (Tcl 9 does not expand ~)
    set dirs [list /usr/local/dserv/work/obs]
    if {$home ne ""} { set dirs [linsert $dirs 0 $home/data/obs $home/data/ess] }
    foreach d $dirs {
        set d [file normalize $d]
        if {[file isdirectory $d]} { lappend opt(data) $d }
    }
}
file mkdir $opt(cache)

# the clip format; a change here starts a fresh cache
set clip_version 2

proc log {msg} { puts "[clock format [clock seconds] -format %H:%M:%S] $msg"; flush stdout }

# ---------------------------------------------------------------------------
# sessions: name -> the file, loaded once, kept while recently used
# ---------------------------------------------------------------------------
set sessions [dict create]   ;# base -> dict S path mtime size used

# the file for NAME: base.obs.dgz in a data dir, else base.ess, else a
# dserv's copy (remote, which may wait on a download: call from a coroutine)
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
    set p [remote $base]
    if {$p ne ""} { return [list $base $p] }
    set where [concat $opt(data) [lmap u $opt(dserv) {set u $u/data/obs/}]]
    http_error 404 "no $base.obs.dgz or $base.ess in [join $where {, }]"
}

# ---------------------------------------------------------------------------
# sessions from a dserv: /data/obs/NAME.obs.dgz copied into the cache
# (curl -R keeps the dserv file's mtime, which names the clips' directory)
# ---------------------------------------------------------------------------
set checked [dict create]     ;# base -> {when path}: the last HEAD check
set fetching [dict create]    ;# base -> coroutines waiting on its download

proc remote_path {url base} {
    global opt
    regsub -all {[^A-Za-z0-9.-]} [regsub {^https?://} $url {}] _ host
    file join $opt(cache) remote $host $base.obs.dgz
}

# run a command without holding the event loop: {exit-code output}
proc run_async {args} {
    set co [info coroutine]
    if {$co eq ""} { error "run_async needs a coroutine" }
    set fd [open |[list {*}$args 2>@1] r]
    fconfigure $fd -blocking 0 -translation binary
    fileevent $fd readable [list $co]
    set out ""
    while 1 {
        yield
        append out [read $fd]
        if {[eof $fd]} break
    }
    fileevent $fd readable {}
    fconfigure $fd -blocking 1
    set code 0
    if {[catch {close $fd} e opts]} {
        set ec [dict get $opts -errorcode]
        set code [expr {[lindex $ec 0] eq "CHILDSTATUS" ? [lindex $ec 2] : 1}]
    }
    list $code $out
}

# curl's options for a dserv URL.  -4 unless the host is an IPv6 literal:
# macOS waits 5 s on an IPv6 lookup of a rig's NAME.local that never
# answers (rpi500.local: 5 s, against 4 ms with -4)
proc curl_opts {u} {
    expr {[string match {*://\[*} $u] ? {-s -k} : {-s -k -4}}
}

# HEAD: {status length mtime} (0 when it did not answer; "" for what the
# dserv did not say)
proc remote_head {u} {
    lassign [run_async curl {*}[curl_opts $u] -I --max-time 5 -w "\nSTATUS %{http_code}\n" $u] code out
    set status 0; set len ""; set mtime ""
    foreach line [split [string map {\r ""} $out] \n] {
        if {[regexp {^STATUS (\d+)$} $line -> s]} { set status $s }
        if {[regexp -nocase {^content-length:\s*(\d+)} $line -> v]} { set len $v }
        if {[regexp -nocase {^last-modified:\s*(.+)$} $line -> v]} {
            catch { set mtime [clock scan [string trim $v] -format {%a, %d %b %Y %H:%M:%S GMT} -gmt 1] }
        }
    }
    list $status $len $mtime
}

# the local copy of base from a -dserv, fetched or refreshed as needed; ""
# when none has it
proc remote {base} {
    global opt checked fetching
    if {![llength $opt(dserv)]} { return "" }
    # one download per session: later askers wait for the first
    if {[dict exists $fetching $base]} {
        dict lappend fetching $base [info coroutine]
        yield
        return [remote $base]
    }
    # checked lately: the copy stands
    if {[dict exists $checked $base]} {
        lassign [dict get $checked $base] when path
        if {[clock seconds] - $when < 30 && [file isfile $path]} { return $path }
    }
    dict set fetching $base {}
    try {
        foreach url $opt(dserv) {
            set u $url/data/obs/$base.obs.dgz
            set path [remote_path $url $base]
            set have [file isfile $path]
            lassign [remote_head $u] status len mtime
            if {$status == 200 && $have
                    && ($len eq "" || $len == [file size $path])
                    && ($mtime eq "" || $mtime == [file mtime $path])} {
                dict set checked $base [list [clock seconds] $path]
                return $path                                   ;# current
            }
            if {$status == 0} {
                if {!$have} continue                           ;# unreachable
                log "$url did not answer: using the copy of $base from before"
                dict set checked $base [list [clock seconds] $path]
                return $path
            }
            # new or changed (200), or a dserv without HEAD: GET decides
            file mkdir [file dirname $path]
            set tmp $path.part
            set t0 [clock milliseconds]
            lassign [run_async curl {*}[curl_opts $u] -f -R --max-time 600 -o $tmp $u] code out
            if {$code != 0 || ![file isfile $tmp]} {
                file delete $tmp
                if {$have} {
                    dict set checked $base [list [clock seconds] $path]
                    return $path                               ;# keep what we have
                }
                continue                                       ;# not there
            }
            if {$have && $mtime eq "" && [file size $tmp] == [file size $path]
                    && [lindex [run_async cmp -s $tmp $path] 0] == 0} {
                # the same bytes (a dserv that sends no Last-Modified): keep
                # the copy and its mtime, which names its clips
                file delete $tmp
                dict set checked $base [list [clock seconds] $path]
                return $path
            }
            file rename -force $tmp $path
            log [format "fetched %s (%.1f MB) from %s in %d ms" $base \
                     [expr {[file size $path] / 1048576.0}] $url [expr {[clock milliseconds] - $t0}]]
            dict set checked $base [list [clock seconds] $path]
            return $path
        }
        return ""
    } finally {
        set waiting [dict get $fetching $base]
        dict unset fetching $base
        foreach co $waiting { after 0 $co }
    }
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
    # no frame records, no clips: say so for the session (a 422, which a
    # viewer takes as "no clips here") rather than fail each trial's render
    if {![llength [replay::get $S frames]]} {
        set why [expr {[replay::get $S obs_frames_narrow]
            ? "its stim/frame records are empty: converted with the rig's df wide_datafiles off"
            : "it has no stim/frame records: recorded before stim2 kept them"}]
        replay::delete $S
        http_error 422 "$base: $why"
    }
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
# the guard: no rendering while the rig runs an experiment.  A render shares
# the machine with the rig's dserv (and, on a Pi with its display, with the
# rig's stim2): ess/status from -guard's dserv text port, asked at most once
# a second while there is work, and every 30 s while nothing answers there.
# ---------------------------------------------------------------------------
set guard [dict create status "" when 0 asking 0 answers ""]

# one question over the text port: the answer line, or "" when nothing
# answers within a second.  Call from a coroutine.
proc guard_ask {} {
    global opt
    regexp {^(.*):(\d+)$} $opt(guard) -> host port
    set co [info coroutine]
    if {[catch {socket -async $host $port} sock]} { return "" }
    set timer [after 1000 [list $co timeout]]
    try {
        fconfigure $sock -blocking 0 -translation {auto lf} -encoding utf-8
        fileevent $sock writable [list $co writable]
        if {[yield] ne "writable" || [fconfigure $sock -error] ne ""} { return "" }
        fileevent $sock writable {}
        puts $sock "dservGet ess/status"
        flush $sock
        fileevent $sock readable [list $co readable]
        while {[yield] eq "readable"} {
            if {[gets $sock line] >= 0} { return $line }
            if {[eof $sock]} break
        }
        return ""
    } finally {
        after cancel $timer
        catch {close $sock}
    }
}

# 1 while the rig runs an experiment.  Asks again when the last answer is
# old and the caller is a coroutine; otherwise goes by the last answer.
proc rig_busy {} {
    global opt guard
    if {$opt(guard) eq "none"} { return 0 }
    set every [expr {[dict get $guard answers] eq "0" ? 30000 : 1000}]
    if {[info coroutine] ne "" && ![dict get $guard asking]
            && [clock milliseconds] - [dict get $guard when] >= $every} {
        dict set guard asking 1
        if {[catch {guard_ask} s]} { set s "" }
        dict set guard asking 0
        dict set guard when [clock milliseconds]
        if {[string index $s 0] eq "!"} { set s "" }   ;# !TCL_ERROR: no ess here
        set answers [expr {$s ne ""}]
        if {$answers ne [dict get $guard answers]} {
            log [expr {$answers
                ? "the guard: no rendering while $opt(guard)'s ess/status is running"
                : "the guard: $opt(guard) has no ess/status, so it does not hold rendering"}]
            dict set guard answers $answers
        }
        set was [dict get $guard status]
        dict set guard status $s
        if {$s eq "running" && $was ne "running"} {
            log "the rig is running an experiment: no rendering until it stops"
        } elseif {$s ne "running" && $was eq "running"} {
            log "the rig's experiment stopped: rendering goes on"
        }
    }
    expr {[dict get $guard status] eq "running"}
}

# in the worker, before a render and every 10 rig frames of one: wait while
# the rig runs an experiment.  Viewers waiting on the clip are told (503)
# rather than kept waiting; the render goes on when the experiment stops.
proc hold_while_busy {key} {
    global jobs
    if {![rig_busy]} return
    if {[dict exists $jobs $key]} {
        foreach chan [dict get $jobs $key waiters] { catch { respond_busy $chan } }
        dict set jobs $key waiters {}
        dict set jobs $key status held
    }
    while {[rig_busy]} {
        after 1000 [info coroutine]
        yield
    }
    if {[dict exists $jobs $key]} { dict set jobs $key status rendering }
}

proc busy_message {} {
    return "the rig is running an experiment: clips are rendered when it stops"
}
proc respond_busy {chan} {
    respond_json $chan 503 [json_obj [list error [replay::_jstr [busy_message]] busy true]]
}

# ---------------------------------------------------------------------------
# the cache's size: past -cache-max, whatever was used longest ago goes.  A
# clip is its .mp4 and .json together, used when last served (respond_trial
# sets their mtime); a session copied from a dserv goes by its own mtime,
# and is kept while it is in memory.  Files being written are left alone.
# ---------------------------------------------------------------------------
proc cache_files {dir} {
    set out [glob -nocomplain -directory $dir -types f *]
    foreach d [glob -nocomplain -directory $dir -types d *] { lappend out {*}[cache_files $d] }
    return $out
}

proc trim_cache {} {
    global opt sessions
    if {$opt(cachemax) <= 0} return
    set limit [expr {$opt(cachemax) * 1048576}]
    set inuse [lmap s [dict values $sessions] {dict get $s path}]
    set units [dict create]   ;# unit -> {mtime size files}
    set total 0
    foreach f [cache_files $opt(cache)] {
        if {[string match *.part* [file tail $f]] || [catch {file stat $f st}]} continue
        incr total $st(size)
        set u [expr {[regexp {\.(mp4|json)$} $f] ? [file rootname $f] : $f}]
        if {[dict exists $units $u]} {
            lassign [dict get $units $u] m z fs
            dict set units $u [list [expr {max($m, $st(mtime))}] [expr {$z + $st(size)}] [lappend fs $f]]
        } else {
            dict set units $u [list $st(mtime) $st(size) [list $f]]
        }
    }
    if {$total <= $limit} return
    # down to 90%, so the next few clips do not each start a trim
    set goal [expr {$limit * 9 / 10}]
    set recent [expr {[clock seconds] - 60}]
    set n 0; set freed 0; set dirs {}
    foreach {u e} [lsort -stride 2 -index {1 0} -integer [dict get $units]] {
        if {$total <= $goal} break
        lassign $e m z fs
        if {$m > $recent || $u in $inuse} continue
        # the .json first: a clip without it is not taken as cached
        foreach f [concat [lsearch -all -inline $fs *.json] [lsearch -all -inline -not $fs *.json]] {
            catch {file delete $f}
        }
        incr total -$z; incr freed $z; incr n
        dict set dirs [file dirname $u] 1
    }
    # directories left empty, then their parents if those are too
    foreach d [dict keys $dirs] {
        while {$d ne $opt(cache) && [string first $opt(cache)/ $d] == 0
                && ![llength [glob -nocomplain -directory $d *]]} {
            catch {file delete $d}
            set d [file dirname $d]
        }
    }
    if {$n} {
        log [format "cache over %d MB: removed %d clips and copies (%.0f MB), %.0f MB left" \
                 $opt(cachemax) $n [expr {$freed / 1048576.0}] [expr {$total / 1048576.0}]]
    }
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
                hold_while_busy $key
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
                    trim_cache
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

# called from inside the render: let the event loop run, then go on.  A
# clip being sent goes first (a viewer is waiting on it, while the render is
# usually a prefetch): the render waits for it, up to 100 ms a turn, so a
# slow client cannot stall the rendering
set sending 0
proc progress {key done of} {
    global jobs sending
    if {[dict exists $jobs $key]} { dict set jobs $key progress [expr {double($done) / $of}] }
    hold_while_busy $key
    set until [expr {[clock milliseconds] + 100}]
    after 0 [info coroutine]
    yield
    while {$sending > 0 && [clock milliseconds] < $until} {
        after 2 [info coroutine]
        yield
    }
}

# ---------------------------------------------------------------------------
# HTTP: one request per connection
# ---------------------------------------------------------------------------
proc json_obj {pairs} { replay::_jobj $pairs }

proc http_error {code msg} { return -code error -errorcode [list HTTP $code] $msg }

proc headers {code ctype len {extra {}}} {
    set reason [dict get {200 OK 204 {No Content} 206 {Partial Content} 400 {Bad Request} 404 {Not Found}
        405 {Method Not Allowed} 416 {Range Not Satisfiable} 422 {Unprocessable Entity}
        500 {Internal Server Error} 503 {Service Unavailable}} $code]
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
    # used now: the cache's trim goes by this
    set now [clock seconds]
    catch { file mtime $mp4 $now; file mtime $json $now }
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
    # large buffers: during a render the event loop runs only between
    # batches of rig frames, and fcopy moves one buffer per turn
    fconfigure $fd -buffersize 1048576
    fconfigure $chan -buffersize 1048576
    incr ::sending
    fcopy $fd $chan -size $len -command [list apply {{fd chan args} {
        incr ::sending -1; close $fd; catch {close $chan}
    }} $fd $chan]
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
    # every line there is, each time: during a render the event loop turns
    # only between batches of rig frames
    while 1 {
        if {[catch {gets $chan line} n] || ($n < 0 && [eof $chan])} { catch {close $chan}; return }
        if {$n < 0} { fileevent $chan readable [list read_request $chan $lines]; return }
        if {$line eq ""} break
        lappend lines $line
        if {[llength $lines] > 100} { catch {close $chan}; return }
    }
    fileevent $chan readable {}
    set req [lindex $lines 0]
    set hdr [dict create]
    foreach l [lrange $lines 1 end] {
        if {[regexp {^([^:]+):\s*(.*)$} $l -> k v]} { dict set hdr [string tolower $k] $v }
    }
    # each request in a coroutine of its own: one waiting on a download
    # from a dserv lets everything else go on
    coroutine ::request[incr ::nrequest] serve_request $chan $req $hdr
}
set nrequest 0

proc serve_request {chan req hdr} {
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
            } elseif {[rig_busy]} {
                # nothing rendered or queued during an experiment
                respond_busy $chan
                return
            } else {
                want $s $trial $chan
            }
            # what to render next: the client's then=K,K,... (obsids, the
            # rows it will step to; some obs periods are not trials), else
            # the next -prefetch trials
            if {[dict exists $q then]} {
                set next {}
                foreach k [lrange [split [dict get $q then] ,] 0 [expr {$opt(prefetch) - 1}]] {
                    if {[string is integer -strict $k]} { lappend next [expr {$k + 1}] }
                }
            } else {
                set next {}
                for {set k 1} {$k <= $opt(prefetch)} {incr k} { lappend next [expr {$trial + $k}] }
            }
            foreach t $next {
                if {$t >= 1 && $t <= $n && ![cached $s $t]} { want $s $t }
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
                cache [replay::_jstr $opt(cache)] data [replay::_jarr [lmap d $opt(data) {replay::_jstr $d}]] \
                dserv [replay::_jarr [lmap d $opt(dserv) {replay::_jstr $d}]] \
                fetching [replay::_jarr [lmap b [dict keys $::fetching] {replay::_jstr $b}]] \
                guard [json_obj [list target [replay::_jstr $opt(guard)] \
                    busy [expr {[rig_busy] ? "true" : "false"}] \
                    status [replay::_jstr [dict get $::guard status]]]] \
                cache_max_mb $opt(cachemax)]]
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
if {[llength $opt(dserv)]} { log "sessions also from: [join $opt(dserv) {, }]" }
if {$opt(guard) eq "none"} { log "the guard is off: rendering regardless of any experiment" } \
else { coroutine ::guard_first rig_busy }   ;# logs whether it holds rendering
trim_cache
vwait forever
