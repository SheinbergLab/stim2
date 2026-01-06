# examples/spine/spine_skins.tcl
# Spine skin switching demonstration
# Demonstrates: sp::setSkin for runtime character customization
#
# The Goblins example has multiple skins that can be swapped at runtime:
#   - default: base goblin
#   - goblin: male goblin variant
#   - goblingirl: female goblin variant
#
# Skins allow artists to create character variants that share
# the same skeleton and animations but have different appearances.

# ============================================================
# STIM CODE - Copy this section to your project
# ============================================================

namespace eval spine_skins {
    variable current_skin "goblin"
    variable char_height 5.0
}

proc spine_skins_setup {} {
    glistInit 1
    resetObjList
    
    # Create spine object using asset finder
    set obj [spineAsset spine/goblins/goblins-pro.json spine/goblins/goblins.atlas]
    objName $obj goblin
    
    # Scale to desired height
    sp::fitToHeight $obj $spine_skins::char_height
    
    # Set initial skin
    sp::setSkin $obj $spine_skins::current_skin
    
    # Start walking animation (only animation available)
    sp::setAnimationByName $obj walk 0 1
    
    # Wrap in metagroup for positioning
    set mg [metagroup]
    metagroupAdd $mg $obj
    objName $mg goblin_group
    
    # Offset Y since spine origin is at feet
    translateObj $mg 0 [expr {-$spine_skins::char_height / 2.0}]
    
    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ---- Adjuster helper procs ----

proc skins_set_skin {skin} {
    sp::setSkin goblin $skin
    set spine_skins::current_skin $skin
    return
}

proc skins_get_skin {{target {}}} {
    dict create skin $spine_skins::current_skin
}

proc skins_set_height {height} {
    sp::fitToHeight goblin $height
    set spine_skins::char_height $height
    
    # Update position offset
    translateObj goblin_group 0 [expr {-$height / 2.0}]
    redraw
    return
}

proc skins_get_height {{target {}}} {
    dict create height $spine_skins::char_height
}

proc skins_set_timescale {scale} {
    sp::setTimeScale goblin $scale
    return
}

proc skins_get_timescale {{target {}}} {
    dict create scale 1.0
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup spine_skins_setup {} \
    -adjusters {goblin_skin goblin_height goblin_timescale goblin_position} \
    -label "Spine Skins (Goblins)"

# Skin selector
workspace::adjuster goblin_skin {
    skin {choice {goblin goblingirl} goblin "Skin"}
} -target {} -proc skins_set_skin -getter skins_get_skin \
  -label "Character Skin"

# Character height
workspace::adjuster goblin_height {
    height {float 2.0 10.0 0.5 5.0 "Height" deg}
} -target {} -proc skins_set_height -getter skins_get_height \
  -label "Character Size"

# Animation speed
workspace::adjuster goblin_timescale {
    scale {float 0.1 3.0 0.1 1.0 "Speed"}
} -target {} -proc skins_set_timescale -getter skins_get_timescale \
  -label "Time Scale"

# Position (on metagroup)
workspace::adjuster goblin_position -template position -target goblin_group \
    -label "Position"
