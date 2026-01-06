# examples/spine/spine_grid.tcl
# Multiple Spine character grid demonstration
# Demonstrates: sp::copy for efficient instancing, grid layout, staggered animations
#
# Shows how to create multiple instances of a Spine character efficiently
# using sp::copy - all instances share the same skeleton data and textures
# but have independent animation state.

# ============================================================
# STIM CODE - Copy this section to your project
# ============================================================

namespace eval spine_grid {
    variable orig_obj ""
    variable objects {}
    variable grid_cols 5
    variable grid_rows 3
    variable spacing_x 3.0
    variable spacing_y 3.5
    variable char_height 2.5
}

proc spine_grid_setup {} {
    glistInit 1
    resetObjList
    
    # Clear object list
    set spine_grid::objects {}
    
    # Create the original spine object (template) using asset finder
    set orig [spineAsset spine/spineboy/spineboy-pro.json spine/spineboy/spineboy.atlas]
    set spine_grid::orig_obj $orig
    
    # Scale to desired height
    sp::fitToHeight $orig $spine_grid::char_height
    
    # Create grid of copies
    set cols $spine_grid::grid_cols
    set rows $spine_grid::grid_rows
    set sx $spine_grid::spacing_x
    set sy $spine_grid::spacing_y
    
    for {set row 0} {$row < $rows} {incr row} {
        for {set col 0} {$col < $cols} {incr col} {
            set obj [sp::copy $orig]
            
            # Track this object
            lappend spine_grid::objects $obj
            
            # Stagger animations - idle first, then walk after random delay
            sp::setAnimationByName $obj idle 0 1
            sp::addAnimationByName $obj walk 0 1 [expr {1.0 + 2.0 * rand()}]
            
            # Position in grid (centered around origin)
            set x [expr {($col - $cols/2.0 + 0.5) * $sx}]
            set y [expr {($row - $rows/2.0 + 0.5) * $sy}]
            
            # Offset since spine origin is at feet
            set y [expr {$y - $spine_grid::char_height / 2.0}]
            
            translateObj $obj $x $y
            glistAddObject $obj 0
        }
    }
    
    glistSetDynamic 0 1
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

# ---- Adjuster helper procs ----

proc grid_set_timescale {scale} {
    foreach obj $spine_grid::objects {
        catch { sp::setTimeScale $obj $scale }
    }
    return
}

proc grid_get_timescale {{target {}}} {
    dict create scale 1.0
}

proc grid_set_size {height} {
    set old_height $spine_grid::char_height
    set spine_grid::char_height $height
    
    foreach obj $spine_grid::objects {
        catch {
            sp::fitToHeight $obj $height
            
            # Adjust Y position for new height
            set pos [translateObj $obj]
            set x [lindex $pos 0]
            set y [lindex $pos 1]
            # Compensate for height change
            set y [expr {$y + ($old_height - $height) / 2.0}]
            translateObj $obj $x $y
        }
    }
    redraw
    return
}

proc grid_get_size {{target {}}} {
    dict create height $spine_grid::char_height
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup spine_grid_setup {} \
    -adjusters {grid_size grid_timescale} \
    -label "Spine Grid"

# Character size
workspace::adjuster grid_size {
    height {float 1.0 5.0 0.25 2.5 "Height" deg}
} -target {} -proc grid_set_size -getter grid_get_size \
  -label "Character Size"

# Animation speed for all
workspace::adjuster grid_timescale {
    scale {float 0.1 3.0 0.1 1.0 "Speed"}
} -target {} -proc grid_set_timescale -getter grid_get_timescale \
  -label "Time Scale"
