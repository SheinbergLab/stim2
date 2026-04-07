# examples/spine/spine_aim.tcl
# Spine IK aiming demonstration
# Demonstrates: programmatic bone control, IK constraint mixing
#
# Uses sp::setBonePosition to move the "crosshair" IK target bone,
# and sp::setIkMix to control how strongly the aim IK constraint
# drives the character's arm and torso.
#
# The crosshair bone drives the aim-ik and aim-torso-ik constraints,
# which rotate the gun arm, torso, and head toward the target.

# ============================================================
# STIM CODE
# ============================================================

namespace eval spine_aim {
    variable orig_obj ""
    variable character_height 4.0
    # Crosshair setup pose position (from skeleton data)
    variable crosshair_base_x 302.83
    variable crosshair_base_y 569.45
}

proc spine_aim_setup {} {
    glistInit 1
    resetObjList

    set obj [spineAsset spine/spineboy/spineboy-pro.json spine/spineboy/spineboy.atlas]
    objName $obj spineboy
    set spine_aim::orig_obj $obj

    sp::fitToHeight $obj $spine_aim::character_height

    # Start with idle + aim animation on track 1 to enable constraints
    sp::setAnimationByName $obj idle 0 1
    sp::setAnimationByName $obj aim 1 1

    # Wrap in metagroup for positioning
    set mg [metagroup]
    metagroupAdd $mg $obj
    objName $mg spine_group

    translateObj $mg 0 [expr {-$spine_aim::character_height / 2.0}]

    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ---- Adjuster procs ----

proc spine_aim_set_crosshair {x y} {
    sp::setBonePosition spineboy crosshair $x $y
    return
}

proc spine_aim_get_crosshair {{target {}}} {
    dict create \
        x $spine_aim::crosshair_base_x \
        y $spine_aim::crosshair_base_y
}

proc spine_aim_set_ik_mix {mix} {
    sp::setIkMix spineboy "aim-ik" $mix
    return
}

proc spine_aim_get_ik_mix {{target {}}} {
    dict create mix 1.0
}

proc spine_aim_set_base {anim} {
    sp::setAnimationByName spineboy $anim 0 1
    return
}

proc spine_aim_get_base {{target {}}} {
    dict create anim idle
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup spine_aim_setup {} \
    -adjusters {aim_crosshair aim_ik_mix aim_base aim_position} \
    -label "Spine Aiming (IK Control)"

# Crosshair position - X/Y sliders to move the aim target
workspace::adjuster aim_crosshair {
    x {float -200.0 800.0 10.0 302.83 "Crosshair X"}
    y {float 100.0 900.0 10.0 569.45 "Crosshair Y"}
} -target {} -proc spine_aim_set_crosshair -getter spine_aim_get_crosshair \
  -label "Aim Target"

# IK mix - blend between no aiming and full aiming
workspace::adjuster aim_ik_mix {
    mix {float 0.0 1.0 0.05 1.0 "Aim IK Mix"}
} -target {} -proc spine_aim_set_ik_mix -getter spine_aim_get_ik_mix \
  -label "IK Strength"

# Base animation
workspace::adjuster aim_base {
    anim {choice {idle walk run} idle "Base Loop"}
} -target {} -proc spine_aim_set_base -getter spine_aim_get_base \
  -label "Base Animation"

# Position
workspace::adjuster aim_position -template position -target spine_group \
    -label "Position"
