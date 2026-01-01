#!/usr/bin/env tclsh
#
# testspine.tcl - Test script for spine module
#
# Demonstrates:
#   - Loading spine characters with new scaling API
#   - Creating copies for multiple instances
#   - Animation control
#   - New bounding box extraction for physics integration
#

# Uncomment to load module if not auto-loaded
# load_modules spine

#----------------------------------------------------------------------
# Configuration
#----------------------------------------------------------------------

# Character height in degrees visual angle
set CHARACTER_HEIGHT 3.0

# Grid layout - spacing should be >= character size to avoid overlap
set GRID_COLS 5
set GRID_ROWS 3
set GRID_SPACING_X 4.0
set GRID_SPACING_Y 4.0

# Center characters vertically? (Spine origin is usually at feet)
set CENTER_VERTICALLY 1

#----------------------------------------------------------------------
# Find spine example files
#----------------------------------------------------------------------
proc find_spine_path {} {
    set search_paths {
        "./examples/spine/spineboy"
        "./spine/spineboy"
        "/Volumes/qpcs/stim2/examples/spine/spineboy"
        "/shared/qpcs/stim2/examples/spine/spineboy"
    }
    
    foreach p $search_paths {
        if {[file exists $p]} {
            return $p
        }
    }
    
    error "Could not find spineboy example files"
}

#----------------------------------------------------------------------
# Setup scene with grid of spine characters
#----------------------------------------------------------------------
proc setup {skel_file atlas_file {height 3.0}} {
    global GRID_COLS GRID_ROWS GRID_SPACING_X GRID_SPACING_Y
    
    resetObjList
    glistInit 2
    
    # Create the original spine object
    set orig [sp::create $skel_file $atlas_file]
    
    # Scale to desired height in degrees
    sp::fitToHeight $orig $height
    puts "Spine character scaled to $height degrees (scale: [sp::getScale $orig])"
    
    # Report size
    set size [sp::getSize $orig]
    puts "Character size: [lindex $size 0] x [lindex $size 1] degrees"
    
    # Check for bounding boxes (for physics integration)
    set bboxes [sp::listBoundingBoxes $orig]
    if {[llength $bboxes] > 0} {
        puts "Available bounding boxes:"
        foreach bb $bboxes {
            puts "  - [lindex $bb 0]: [lindex $bb 1]"
        }
    }
    
    # Create grid of copies
    for {set row 0} {$row < $GRID_ROWS} {incr row} {
        for {set col 0} {$col < $GRID_COLS} {incr col} {
            set obj [sp::copy $orig]
            
            # Set up animation - idle then walk after random delay
            sp::setAnimationByName $obj idle 0 1
            sp::addAnimationByName $obj walk 0 1 [expr {2.0 + 3.0 * rand()}]
            
            # Position in grid (centered around origin)
            set x [expr {($col - $GRID_COLS/2.0 + 0.5) * $GRID_SPACING_X}]
            set y [expr {($row - $GRID_ROWS/2.0 + 0.5) * $GRID_SPACING_Y}]
            
            # Offset to center vertically (Spine origin is at feet)
            if {$CENTER_VERTICALLY} {
                set y [expr {$y - $height/2.0}]
            }
            
            translateObj $obj $x $y
            
            glistAddObject $obj 0
        }
    }
    
    glistSetDynamic 0 1
    
    return $orig
}

#----------------------------------------------------------------------
# Demo: Extract bounding boxes for physics
#----------------------------------------------------------------------
proc show_bounding_boxes {obj} {
    set boxes [sp::getBoundingBoxes $obj]
    
    if {[llength $boxes] == 0} {
        puts "No active bounding boxes in current pose"
        return
    }
    
    puts "Current bounding box polygons:"
    foreach box $boxes {
        set slot [lindex $box 0]
        set name [lindex $box 1]
        set verts [lindex $box 2]
        
        puts "  $slot / $name:"
        puts "    Vertices: [llength $verts] floats ([expr {[llength $verts]/2}] points)"
        
        # Print first few vertices
        if {[llength $verts] >= 4} {
            puts "    First point: ([lindex $verts 0], [lindex $verts 1])"
            puts "    Second point: ([lindex $verts 2], [lindex $verts 3])"
        }
    }
}

#----------------------------------------------------------------------
# Display control
#----------------------------------------------------------------------
proc clearscreen {} {
    glistSetVisible 0
    redraw
}

proc see {{group 0}} {
    glistSetVisible 1
    glistSetCurGroup $group
    redraw
}

#----------------------------------------------------------------------
# Main
#----------------------------------------------------------------------
set path [find_spine_path]
puts "Using spine files from: $path"

# Available characters (uncomment to switch)
set character "spineboy-pro"
# set character "stretchyman"
# set character "vine-pro"  
# set character "coin-pro"
# set character "skeleton"

set skel_file [file join $path ${character}.json]
set atlas_file [file join $path spineboy.atlas]

# Verify files exist
if {![file exists $skel_file]} {
    error "Skeleton file not found: $skel_file"
}
if {![file exists $atlas_file]} {
    error "Atlas file not found: $atlas_file"
}

# Setup and display
set orig [setup $skel_file $atlas_file $CHARACTER_HEIGHT]
see 0

puts ""
puts "Commands:"
puts "  see ?group?         - Show display group"
puts "  clearscreen         - Hide all"
puts "  show_bounding_boxes \$orig - Show physics polygons"
puts "  sp::getSize \$orig   - Get current size in degrees"
puts "  sp::setScale \$orig val - Set scale directly"
