# Puzzle Tiles Atlas Description
# 320x320 PNG, 10x10 grid of 32x32 tiles
#
# Tile IDs (0-indexed):
#   0 = empty/transparent
#   1 = floor (light gray or tan)
#   2 = wall (dark blue or gray) 
#   3 = pushable block (orange/yellow - stands out)
#   4 = player (green)
#   5 = goal marker (gold star or bright color)
#
# You can create this in any image editor, or use the test_tiles.png
# you already have (just rename it to puzzle_tiles.png)
#
# For quick testing, the test_tiles.png should work - just:
#   cp test_tiles.png puzzle_tiles.png
#
# The colors will just be different than described above.

# If you want to generate a proper one programmatically:

package require Tk

proc generate_puzzle_tiles {} {
    set size 320
    set tilesize 32
    
    image create photo puzzleimg -width $size -height $size
    
    # Fill with transparency (or black)
    for {set y 0} {$y < $size} {incr y} {
        for {set x 0} {$x < $size} {incr x} {
            puzzleimg put "#000000" -to $x $y
        }
    }
    
    # Tile 0 (0,0): Empty - already black
    
    # Tile 1 (1,0): Floor - light tan
    fill_tile puzzleimg 1 0 "#C4A66B" "#9A8050"
    
    # Tile 2 (2,0): Wall - dark blue  
    fill_tile puzzleimg 2 0 "#2C5F8E" "#1E4263"
    
    # Tile 3 (3,0): Pushable - orange
    fill_tile puzzleimg 3 0 "#E87E24" "#C46A1D"
    
    # Tile 4 (4,0): Player - green
    fill_tile puzzleimg 4 0 "#4CAF50" "#388E3C"
    
    # Tile 5 (5,0): Goal - gold
    fill_tile puzzleimg 5 0 "#FFD700" "#DAA520"
    
    puzzleimg write "puzzle_tiles.png" -format png
    puts "Created puzzle_tiles.png"
}

proc fill_tile {img col row color border} {
    set x1 [expr {$col * 32}]
    set y1 [expr {$row * 32}]
    set x2 [expr {$x1 + 31}]
    set y2 [expr {$y1 + 31}]
    
    # Fill interior
    for {set y [expr {$y1+2}]} {$y <= [expr {$y2-2}]} {incr y} {
        for {set x [expr {$x1+2}]} {$x <= [expr {$x2-2}]} {incr x} {
            $img put $color -to $x $y
        }
    }
    
    # Border
    for {set x $x1} {$x <= $x2} {incr x} {
        $img put $border -to $x $y1
        $img put $border -to $x [expr {$y1+1}]
        $img put $border -to $x $y2
        $img put $border -to $x [expr {$y2-1}]
    }
    for {set y $y1} {$y <= $y2} {incr y} {
        $img put $border -to $x1 $y
        $img put $border -to [expr {$x1+1}] $y
        $img put $border -to $x2 $y
        $img put $border -to [expr {$x2-1}] $y
    }
}

# Run it:
generate_puzzle_tiles
