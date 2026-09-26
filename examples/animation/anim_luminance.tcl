# examples/animation/anim_luminance.tcl
# Steady-state luminance flicker, in C, per object
# Demonstrates: animateLuminance on polygons (a module that installed the
# colour hooks in stim2.h -- see polygon.c polygonGetColor/SetColor)
#
#   animateLuminance obj -freq hz -phase rad -depth 0..1 ?-base {r g b ?a?}?
#
# Each object's colour is multiplied by 1 + depth*sin(2 pi f t + phase)
# every frame, with the object's own clock, no Tcl per frame. Re-issuing
# the command updates options in place (no phase jump), so depth can be
# titrated live. animateClear restores the base colour.
#
# The use case: a field of dim "distractor" dots that must carry a driven
# response without ever presenting an event. Each dot gets its own phase,
# so the field never pulses as a whole; equal depth on every dot, so any
# relative difference between dots can only come from attention.

# ============================================================
# SETUP PROCS
# ============================================================

# One dot flickering about its own gray
proc setup_one { {freq 8.0} {depth 0.25} } {
    glistInit 1
    resetObjList

    set p [polygon]
    objName $p dot
    polycirc $p 1
    scaleObj $p 1.0 1.0
    polycolor $p 0.45 0.45 0.5

    animateLuminance dot -freq $freq -depth $depth

    glistAddObject $p 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# A constellation: n dots on a ring, each with a random phase, in one
# metagroup so they can be shown/hidden together. Because the animation
# is per object it runs for members of a metagroup just the same.
proc setup_field { {n 12} {radius 6.0} {freq 8.0} {depth 0.25} {gray 0.4} } {
    glistInit 1
    resetObjList

    set mg [metagroup]
    objName $mg field
    for { set k 0 } { $k < $n } { incr k } {
        set a [expr {2*3.14159265*$k/$n}]
        set p [polygon]
        objName $p dot$k
        polycirc $p 1
        scaleObj $p 0.3 0.3
        translateObj $p [expr {$radius*cos($a)}] [expr {$radius*sin($a)}]
        polycolor $p $gray $gray [expr {$gray*1.1}]
        animateLuminance dot$k -freq $freq -depth $depth \
            -phase [expr {rand()*2*3.14159265}]
        metagroupAdd $mg $p
    }

    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# ============================================================
# ADJUSTERS
# ============================================================

# Titrate the depth on every dot without touching phase or frequency
proc set_depth { depth {n 12} } {
    for { set k 0 } { $k < $n } { incr k } {
        if { [animateLuminance dot$k] ne "" } {
            animateLuminance dot$k -depth $depth
        }
    }
}

proc get_state { {k 0} } { return [animateLuminance dot$k] }

# Static again: clears the animation and restores each dot's base colour
proc stop_field { {n 12} } {
    for { set k 0 } { $k < $n } { incr k } { animateClear dot$k luminance }
}
