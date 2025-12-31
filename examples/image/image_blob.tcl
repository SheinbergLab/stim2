# examples/image/image_blob.tcl
# Generate smooth procedural blob shapes
# Demonstrates curve::cubic interpolation + impro + imageTextureFromList

package require curve
package require impro

workspace::reset

# Create a random blob polygon with n control points
proc create_blob_points { n radius variance seed } {
    dl_srand $seed
    
    # Generate points around a circle with random variance
    dl_local angles [dl_mult [dl_fromto 0 $n] [expr {2.0 * 3.14159265 / $n}]]
    dl_local radii [dl_add $radius [dl_mult $variance [dl_sub [dl_urand $n] 0.5]]]
    
    dl_local x [dl_mult $radii [dl_cos $angles]]
    dl_local y [dl_mult $radii [dl_sin $angles]]
    
    dl_return [dl_llist $x $y]
}

# Render blob to image and create texture
proc render_blob { x y size r g b } {
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

proc image_blob { npoints variance seed size } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    # Generate smooth blob control points
    dl_local pts [create_blob_points $npoints 0.7 $variance $seed]
    
    # Cubic interpolation for smooth curve (20 segments per control point)
    dl_local smooth [curve::cubic $pts:0 $pts:1 20]
    
    # Render to texture (white blob)
    set tex [render_blob $smooth:0 $smooth:1 $size 255 255 255]
    
    # Create image object
    set obj [image $tex]
    scaleObj $obj 8.0 8.0
    
    glistAddObject $obj 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export image_blob {
    npoints  {int 3 12 1 5 "Control Points"}
    variance {float 0.0 1.0 0.05 0.4 "Shape Variance"}
    seed     {int 0 9999 1 42 "Random Seed (0=auto)"}
    size     {choice {128 256 512} 256 "Texture Size"}
}
