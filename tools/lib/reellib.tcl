# reellib.tcl -- several trials, title cards and captions into one movie
#
#     source tools/lib/replaylib.tcl
#     source tools/lib/reellib.tcl
#     reel::render $spec out.mp4 -sock $proc
#
# A reel is a list of items, each rendered by stim2 into a movie segment of
# its own -- a trial frame-locked from its session (replay::render_trial),
# with the gaze and a caption drawn on stim2's overlay layer; a card, text
# over the rig's background -- all at one size, frame rate and x264
# setting, then joined.  Segments that match (the usual case: one rig) are
# joined without encoding them again (ffmpeg's concat demuxer, -c copy);
# otherwise once, each scaled and padded to the first one's size.
#
# The spec is a Tcl dict:
#   fps F           the reel's frame rate (default 60): every segment's
#   crf N           x264 quality (default 14)
#   items {ITEM ...}
# and each ITEM a dict:
#   type trial  S <session (replay::load)>  trial N (1-based)
#               ?caption TEXT?  ?caption_at nw|n|ne|sw|s|se (default nw)?
#               ?caption_size FRAC (of the screen's height; default 0.045)?
#               ?gaze {T X Y}? (render_trial -gaze; none drawn without it)
#               ?gaze_style {SIZE {R G B A}}?
#   type card   text TEXT  ?dur SECONDS (default 2)?
#               ?size FRAC (of the height, a line; default 0.07)?
#               ?color {R G B ?A?}? (default white)  ?bg {R G B}? (default
#               the rig's background, else black)  ?S <session>? (the
#               stim2 to draw on: the reel's nearest trial's by default)
#
# Text goes to stim2 as list elements, never through substitution, so a
# caption is only ever text.  reel::from_wire checks the JSON form (the
# render server's POST /export, docs/render.md) and turns it into this
# spec.
#
# Options:
#   -sock CMD       {*}CMD S: the stim2 for session S (set up at its size);
#                   required
#   -workdir DIR    where segments go (default OUT's directory, .reel_*)
#   -run CMD        {*}CMD argv...: run a command, return {code output}
#                   (default reel::run_sync; a server passes its own, which
#                   yields while it runs)
#   -progress CMD   {*}CMD done of: called as items are drawn
#   -log CMD
# Returns a dict: out, segments, copied (1 when joined without encoding),
# seconds.

namespace eval reel {}

proc reel::say {msg} { puts "  $msg" }

proc reel::run_sync {args} {
    set code 0
    if {[catch {exec {*}$args 2>@1} out opts]} {
        set ec [dict get $opts -errorcode]
        set code [expr {[lindex $ec 0] eq "CHILDSTATUS" ? [lindex $ec 2] : 1}]
    }
    list $code $out
}

proc reel::_get {d key default} {
    expr {[dict exists $d $key] ? [dict get $d $key] : $default}
}

proc reel::render {spec out args} {
    array set o {-sock "" -workdir "" -run reel::run_sync -progress "" -log reel::say}
    foreach {k v} $args {
        if {![info exists o($k)]} { error "reel::render: unknown option $k" }
        set o($k) $v
    }
    if {$o(-sock) eq ""} { error "reel::render: -sock CMD is required" }
    set fps [_get $spec fps 60]
    set crf [_get $spec crf 14]
    set items [_get $spec items {}]
    if {![llength $items]} { error "reel::render: no items" }
    set out [file normalize $out]
    set work $o(-workdir)
    if {$work eq ""} { set work [file join [file dirname $out] .reel_[pid]_[clock milliseconds]] }
    file mkdir $work

    # the session each card draws on: its own, else the nearest trial's
    set sess {}
    foreach it $items { lappend sess [expr {[_get $it type ""] eq "trial" ? [dict get $it S] : [_get $it S ""]}] }
    for {set i 0} {$i < [llength $items]} {incr i} {
        if {[lindex $sess $i] ne ""} continue
        for {set d 1} {$d < [llength $items]} {incr d} {
            foreach j [list [expr {$i + $d}] [expr {$i - $d}]] {
                if {$j >= 0 && $j < [llength $items] && [lindex $sess $j] ne ""} { lset sess $i [lindex $sess $j]; break }
            }
            if {[lindex $sess $i] ne ""} break
        }
        if {[lindex $sess $i] eq ""} { error "reel::render: a card needs a trial in the reel, or its own S" }
    }

    set t0 [clock milliseconds]
    set n [llength $items]
    set segs {}
    for {set i 0} {$i < $n} {incr i} {
        set it [lindex $items $i]
        set S [lindex $sess $i]
        set seg [file join $work [format seg%03d.mp4 $i]]
        set sock [{*}$o(-sock) $S]
        if {[_get $it type ""] eq "card" || [_get $it caption ""] ne ""} { need_text $sock }
        set prog {}
        if {$o(-progress) ne ""} {
            set prog [list apply {{cb i n done of} { {*}$cb [expr {$i + double($done) / $of}] $n }} $o(-progress) $i $n]
        }
        switch -- [_get $it type ""] {
            trial {
                set trial [dict get $it trial]
                set opts [list -fps $fps -crf $crf -keyint "" -log $o(-log)]
                if {$prog ne ""} { lappend opts -progress $prog }
                if {[_get $it gaze ""] ne ""} { lappend opts -gaze [dict get $it gaze] }
                if {[_get $it gaze_style ""] ne ""} { lappend opts -gaze-style [dict get $it gaze_style] }
                set cap [_get $it caption ""]
                if {$cap ne ""} {
                    lappend opts -labels [list __reel_caption [caption_opts $S $cap \
                        [_get $it caption_at nw] [_get $it caption_size 0.045]]]
                }
                set r [replay::render_trial $S $sock $trial $seg {*}$opts]
                if {[dict get $r bad]} { error "trial $trial: the movie writer failed: [dict get $r status]" }
                if {[dict get $r errors]} { {*}$o(-log) "trial $trial: [dict get $r errors] errors while rendering" }
            }
            card {
                card $sock $S $seg $fps $crf $it
            }
            default { error "reel::render: item $i: type must be trial or card" }
        }
        lappend segs $seg
        if {$o(-progress) ne ""} { {*}$o(-progress) [expr {$i + 1}] $n }
    }
    set copied [join_segments $segs $out $fps $crf $o(-run) $work]
    foreach f [glob -nocomplain -directory $work *] { file delete $f }
    catch { file delete $work }
    dict create out $out segments [llength $segs] copied $copied \
        seconds [expr {([clock milliseconds] - $t0) / 1000.0}]
}

# captions and cards are overlay text, which a stim2 from before it
# refuses as a usage error: say what is wrong instead
proc reel::need_text {sock} {
    set r [framelock::send $sock {overlay set __reel_probe -text {}; overlay delete __reel_probe}]
    if {[framelock::is_error $r]} {
        set v [framelock::send $sock {catch {stimVersion} v; set v}]
        error "this stim2 ($v) cannot draw text on its overlay, which captions and cards need: update stim2"
    }
}

# the overlay options for a caption: a line FRAC of the screen's height,
# at a corner or edge of the rig's screen, over a translucent box
proc reel::caption_opts {S text at frac} {
    if {$at ni {nw n ne sw s se}} { error "caption_at must be nw, n, ne, sw, s or se" }
    array set g [replay::get $S geom]
    set hx [expr {[info exists g(halfx)] ? $g(halfx) : 10.0}]
    set hy [expr {[info exists g(halfy)] ? $g(halfy) : 7.5}]
    set size [expr {$frac * 2 * $hy}]
    set m [expr {0.6 * $size}]       ;# margin from the edge: the box's pad and a little more
    set x [switch -- [string index $at end] { w { expr {-$hx + $m} } e { expr {$hx - $m} } default { expr 0 } }]
    set y [expr {[string index $at 0] eq "n" ? $hy - $m : -$hy + $m}]
    list -text $text -size $size -anchor $at -pos [list $x $y] -color {1 1 1 1} -bg {0 0 0 0.55} -show 1
}

# a title card: TEXT centred over a plain background, for DUR seconds
proc reel::card {sock S out fps crf it} {
    set text [dict get $it text]
    set dur [_get $it dur 2]
    if {![string is double -strict $dur] || $dur <= 0 || $dur > 60} { error "card dur must be 0-60 s" }
    set frames [expr {max(1, int(round($dur * $fps)))}]
    set bg [_get $it bg ""]
    if {$bg eq ""} {
        array set g [replay::get $S geom]
        set bg [expr {[info exists g(bg)] ? $g(bg) : {0 0 0}}]
    }
    set w [framelock::send $sock {screen_set WinWidth}]
    set h [framelock::send $sock {screen_set WinHeight}]
    set hy [framelock::send $sock {screen_set HalfScreenDegreeY}]
    set size [expr {[_get $it size 0.07] * 2 * $hy}]
    set color [_get $it color {1 1 1 1}]
    set target [replay::_sink_target $out 1 1 $fps $w $h $fps $crf ""]
    framelock::send $sock {stepMode 0}
    # an empty scene (core commands: `reset` is the rig script's, and the
    # next trial's setup brings that back)
    foreach c [list resetObjList {glistInit 1} [list setBackground {*}$bg]] {
        set r [framelock::send $sock $c]
        if {[framelock::is_error $r]} { error "card: $c: $r" }
    }
    set r [framelock::send $sock [list overlay set __reel_card -text $text -size $size -anchor c \
                                      -pos {0 0} -color $color -show 1]]
    if {[framelock::is_error $r]} { error "card: $r" }
    try {
        framelock::send $sock {stepMode 1 0}
        set r [framelock::send $sock [list frameSink open -manual $target]]
        if {[framelock::is_error $r]} { error "card: frameSink: $r" }
        framelock::send $sock [list stepFrame [expr {1.0 / $fps}]]
        framelock::send $sock [list frameSink write $frames]
        set st [framelock::send $sock {frameSink close}]
        if {[dict exists $st status] && [dict get $st status] != 0} { error "card: the movie writer failed: $st" }
    } finally {
        catch { framelock::send $sock {stepMode 0} }
        catch { framelock::send $sock {overlay delete __reel_card} }
    }
}

# what decides whether segments join by copy: codec, size, pixel format,
# profile, frame rate and time base of the video stream
proc reel::probe {run f} {
    lassign [{*}$run ffprobe -v error -select_streams v:0 -show_entries \
                 stream=codec_name,width,height,pix_fmt,profile,r_frame_rate,time_base \
                 -of default=noprint_wrappers=1 $f] code out
    if {$code} { error "ffprobe $f: $out" }
    set d [dict create]
    foreach line [split $out \n] { if {[regexp {^([a-z_]+)=(.*)$} $line -> k v]} { dict set d $k $v } }
    return $d
}

proc reel::join_segments {segs out fps crf run work} {
    if {[llength $segs] == 1} {
        file rename -force [lindex $segs 0] $out
        return 1
    }
    set ps [lmap s $segs {probe $run $s}]
    set same 1
    foreach p [lrange $ps 1 end] { if {$p ne [lindex $ps 0]} { set same 0; break } }
    if {$same} {
        set list [file join $work segments.txt]
        set fd [open $list w]
        foreach s $segs { puts $fd "file '[string map {' '\\''} $s]'" }
        close $fd
        lassign [{*}$run ffmpeg -loglevel error -y -f concat -safe 0 -i $list -c copy \
                     -movflags +faststart $out] code msg
        if {!$code} { return 1 }
        say "joining by copy failed ($msg): encoding once instead"
    }
    # encode once: each scaled into the first one's size, padded, at fps
    set w [dict get [lindex $ps 0] width]; set h [dict get [lindex $ps 0] height]
    set in {}; set fc ""; set k 0
    foreach s $segs {
        lappend in -i $s
        append fc "\[$k:v\]scale=${w}:${h}:force_original_aspect_ratio=decrease,pad=${w}:${h}:(ow-iw)/2:(oh-ih)/2,setsar=1,fps=$fps\[v$k\];"
        incr k
    }
    for {set i 0} {$i < $k} {incr i} { append fc "\[v$i\]" }
    append fc "concat=n=$k:v=1:a=0\[out\]"
    lassign [{*}$run ffmpeg -loglevel error -y {*}$in -filter_complex $fc -map {[out]} \
                 -c:v libx264 -preset medium -tune animation -crf $crf -pix_fmt yuv420p \
                 -movflags +faststart $out] code msg
    if {$code} { error "joining the segments failed: $msg" }
    return 0
}

# ---------------------------------------------------------------------------
# the wire form: a reel as JSON, parsed to a dict (yajl::json2dict or
# json::json2dict), checked field by field and turned into a spec.  LOAD
# is {*}LOAD NAME -> a session (replay::load's), for a trial's "file".
# Anything wrong is an error with -errorcode {REEL msg}: a 400 to a server.
#
#   { "fps": 60, "crf": 14,
#     "items": [
#       {"type": "card", "text": "Day 1", "dur": 2, "size": 0.07,
#        "color": [1,1,1,1], "bg": [0,0,0]},
#       {"type": "trial", "file": "NAME", "obsid": 12,      (or "trial": 13)
#        "caption": "Trial 13", "caption_at": "nw", "caption_size": 0.045,
#        "gaze": {"t": [s...], "x": [deg...], "y": [deg...]},
#        "gaze_style": {"size": 0.7, "color": [1,0.55,0.1,0.9]}} ] }
# ---------------------------------------------------------------------------
proc reel::bad {msg} { return -code error -errorcode [list REEL $msg] $msg }

proc reel::_num {v what lo hi} {
    if {![string is double -strict $v] || $v < $lo || $v > $hi} { bad "$what must be a number from $lo to $hi" }
    return $v
}
proc reel::_text {v what} {
    if {[string length $v] > 500} { bad "$what is longer than 500 characters" }
    return $v
}
proc reel::_rgba {v what} {
    if {[llength $v] ni {3 4}} { bad "$what must be [r,g,b] or [r,g,b,a], 0 to 1" }
    lmap c $v { _num $c $what 0 1 }
}

proc reel::from_wire {d load} {
    set spec [dict create]
    dict set spec fps [expr {int([_num [_get $d fps 60] fps 10 120])}]
    dict set spec crf [expr {int([_num [_get $d crf 14] crf 0 35])}]
    set items [_get $d items {}]
    if {![llength $items]} { bad "items: none" }
    if {[llength $items] > 50} { bad "items: at most 50" }
    set out {}
    set i 0
    foreach it $items {
        incr i
        if {[catch {dict size $it}]} { bad "item $i is not an object" }
        set type [_get $it type ""]
        switch -- $type {
            card {
                set c [dict create type card text [_text [_get $it text ""] "item $i text"]]
                if {[dict get $c text] eq ""} { bad "item $i: a card needs text" }
                dict set c dur [_num [_get $it dur 2] "item $i dur" 0.1 60]
                dict set c size [_num [_get $it size 0.07] "item $i size" 0.01 0.5]
                if {[dict exists $it color]} { dict set c color [_rgba [dict get $it color] "item $i color"] }
                if {[dict exists $it bg]} { dict set c bg [lrange [_rgba [dict get $it bg] "item $i bg"] 0 2] }
                lappend out $c
            }
            trial {
                set f [_get $it file ""]
                if {![regexp {^[A-Za-z0-9._-]+$} $f]} { bad "item $i: file must be a session's name" }
                set S [{*}$load $f]
                if {[dict exists $it obsid]} {
                    set trial [expr {int([_num [dict get $it obsid] "item $i obsid" 0 1e6]) + 1}]
                } else {
                    set trial [expr {int([_num [_get $it trial ""] "item $i trial" 1 1e6])}]
                }
                set n [replay::ntrials $S]
                if {$trial > $n} { bad "item $i: trial $trial: $f has $n" }
                set t [dict create type trial S $S trial $trial file $f]
                set cap [_text [_get $it caption ""] "item $i caption"]
                if {$cap ne ""} {
                    dict set t caption $cap
                    set at [_get $it caption_at nw]
                    if {$at ni {nw n ne sw s se}} { bad "item $i caption_at must be nw, n, ne, sw, s or se" }
                    dict set t caption_at $at
                    dict set t caption_size [_num [_get $it caption_size 0.045] "item $i caption_size" 0.01 0.3]
                }
                set g [_get $it gaze ""]
                if {$g ne "" && $g ne "null"} {
                    if {[catch {dict size $g}]} { bad "item $i gaze must be {t, x, y}" }
                    set T [_get $g t {}]; set X [_get $g x {}]; set Y [_get $g y {}]
                    set m [llength $T]
                    if {[llength $X] != $m || [llength $Y] != $m} { bad "item $i gaze: t, x and y differ in length" }
                    if {$m > 500000} { bad "item $i gaze: more than 500000 samples" }
                    foreach v [concat $T $X $Y] {
                        if {![string is double -strict $v]} { bad "item $i gaze: not a number: [string range $v 0 20]" }
                    }
                    if {$m} { dict set t gaze [list $T $X $Y] }
                }
                set gs [_get $it gaze_style ""]
                if {$gs ne "" && $gs ne "null"} {
                    dict set t gaze_style [list [_num [_get $gs size 0.7] "item $i gaze_style size" 0.05 10] \
                        [_rgba [_get $gs color {1 0.55 0.1 0.9}] "item $i gaze_style color"]]
                }
                lappend out $t
            }
            default { bad "item $i: type must be \"trial\" or \"card\"" }
        }
    }
    set ntrials 0
    foreach x $out { if {[dict get $x type] eq "trial"} { incr ntrials } }
    if {!$ntrials} {
        bad "items: a reel needs at least one trial (cards draw on its stim2)"
    }
    dict set spec items $out
    return $spec
}
