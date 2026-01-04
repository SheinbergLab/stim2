# examples/image/image_blob.tcl
# Procedural blob shape generation
# Demonstrates: curve::cubic interpolation, curve::clipper union, 
#               impro rendering, imageTextureFromList
#
# Two modes:
#   Simple: single smooth blob from control points around a circle
#   Compound: union of multiple random polygons (shapematch style)
#

package require curve
package require impro

# ============================================================
# STIM CODE - Simple Blob
# ============================================================

# Create random blob control points around a circle
proc create_blob_points {n radius variance seed} {
    dl_srand $seed
    
    # Generate points around a circle with random radial variance
    dl_local angles [dl_mult [dl_fromto 0 $n] [expr {2.0 * 3.14159265 / $n}]]
    dl_local radii [dl_add $radius [dl_mult $variance [dl_sub [dl_urand $n] 0.5]]]
    
    dl_local x [dl_mult $radii [dl_cos $angles]]
    dl_local y [dl_mult $radii [dl_sin $angles]]
    
    dl_return [dl_llist $x $y]
}

# ============================================================
# STIM CODE - Compound Blob (union of random polygons)
# ============================================================

# Add n random points to existing polygon, maintaining minimum distance
proc add_points_to_poly {poly n {mindist 0.05}} {
    set maxtries 1000
    set mind2 [expr {$mindist * $mindist}]
    
    for {set i 0} {$i < $n} {incr i} {
        set done 0
        set tries 0
        while {!$done && $tries < $maxtries} {
            # Random point in central region
            dl_local randoms [dl_sub [dl_mult 0.8 [dl_urand 2]] 0.4]
            set x [dl_get $randoms 0]
            set y [dl_get $randoms 1]
            
            # Check distance to all existing points
            dl_local xd [dl_sub $poly:x $x]
            dl_local yd [dl_sub $poly:y $y]
            dl_local d2 [dl_add [dl_mult $xd $xd] [dl_mult $yd $yd]]
            
            if {[dl_min $d2] >= $mind2} {
                set done 1
            }
            incr tries
        }
        if {$tries == $maxtries} {
            error "unable to place points"
        }
        
        # Insert point at closest edge
        set index [curve::closestPoint $poly:x $poly:y $x $y]
        dl_insert $poly:x [expr {$index + 1}] $x
        dl_insert $poly:y [expr {$index + 1}] $y
    }
    return $poly
}

# Create a random non-self-intersecting polygon
proc create_random_poly {nverts} {
    set done 0
    set g [dg_create]
    
    while {!$done} {
        # Start with 3 random points
        dl_set $g:x [dl_sub [dl_mult 0.6 [dl_urand 3]] 0.3]
        dl_set $g:y [dl_sub [dl_mult 0.6 [dl_urand 3]] 0.3]
        
        # Add remaining vertices
        add_points_to_poly $g $nverts
        
        # Check for self-intersection
        if {![curve::polygonSelfIntersects $g:x $g:y]} {
            set done 1
        }
    }
    return $g
}

# Create union of multiple polygons
proc create_poly_union {polys} {
    set s 10000.0  ;# clipper works with integers, scale up/down
    
    dl_local ps [dl_llist]
    foreach poly $polys {
        # Cubic interpolate each polygon
        dl_local interped [curve::cubic $poly:x $poly:y 20]
        dl_append $ps $interped
    }
    
    # Scale to integers, union, scale back
    dl_local polys_int [dl_int [dl_mult $ps $s]]
    dl_local union [dl_div [curve::clipper $polys_int] $s]
    
    dl_return $union
}

# Check if shape can be made symmetrical (single path after clip)
proc symmetrical_valid {v} {
    set s 1000.0
    # Clip region: left half
    dl_local clip [dl_int [dl_mult [dl_llist [dl_flist -.5 0 0 -.5] [dl_flist -.5 -.5 .5 .5]] $s]]
    dl_local poly [dl_int [dl_mult $v $s]]
    dl_local left_half [curve::clipper [dl_llist $poly] [dl_llist $clip]]
    
    if {[dl_length $left_half] == 1} {
        return 1
    } else {
        return 0
    }
}

# Make shape symmetrical by cutting left half and mirroring
proc make_symmetrical {v} {
    set s 1000.0
    # Clip to left half
    dl_local clip [dl_int [dl_mult [dl_llist [dl_flist -.5 0 0 -.5] [dl_flist -.5 -.5 .5 .5]] $s]]
    dl_local poly [dl_int [dl_mult $v $s]]
    dl_local left_half [curve::clipper [dl_llist $poly] [dl_llist $clip]]
    
    # Mirror to create right half (negate x)
    dl_local right_half [dl_mult $left_half [dl_llist [dl_ilist -1 1]]]
    
    # Union left and right
    dl_local union [dl_div [curve::clipper [dl_llist $left_half:0 $right_half:0]] $s]
    
    dl_return [dl_llist $union:0:0 $union:0:1]
}

# ============================================================
# STIM CODE - Rendering
# ============================================================

# Render polygon to RGBA texture
proc render_blob {x y size r g b} {
    set width $size
    set height $size
    set depth 4
    set half [expr {$size / 2}]
    
    # Scale normalized coords (-1 to 1) to pixel coords
    dl_local xscaled [dl_add [dl_mult $x $half] $half]
    dl_local yscaled [dl_add [dl_mult [dl_negate $y] $half] $half]
    
    # Create image and draw filled polygon
    set img [img_create -width $width -height $height -depth $depth]
    set poly [img_drawPolygon $img $xscaled $yscaled $r $g $b 255]
    
    # Convert to DYN_LIST for texture
    dl_local pix [img_img2list $poly]
    
    # Cleanup impro images
    img_delete $img $poly
    
    # Create texture (RGBA = 4 channels)
    set tex [imageTextureFromList $pix $width $height LINEAR]
    return $tex
}

# ============================================================
# SETUP PROCS
# ============================================================

# Simple blob setup
proc blob_setup {npoints variance seed size} {
    glistInit 1
    resetObjList
    imageTextureReset
    
    dl_srand $seed
    
    # Generate smooth blob control points
    dl_local pts [create_blob_points $npoints 0.7 $variance $seed]
    
    # Cubic interpolation for smooth curve
    dl_local smooth [curve::cubic $pts:0 $pts:1 20]
    
    # Render to texture
    set tex [render_blob $smooth:0 $smooth:1 $size 255 255 255]
    
    # Create image object
    set img [image $tex]
    objName $img blob_img
    scaleObj $img 6.0 6.0
    
    glistAddObject $img 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# Compound blob setup (union of random polygons)
proc compound_setup {npolys nverts symmetric seed size} {
    glistInit 1
    resetObjList
    imageTextureReset
    
    dl_srand $seed
    
    set maxtries 100
    set tries 0
    set done 0
    
    while {!$done && $tries < $maxtries} {
        incr tries
        
        # Create random polygons
        set polys {}
        for {set i 0} {$i < $npolys} {incr i} {
            lappend polys [create_random_poly $nverts]
        }
        
        # Create union
        dl_local union [create_poly_union $polys]
        
        # Check for valid single union that can be made symmetrical
        if {[dl_length $union] == 1} {
            dl_local shape [dl_llist $union:0:0 $union:0:1]
            if {!$symmetric || [symmetrical_valid $shape]} {
                set done 1
            }
        }
        
        if {!$done} {
            foreach p $polys {
                dg_delete $p
            }
        }
    }
    
    if {!$done} {
        error "unable to create valid compound shape after $maxtries tries"
    }
    
    # Apply symmetry if requested
    if {$symmetric} {
        dl_local final [make_symmetrical $shape]
    } else {
        dl_local final $shape
    }
    
    # Cleanup polygon datagroups
    foreach p $polys {
        dg_delete $p
    }
    
    # Render to texture
    set tex [render_blob $final:0 $final:1 $size 255 255 255]
    
    # Create image object
    set img [image $tex]
    objName $img blob_img
    scaleObj $img 6.0 6.0
    
    glistAddObject $img 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Simple blob - single smooth shape
workspace::setup blob_setup {
    npoints  {int 3 12 1 5 "Control Points"}
    variance {float 0.0 1.0 0.05 0.4 "Shape Variance"}
    seed     {int 0 9999 1 42 "Random Seed"}
    size     {choice {128 256 512} 256 "Texture Size"}
} -adjusters {blob_scale blob_rotation} -label "Simple Blob"

# Compound blob - union of random polygons (shapematch style)
workspace::variant compound {
    npolys    {int 1 4 1 3 "Number of Polygons"}
    nverts    {int 0 8 1 2 "Extra Vertices per Polygon"}
    symmetric {bool 1 "Symmetrical"}
    seed      {int 0 9999 1 42 "Random Seed"}
    size      {choice {128 256 512} 256 "Texture Size"}
} -proc compound_setup -adjusters {blob_scale blob_rotation} -label "Compound Shape"

# Transform adjusters (shared by both variants)
workspace::adjuster blob_scale -template size2d -target blob_img \
    -defaults {width 6.0 height 6.0}
workspace::adjuster blob_rotation -template rotation -target blob_img
