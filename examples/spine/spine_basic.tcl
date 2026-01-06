# examples/spine/spine_basic.tcl
# Spine 2D character animation demonstration
# Demonstrates: spine loading, animation control, scaling, positioning
#
# Uses the spine-c runtime for skeletal animation:
#   - sp::create to load skeleton + atlas
#   - sp::fitToHeight for visual angle scaling
#   - sp::setAnimationByName / sp::addAnimationByName for animation control
#   - sp::copy for efficient instancing
#
# Spineboy is the example character from Esoteric Software.

# ============================================================
# STIM CODE - Copy this section to your project
# ============================================================

namespace eval spine_demo {
    variable orig_obj ""
    variable base_anim "idle"
    variable character_height 4.0
}

proc spine_basic_setup {} {
    glistInit 1
    resetObjList
    
    # Create spine object using asset finder
    set obj [spineAsset spine/spineboy/spineboy-pro.json spine/spineboy/spineboy.atlas]
    objName $obj spineboy
    set spine_demo::orig_obj $obj
    
    # Initialize object properties
    setObjProp $obj facing_right 1
    
    # Scale to desired height
    sp::fitToHeight $obj $spine_demo::character_height
    
    # Start with base animation (looping)
    sp::setAnimationByName $obj $spine_demo::base_anim 0 1
    
    # Wrap in metagroup for positioning
    set mg [metagroup]
    metagroupAdd $mg $obj
    objName $mg spine_group
    
    # Offset Y since spine origin is at feet
    translateObj $mg 0 [expr {-$spine_demo::character_height / 2.0}]
    
    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ---- Adjuster helper procs ----

# Base animation - the looping animation to return to
proc spine_set_base {anim} {
    sp::setAnimationByName spineboy $anim 0 1
    set spine_demo::base_anim $anim
    return
}

proc spine_get_base {{target {}}} {
    dict create anim $spine_demo::base_anim
}

# Trigger a one-shot action that returns to base
proc spine_trigger_action {action} {
    # Play the action, then queue return to base animation
    sp::setAnimationByName spineboy $action 0 0
    sp::addAnimationByName spineboy $spine_demo::base_anim 0 1 0.0
    return
}

proc spine_set_height {height} {
    sp::fitToHeight spineboy $height
    set spine_demo::character_height $height
    
    # Update position offset
    translateObj spine_group 0 [expr {-$height / 2.0}]
    redraw
    return
}

proc spine_get_height {{target {}}} {
    dict create height $spine_demo::character_height
}

proc spine_set_timescale {scale} {
    sp::setTimeScale spineboy $scale
    return
}

proc spine_get_timescale {{target {}}} {
    dict create scale 1.0
}

proc spine_set_mix {duration} {
    sp::setDefaultMix spineboy $duration
    return
}

proc spine_get_mix {{target {}}} {
    dict create duration 0.0
}

proc spine_flip {direction} {
    if {$direction eq "left"} {
        scaleObj spineboy -1.0 1.0
        setObjProp spineboy facing_right 0
    } else {
        scaleObj spineboy 1.0 1.0
        setObjProp spineboy facing_right 1
    }
    return
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup spine_basic_setup {} \
    -adjusters {spine_base spine_actions spine_direction spine_height spine_timescale spine_mix spine_position} \
    -label "Spine Basic (Spineboy)"

# Base animation (looping)
workspace::adjuster spine_base {
    anim {choice {idle walk run hoverboard} idle "Base Loop"}
} -target {} -proc spine_set_base -getter spine_get_base \
  -label "Base Animation"

# Trigger actions (one-shot) - these are buttons, not a dropdown
workspace::adjuster spine_actions {
    jump   {action "Jump"}
    shoot  {action "Shoot"}
    portal {action "Portal"}
    death  {action "Death"}
} -target {} -proc spine_trigger_action \
  -label "Trigger Actions"

# Direction control
workspace::adjuster spine_direction {
    left  {action "← Face Left"}
    right {action "Face Right →"}
} -target {} -proc spine_flip \
  -label "Direction"

# Character height
workspace::adjuster spine_height {
    height {float 1.0 10.0 0.5 4.0 "Height" deg}
} -target {} -proc spine_set_height -getter spine_get_height \
  -label "Character Size"

# Animation speed
workspace::adjuster spine_timescale {
    scale {float 0.1 3.0 0.1 1.0 "Speed"}
} -target {} -proc spine_set_timescale -getter spine_get_timescale \
  -label "Time Scale"

# Blend duration between animations
workspace::adjuster spine_mix {
    duration {float 0.0 1.0 0.05 0.0 "Duration" sec}
} -target {} -proc spine_set_mix -getter spine_get_mix \
  -label "Blend Mix"

# Position (on metagroup)
workspace::adjuster spine_position -template position -target spine_group \
    -label "Position"
