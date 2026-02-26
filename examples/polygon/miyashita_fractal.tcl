# examples/polygon/miyashita_fractal.tcl
# Miyashita-style fractal stimulus generation
# Demonstrates: midpoint-displacement fractals, metagroups, dynamics
#
# Creates "fractal" stimuli following the method of Miyashita et al. (1991):
#   - Each layer starts as an N-sided regular polygon
#   - Midpoints between vertices are recursively displaced perpendicular
#     to each edge by random amounts, doubling vertex count at each level
#   - Multiple layers are overlaid to form the composite stimulus
#
# The key parameters controlling stimulus appearance:
#   - nlayers: number of overlaid polygon layers
#   - depth: recursion levels for midpoint displacement (complexity)
#   - base_n: range of initial polygon edge counts
#
# Uses metagroup to bundle the composite so it can be transformed as a unit.

# ============================================================
# FRACTAL GENERATION (from testfractal.tcl)
# ============================================================

# Generation parameters
array set ::miya {
    nlayers     3
    min_n       2
    max_n       5
    min_depth   2
    max_depth   4
    filled      1
    linewidth   2.0
}

# Generate random fractal parameters for each layer
proc miya_make_params {} {
    set fp [list]
    for {set i 0} {$i < $::miya(nlayers)} {incr i} {
        set vals ""
        set n [expr {$::miya(min_n) + int(($::miya(max_n) - $::miya(min_n) + 1) * rand())}]
        set vals "$vals n $n"
        set depth [expr {$::miya(min_depth) + int(($::miya(max_depth) - $::miya(min_depth) + 1) * rand())}]
        set GAs [dl_tcllist [dl_sub [dl_urand $depth] 0.5]]
        set vals "$vals GAs [list $GAs]"
        lappend fp $vals
    }
    return $fp
}

# Deflect midpoints perpendicular to edges by amount GA
proc miya_deflect_midpoints {xylists GA} {
    set x $xylists:0
    set y $xylists:1

    # Shifted (cycled) lists for next vertex
    dl_local cx [dl_cycle $x -1]
    dl_local cy [dl_cycle $y -1]

    # Midpoints
    dl_local mx [dl_div [dl_add $x $cx] 2.0]
    dl_local my [dl_div [dl_add $y $cy] 2.0]

    # Edge direction
    dl_local dx [dl_sub $cx $x]
    dl_local dy [dl_sub $cy $y]
    dl_local theta [dl_atan2 $dy $dx]

    # Displace midpoints perpendicular to edge
    dl_local newx [dl_add $mx [dl_mult $GA [dl_sin $theta]]]
    dl_local newy [dl_sub $my [dl_mult $GA [dl_cos $theta]]]

    # Interleave original vertices with displaced midpoints
    dl_return [dl_llist [dl_interleave $x $newx] [dl_interleave $y $newy]]
}

# Generate one fractal layer: start with n-gon, recursively displace midpoints
proc miya_make_layer {n GAs} {
    set angle_step [expr {3.14159265 * 2.0 / $n}]
    dl_local angles [dl_mult [dl_fromto 0 $n] $angle_step]
    dl_local x [dl_cos $angles]
    dl_local y [dl_sin $angles]
    dl_local xy [dl_llist $x $y]

    foreach GA $GAs {
        dl_local xy [miya_deflect_midpoints $xy $GA]
    }

    # Close the polygon
    dl_append $xy:0 [dl_first $xy:0]
    dl_append $xy:1 [dl_first $xy:1]

    # Rotate 90 degrees
    set cos_theta [expr {cos(3.14159265 / 2.0)}]
    set sin_theta [expr {sin(3.14159265 / 2.0)}]
    dl_local rx [dl_sub [dl_mult $xy:0 $cos_theta] [dl_mult $xy:1 $sin_theta]]
    dl_local ry [dl_add [dl_mult $xy:0 $sin_theta] [dl_mult $xy:1 $cos_theta]]

    dl_return [dl_llist $rx $ry]
}

# Build all layers into a single normalized pattern
proc miya_make_pattern {fparams} {
    dl_local xys [dl_llist]

    foreach params $fparams {
        dict with params {
            dl_append $xys [miya_make_layer $n $GAs]
        }
    }

    # Normalize to [-0.5, 0.5] range
    dl_return [dl_mult 0.5 [dl_div $xys [dl_max $xys]]]
}

# Generate random saturated color via HSV
proc miya_random_color {} {
    set h [expr {rand() * 6.0}]
    set s [expr {0.6 + rand() * 0.4}]
    set v [expr {0.5 + rand() * 0.5}]

    set hi [expr {int(floor($h))}]
    set f [expr {$h - $hi}]
    set p [expr {$v * (1.0 - $s)}]
    set q [expr {$v * (1.0 - $s * $f)}]
    set t [expr {$v * (1.0 - $s * (1.0 - $f))}]

    switch $hi {
        0 { return [list $v $t $p] }
        1 { return [list $q $v $p] }
        2 { return [list $p $v $t] }
        3 { return [list $p $q $v] }
        4 { return [list $t $p $v] }
        default { return [list $v $p $q] }
    }
}

# Assemble fractal layers into a metagroup
# When filled, layers are scaled progressively smaller so they nest visibly
proc miya_make_fractal {polys} {
    set n [dl_length $polys]
    set mg [metagroup]

    for {set i 0} {$i < $n} {incr i} {
        set p [polygon]
        set len [dl_length $polys:$i:0]
        polyverts $p $polys:$i:0 $polys:$i:1 [dl_zeros $len.]

        if {$::miya(filled)} {
            polytype $p triangle_fan
            set color [miya_random_color]
            polycolor $p {*}$color 1.0
            # Scale layers so they nest: largest first, smallest on top
            set s [expr {1.0 - double($i) / double($n) * 0.6}]
            scaleObj $p $s $s
        } else {
            polytype $p LINE_LOOP
            polylinewidth $p $::miya(linewidth)
            set color [miya_random_color]
            polycolor $p {*}$color 1.0
        }

        metagroupAdd $mg $p
    }

    return $mg
}

# ============================================================
# STIM CODE
# ============================================================

proc miyashita_setup {} {
    glistInit 1
    resetObjList

    set params [miya_make_params]
    dl_local polys [miya_make_pattern $params]

    set mg [miya_make_fractal $polys]
    objName $mg fractal
    scaleObj $mg 8.0 8.0

    glistAddObject $mg 0
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

proc miyashita_randomize {name} {
    miyashita_setup
}

# Generate a row of fractals for set building
proc miyashita_set_setup {} {
    glistInit 1
    resetObjList

    set n 5
    set spacing 3.2
    set start_x [expr {-$spacing * ($n - 1) / 2.0}]

    for {set i 0} {$i < $n} {incr i} {
        set params [miya_make_params]
        dl_local polys [miya_make_pattern $params]
        set mg [miya_make_fractal $polys]
        scaleObj $mg 2.8 2.8
        translateObj $mg [expr {$start_x + $i * $spacing}] 0.0
        glistAddObject $mg 0
    }

    glistSetVisible 1
    glistSetCurGroup 0
    redraw
}

proc miyashita_set_randomize {name} {
    miyashita_set_setup
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# ---- Generation parameter adjusters ----

proc miya_set_params {target nlayers min_n max_n min_depth max_depth filled linewidth} {
    set ::miya(nlayers)   [expr {int($nlayers)}]
    set ::miya(min_n)     [expr {int($min_n)}]
    set ::miya(max_n)     [expr {int($max_n)}]
    set ::miya(min_depth) [expr {int($min_depth)}]
    set ::miya(max_depth) [expr {int($max_depth)}]
    set ::miya(filled)    [expr {$filled ? 1 : 0}]
    set ::miya(linewidth) $linewidth
}

proc miya_get_params {{target {}}} {
    dict create \
        nlayers   $::miya(nlayers) \
        min_n     $::miya(min_n) \
        max_n     $::miya(max_n) \
        min_depth $::miya(min_depth) \
        max_depth $::miya(max_depth) \
        filled    $::miya(filled) \
        linewidth $::miya(linewidth)
}

proc miya_trigger {action} {
    switch $action {
        randomize { miyashita_setup }
    }
}

proc miya_trigger_set {action} {
    switch $action {
        randomize { miyashita_set_setup }
    }
}

workspace::adjuster mi_actions {
    randomize {action "New Fractal"}
} -target {} -proc miya_trigger \
  -label "Generate"

workspace::adjuster mi_set_actions {
    randomize {action "New Set"}
} -target {} -proc miya_trigger_set \
  -label "Generate"

workspace::adjuster mi_params {
    nlayers   {int 1 8 1 3 "Layers"}
    min_n     {int 2 8 1 2 "Min Sides"}
    max_n     {int 2 8 1 5 "Max Sides"}
    min_depth {int 1 6 1 2 "Min Depth"}
    max_depth {int 1 6 1 4 "Max Depth"}
    filled    {bool 1 "Filled"}
    linewidth {float 0.5 5.0 0.5 2.0 "Line Width"}
} -target fractal -proc miya_set_params -getter miya_get_params \
  -label "Parameters"

# ---- Display transform adjusters ----
workspace::adjuster fractal_scale -template scale -target fractal \
    -defaults {scale 8.0}
workspace::adjuster fractal_rotation -template rotation -target fractal

# ---- Setup definitions ----

workspace::setup miyashita_setup {} \
    -adjusters {mi_actions mi_params fractal_scale fractal_rotation} \
    -label "Miyashita Fractal"

workspace::variant miyashita_set {} -proc miyashita_set_setup \
    -adjusters {mi_set_actions mi_params} \
    -label "Stimulus Set (5)"
