# examples/shader/shader_grid_demo.tcl
# Multiple shader instances demonstration
# Demonstrates: one shader program, multiple objects with independent uniforms
#
# This shows how shaderBuild compiles the shader once, then shaderObj
# creates independent instances that each have their own uniform values.
# Perfect for stimulus arrays, oddball detection paradigms, etc.
#
# Key concepts:
#   - shaderBuild: compiles shader program (do once)
#   - shaderObj: creates instance with own uniforms (do many times)
#   - Each instance can have different parameter values

# ============================================================
# STIM CODE
# ============================================================

# Track current settings for randomize action
set ::grid_rows 3
set ::grid_cols 3
set ::grid_oddball_diff 2.0

proc grating_grid_setup {rows cols oddball_diff} {
    # Save current settings
    set ::grid_rows $rows
    set ::grid_cols $cols
    set ::grid_oddball_diff $oddball_diff
    
    glistInit 1
    resetObjList
    shaderDeleteAll
    
    # Build shader once
    set shader [shaderBuild concentricsine]
    
    # Grid layout parameters
    set spacing 2.2
    set size 2.0
    set base_freq 6.0
    
    # Calculate offsets to center the grid
    set x_offset [expr {-($cols - 1) * $spacing / 2.0}]
    set y_offset [expr {-($rows - 1) * $spacing / 2.0}]
    
    # Pick random oddball position
    set oddball_row [expr {int(rand() * $rows)}]
    set oddball_col [expr {int(rand() * $cols)}]
    
    # Create grid of grating instances
    for {set r 0} {$r < $rows} {incr r} {
        for {set c 0} {$c < $cols} {incr c} {
            # Create new instance from same shader program
            set sobj [shaderObj $shader]
            
            # Position in grid
            set x [expr {$x_offset + $c * $spacing}]
            set y [expr {$y_offset + $r * $spacing}]
            translateObj $sobj $x $y
            scaleObj $sobj $size $size
            
            # Set common parameters (linear grating, not circular)
            shaderObjSetUniform $sobj Circular 0
            shaderObjSetUniform $sobj Envelope 1
            shaderObjSetUniform $sobj Sigma 0.18
            shaderObjSetUniform $sobj Contrast 1.0
            shaderObjSetUniform $sobj CyclesPerSec 2.0
            shaderObjSetUniform $sobj ColorR 1.0
            shaderObjSetUniform $sobj ColorG 1.0
            shaderObjSetUniform $sobj ColorB 1.0
            shaderObjSetUniform $sobj ColorA 1.0
            
            # Set spatial frequency - oddball is different
            if {$r == $oddball_row && $c == $oddball_col} {
                shaderObjSetUniform $sobj NCycles [expr {$base_freq + $oddball_diff}]
                objName $sobj oddball
            } else {
                shaderObjSetUniform $sobj NCycles $base_freq
            }
            
            glistAddObject $sobj 0
        }
    }
    
    glistSetCurGroup 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
    
    puts "Oddball at row $oddball_row, col $oddball_col"
}

# Randomize oddball position using current settings
proc randomize_oddball {args} {
    grating_grid_setup $::grid_rows $::grid_cols $::grid_oddball_diff
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Setup parameters
workspace::setup grating_grid_setup {
    rows        {int 2 5 1 3 "Rows"}
    cols        {int 2 5 1 3 "Columns"}
    oddball_diff {float 0.5 4.0 0.5 2.0 "Oddball Δ Cycles"}
} -adjusters {grid_randomize} \
  -label "Grating Grid (Oddball)"

# Action to randomize oddball position
workspace::adjuster grid_randomize {
    randomize {action "New Random Oddball"}
} -target {} -proc randomize_oddball -label "Actions"
