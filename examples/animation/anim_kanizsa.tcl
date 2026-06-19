# examples/animation/anim_kanizsa.tcl
# Moving Kanizsa square built from "pac-man" inducers.
# Demonstrates: pac-men as anti-aliased circular SECTORS (polygon's polysector
#               mask), declarative pursuit motion (animatePosition -oscillate),
#               and the pursuit/perception
#               dissociation -- inducers that only change ORIENTATION while the
#               illusory square is perceived to MOVE.
#
# Reproduces the moving illusory-figure stimulus used to study smooth pursuit
# and perception (Journal of Vision 3(11):1). stim2 units are degrees, so the
# defaults are the paper's values directly:
#   - 16 Kanizsa squares spanning ~46 deg horizontally (33 inducer columns)
#   - square side 1.44 deg (== inducer column spacing)
#   - white disc inducers 1.15 deg diameter, 0.29 deg gap, on a black background
#   - right-angle (90 deg) mouths
#   - frames alternate at ~15 Hz (66 ms each) -> apparent-motion velocity
#     = side x flip-rate ~= 21.6 deg/s
#
# Two workspace variants:
#   "Two-frame (apparent motion)" -- THE faithful version. A fixed 2-row field
#       of pac-men defines a whole ROW of Kanizsa squares. EVERY inducer rotates
#       by exactly +-90 deg between frame A and frame B, so frame B's squares sit
#       at the midpoints of frame A's; alternating A/B at ~15 Hz yields the
#       bi-directional apparent motion of the illusory contours. Nothing
#       translates and no inducer "rests" -- each is a corner of one square per
#       frame.
#   "Drifting (smooth)" -- a real Kanizsa square that translates smoothly: its 4
#       corner inducers travel with it (a metagroup, via animatePosition
#       -oscillate). The features move too, so there's no low-/high-level
#       dissociation -- but it's the only way to get genuinely smooth motion of
#       the figure (with fixed inducers the square can only sit where inducers
#       are, which is why the faithful stimulus uses apparent motion).
#
# A pac-man is a disc with a wedge ("mouth") removed. We use the polygon
# module's anti-aliased sector mask: scale the unit quad to the diameter and
# call `polysector $p $mouthDeg`. The mouth is centred on +X -- aim it with
# rotateObj; change its size live by re-calling polysector (just a uniform, no
# vertex work). (`polyannulus` gives rings/arc-bands the same way.)
#
# Requires modules: polygon, metagroup (plus the built-in animate commands).

# State only; procs are global (matches the anim_launch convention and avoids
# namespace-relative name resolution surprises). Setups read every parameter
# from here, so any adjuster can just set a var and rebuild.
namespace eval kanizsa {
    variable inducers {}     ;# ids of all pac-men in the field (field variants)
    variable gx              ;# array: id -> fixed grid x
    variable gy              ;# array: id -> fixed grid y (+s/2 top, -s/2 bottom)
    variable ang             ;# array: id -> current orientation (continuous variant)
    variable angA            ;# array: id -> orientation in two-frame state A
    variable angB            ;# array: id -> orientation in two-frame state B
    variable refsq    ""     ;# single percept marker (drifting variant)
    variable refslots {}     ;# two-frame: list of {id slotL} percept markers
    variable mg       ""     ;# carrier metagroup (animation attached here)

    variable size     1.44   ;# square side == inducer spacing (deg)
    variable mouth    90.0   ;# mouth angle (deg); 90 = right-angle sectors
    variable radius   0.575  ;# pac-man radius (deg); set to 0.4*size in build
    variable speed    8.0    ;# drifting variant: translation speed (deg/sec)
    variable rate     15.0   ;# two-frame: state flips per second (Hz; 66 ms/frame)
    variable ncols    33     ;# columns of inducers -> 16 squares over ~46 deg
    variable show_ref 0      ;# draw the reference (ground-truth) square?

    variable last_setup kanizsa_setup_twoframe
}

# ============================================================
# Pac-man -- the polygon module's anti-aliased sector mask on a unit quad.
# Mouth (the missing wedge) is centred on +X; aim it with rotateObj.
# ============================================================
proc kanizsa_pacman { mouth } {
    set p [polygon]
    set d [expr {2.0*$kanizsa::radius}]
    scaleObj $p $d $d
    polysector $p $mouth
    return $p
}

# Thin square outline, unit-centred, scaled to side -> "ground-truth" marker.
proc kanizsa_ref_square { side color } {
    set h [expr {$side/2.0}]
    set p [polygon]
    polyverts $p [dl_flist -$h $h $h -$h] [dl_flist -$h -$h $h $h]
    polytype  $p line_loop
    polyfill  $p 0 2.0
    polycolor $p {*}$color
    return $p
}

# ============================================================
# Shared scene build: a fixed 2-row field of pac-men (no animation, no
# markers). Reads kanizsa:: size / ncols / mouth. Returns the metagroup.
# ============================================================
proc kanizsa_build_field {} {
    glistInit 1
    resetObjList
    setBackground 0 0 0                 ;# black bg, white inducers (cf. the methods)

    array unset kanizsa::gx
    array unset kanizsa::gy
    array unset kanizsa::ang
    set kanizsa::inducers {}
    set kanizsa::refsq ""
    set kanizsa::refslots {}

    set s   $kanizsa::size
    set top [expr {$s/2.0}]
    set kanizsa::radius [expr {0.4*$s}]   ;# diameter 0.8*side -> 0.29 deg gap at side 1.44
    set x0  [expr {-($kanizsa::ncols-1)*$s/2.0}]   ;# leftmost column x

    set mg [metagroup]
    objName $mg kanizsa
    set kanizsa::mg $mg
    for { set i 0 } { $i < $kanizsa::ncols } { incr i } {
        set cx [expr {$x0 + $i*$s}]
        foreach cy [list $top [expr {-$top}]] {
            set p [kanizsa_pacman $kanizsa::mouth]
            polycolor $p 1 1 1
            translateObj $p $cx $cy
            set kanizsa::gx($p) $cx
            set kanizsa::gy($p) $cy
            set kanizsa::ang($p) 0.0
            lappend kanizsa::inducers $p
            metagroupAdd $mg $p
        }
    }

    glistAddObject $mg 0
    return $mg
}

# Column x for column index c, and the centre x of square slot L (cols L,L+1).
proc kanizsa_colx  { c } { expr {-($kanizsa::ncols-1)*$kanizsa::size/2.0 + $c*$kanizsa::size} }
proc kanizsa_slotx { L } { expr {[kanizsa_colx $L] + $kanizsa::size/2.0} }

# Which square slot does column c complete in a frame whose completed slots
# have parity p (0 = even-L, 1 = odd-L)?  c is the LEFT corner of slot c, or the
# RIGHT corner of slot c-1; at most one of those is completed.  -1 = none (edge).
proc kanizsa_completed_slot { c p ncols } {
    if { ($c % 2) == $p && $c <= $ncols-2 }       { return $c }
    if { (($c-1) % 2) == $p && $c-1 >= 0 }         { return [expr {$c-1}] }
    return -1
}

proc kanizsa_finalize {} {
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# Orientation (deg) for an inducer whose mouth should open toward (xc,0).
proc kanizsa_aim { p xc } {
    expr {atan2(-$kanizsa::gy($p), $xc - $kanizsa::gx($p)) * 180.0/$::pi}
}

# ============================================================
# VARIANT A: two-frame apparent motion  (the faithful stimulus)
# ============================================================
# A whole row of Kanizsa squares. Frame A completes even slots, frame B odd
# slots; every inducer rotates +-90 deg between them, sliding the lattice over
# by one slot. Alternating A/B -> bi-directional apparent motion.
proc kanizsa_setup_twoframe {} {
    set kanizsa::last_setup kanizsa_setup_twoframe
    set mg [kanizsa_build_field]
    kanizsa_compute_states

    # apply frame A so a static source still shows squares
    foreach p $kanizsa::inducers { rotateObj $p $kanizsa::angA($p) 0 0 1 }

    # one percept marker per square slot (shown for the completed slots only)
    for { set L 0 } { $L <= $kanizsa::ncols-2 } { incr L } {
        set r [kanizsa_ref_square $kanizsa::size {0.2 0.5 1.0}]
        translateObj $r [kanizsa_slotx $L] 0
        setVisible $r 0
        metagroupAdd $mg $r
        lappend kanizsa::refslots [list $r $L]
    }

    animateCustom kanizsa -proc kanizsa_drive_twoframe -params {}
    kanizsa_finalize
}

# Precompute each inducer's orientation in frame A and frame B (mouth aimed at
# the centre of whatever square it completes that frame; outward at the edges).
proc kanizsa_compute_states {} {
    set x0 [kanizsa_colx 0]
    foreach p $kanizsa::inducers {
        set col  [expr {round(($kanizsa::gx($p) - $x0)/$kanizsa::size)}]
        set rest [expr {$kanizsa::gy($p) > 0 ? 90.0 : -90.0}]
        set la [kanizsa_completed_slot $col 0 $kanizsa::ncols]
        set lb [kanizsa_completed_slot $col 1 $kanizsa::ncols]
        set kanizsa::angA($p) [expr {$la >= 0 ? [kanizsa_aim $p [kanizsa_slotx $la]] : $rest}]
        set kanizsa::angB($p) [expr {$lb >= 0 ? [kanizsa_aim $p [kanizsa_slotx $lb]] : $rest}]
    }
}

proc kanizsa_drive_twoframe { t dt frame obj } {
    set rate $kanizsa::rate
    if { $rate <= 0 } { set rate 0.001 }
    set state [expr {int(floor($t*$rate)) % 2}]    ;# 0 -> frame A, 1 -> frame B
    foreach p $kanizsa::inducers {
        rotateObj $p [expr {$state ? $kanizsa::angB($p) : $kanizsa::angA($p)}] 0 0 1
    }
    foreach slot $kanizsa::refslots {
        lassign $slot r L
        setVisible $r [expr {$kanizsa::show_ref && ($L % 2) == $state}]
    }
}

# ============================================================
# VARIANT B: drifting (smoothly translating) Kanizsa square
# ============================================================
# 4 pac-men locked at the corners of one square, mouths aimed at the group
# centre. The whole metagroup translates smoothly -- features move too.
proc kanizsa_setup_drift {} {
    set kanizsa::last_setup kanizsa_setup_drift
    set kanizsa::inducers {}                 ;# 4 corner pac-men (for live mouth)
    set kanizsa::refslots {}
    set kanizsa::radius [expr {0.4*$kanizsa::size}]

    glistInit 1
    resetObjList
    setBackground 0 0 0

    set h [expr {$kanizsa::size/2.0}]
    set mg [metagroup]
    objName $mg kanizsa
    set kanizsa::mg $mg
    foreach {cx cy} [list $h $h  $h [expr {-$h}]  [expr {-$h}] [expr {-$h}]  [expr {-$h}] $h] {
        set p [kanizsa_pacman $kanizsa::mouth]
        polycolor $p 1 1 1
        translateObj $p $cx $cy
        rotateObj $p [expr {atan2(-$cy,-$cx)*180.0/$::pi}] 0 0 1   ;# mouth toward centre
        lappend kanizsa::inducers $p
        metagroupAdd $mg $p
    }

    set kanizsa::refsq [kanizsa_ref_square $kanizsa::size {0.2 0.5 1.0}]
    setVisible $kanizsa::refsq $kanizsa::show_ref
    metagroupAdd $mg $kanizsa::refsq

    glistAddObject $mg 0
    kanizsa_drift_motion          ;# declarative back-and-forth via animatePosition
    kanizsa_finalize
}

# Constant-velocity (triangle) horizontal oscillation of the whole square;
# mean speed ~= kanizsa::speed (triangle covers 4*amp per period).
proc kanizsa_drift_motion {} {
    set amp 6.0
    animatePosition kanizsa -oscillate $amp -waveform triangle \
        -axis {1 0} -freq [expr {$kanizsa::speed/(4.0*$amp)}]
}

# ============================================================
# ADJUSTERS  (live where possible; geometry changes rebuild the scene)
# ============================================================
proc kanizsa_rebuild {} { eval $kanizsa::last_setup }

proc kanizsa_set_mouth { mouth } {
    set kanizsa::mouth $mouth
    foreach p $kanizsa::inducers { polysector $p $mouth }   ;# live, just a uniform
    redraw
}
proc kanizsa_get_mouth { {target {}} } { dict create mouth $kanizsa::mouth }

proc kanizsa_set_speed { speed } {
    set kanizsa::speed $speed
    if { $kanizsa::last_setup eq "kanizsa_setup_drift" } { kanizsa_drift_motion }
    redraw
}
proc kanizsa_get_speed { {target {}} } { dict create speed $kanizsa::speed }

proc kanizsa_set_rate  { rate } { set kanizsa::rate $rate; redraw }
proc kanizsa_get_rate  { {target {}} } { dict create rate $kanizsa::rate }

proc kanizsa_set_geom  { size ncols } {
    set kanizsa::size  $size
    set kanizsa::ncols [expr {int($ncols)}]
    kanizsa_rebuild
}
proc kanizsa_get_geom  { {target {}} } {
    dict create size $kanizsa::size ncols $kanizsa::ncols
}

proc kanizsa_set_ref { show } {
    set kanizsa::show_ref $show
    if { $kanizsa::refsq ne "" } { setVisible $kanizsa::refsq $show; redraw }
}
proc kanizsa_get_ref { {target {}} } { dict create show $kanizsa::show_ref }

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
proc kanizsa_setup {} { kanizsa_setup_twoframe }

if { [llength [info commands workspace::setup]] } {
    workspace::reset

    workspace::setup kanizsa_setup_twoframe {} \
        -adjusters {kanizsa_mouth kanizsa_rate kanizsa_geom kanizsa_ref} \
        -label "Two-frame (apparent motion)"

    workspace::variant drift {} \
        -proc kanizsa_setup_drift \
        -adjusters {kanizsa_mouth kanizsa_speed kanizsa_ref} \
        -label "Drifting (smooth)"

    workspace::adjuster kanizsa_mouth {
        mouth {float 20.0 150.0 5.0 90.0 "Mouth" deg}
    } -target {} -proc kanizsa_set_mouth -getter kanizsa_get_mouth -label "Mouth"

    workspace::adjuster kanizsa_rate {
        rate {float 1.0 20.0 0.5 15.0 "Flip rate" Hz}
    } -target {} -proc kanizsa_set_rate -getter kanizsa_get_rate -label "Flip rate"

    workspace::adjuster kanizsa_speed {
        speed {float 0.5 30.0 0.5 8.0 "Speed" deg/s}
    } -target {} -proc kanizsa_set_speed -getter kanizsa_get_speed -label "Speed"

    workspace::adjuster kanizsa_geom {
        size  {float 0.5 3.0 0.05 1.44 "Square size" deg}
        ncols {float 4 41 1 33 "Columns"}
    } -target {} -proc kanizsa_set_geom -getter kanizsa_get_geom -label "Field"

    workspace::adjuster kanizsa_ref {
        show {bool 0 "Show reference square"}
    } -target {} -proc kanizsa_set_ref -getter kanizsa_get_ref -label "Reference"
}

# Build something when sourced directly.
kanizsa_setup_twoframe
