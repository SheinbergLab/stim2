# examples/motionpatch/mp_shape.tcl
# Shape-Defined Motion Demonstration
# Demonstrates: image masks, figure/ground segregation, two-color mode
#
# Motion dots are masked by a shape - dots inside the shape can have
# different color/luminance than dots outside, creating figure/ground
# segregation purely from motion and luminance cues.

load_Impro

# ============================================================
# STIM CODE
# ============================================================

# Render polygon to RGBA texture using shader image system
proc mp_render_shape {x y size r g b} {
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
    dl_local pix [img_imgtolist $poly]
    
    # Cleanup impro images
    img_delete $img $poly
    
    # Create shader image texture
    set tex [shaderImageCreate $pix $width $height linear]
    return $tex
}

# Generate circle vertices
proc mp_make_circle {npoints} {
    set step [expr {2.0 * 3.14159265 / $npoints}]
    dl_local angles [dl_fromto 0 [expr {2.0 * 3.14159265}] $step]
    dl_local x [dl_mult [dl_cos $angles] 0.7]
    dl_local y [dl_mult [dl_sin $angles] 0.7]
    dl_return [dl_llist $x $y]
}

# Generate square vertices
proc mp_make_square {} {
    dl_local x [dl_flist -0.5 0.5 0.5 -0.5 -0.5]
    dl_local y [dl_flist -0.5 -0.5 0.5 0.5 -0.5]
    dl_return [dl_llist $x $y]
}

# Generate arrow vertices
proc mp_make_arrow {} {
    # Arrow pointing up, normalized to fit in -1 to 1
    dl_local x [dl_flist -0.15 -0.15 -0.4 0.0 0.4 0.15 0.15 -0.15]
    dl_local y [dl_flist -0.6 0.1 0.1 0.7 0.1 0.1 -0.6 -0.6]
    dl_return [dl_llist $x $y]
}

# Generate star vertices
proc mp_make_star {npoints} {
    set outer 0.7
    set inner 0.35
    set n [expr {$npoints * 2}]
    set step [expr {2.0 * 3.14159265 / $n}]
    # Generate exactly n points
    dl_local indices [dl_fromto 0 $n]
    dl_local angles [dl_mult $indices $step]
    # Alternate between outer and inner radius
    dl_local radii [dl_replicate [dl_flist $outer $inner] $npoints]
    dl_local x [dl_mult [dl_cos $angles] $radii]
    dl_local y [dl_mult [dl_sin $angles] $radii]
    dl_return [dl_llist $x $y]
}

proc mp_shape_setup {shape lumdiff} {
    glistInit 1
    resetObjList
    shaderImageReset
    
    set pedestal 0.6
    set nDots 2000
    set texSize 256
    
    # Generate shape vertices based on selection
    switch $shape {
        circle { dl_local verts [mp_make_circle 64] }
        square { dl_local verts [mp_make_square] }
        arrow  { dl_local verts [mp_make_arrow] }
        star   { dl_local verts [mp_make_star 5] }
    }
    
    # Create mask texture from shape
    set tex [mp_render_shape $verts:0 $verts:1 $texSize 255 255 255]
    set texID [shaderImageID $tex]
    
    # Calculate colors based on luminance difference
    set c1 [expr {$pedestal + $lumdiff / 2.0}]
    set c2 [expr {$pedestal - $lumdiff / 2.0}]
    
    # Create metagroup to hold both patches
    set mg [metagroup]
    objName $mg patch
    
    # Patch 1: dots INSIDE shape (samplermaskmode 1 = use alpha)
    set mp1 [motionpatch $nDots 0.01 20]
    objName $mp1 dots_inside
    motionpatch_pointsize $mp1 4.0
    motionpatch_color $mp1 $c1 $c1 $c1 1.0
    motionpatch_masktype $mp1 1
    motionpatch_coherence $mp1 1.0
    motionpatch_direction $mp1 0.0
    motionpatch_speed $mp1 0.002
    motionpatch_setSampler $mp1 $texID 0
    motionpatch_samplermaskmode $mp1 1
    metagroupAdd $mg $mp1
    
    # Patch 2: dots OUTSIDE shape (samplermaskmode 2 = use 1-alpha)
    set mp2 [motionpatch $nDots 0.01 20]
    objName $mp2 dots_outside
    motionpatch_pointsize $mp2 4.0
    motionpatch_color $mp2 $c2 $c2 $c2 1.0
    motionpatch_masktype $mp2 1
    motionpatch_coherence $mp2 1.0
    motionpatch_direction $mp2 3.14159265
    motionpatch_speed $mp2 0.002
    motionpatch_setSampler $mp2 $texID 0
    motionpatch_samplermaskmode $mp2 2
    metagroupAdd $mg $mp2
    
    scaleObj $mg 8.0 8.0
    
    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}
    
# ---- Adjuster helper procs ----

# Motion for inside dots
proc mp_shape_set_motion_inside {name coherence direction speed} {
    motionpatch_coherence $name $coherence
    motionpatch_direction $name [expr {$direction * 3.14159265 / 180.0}]
    motionpatch_speed $name $speed
}

proc mp_shape_get_motion_inside {name} {
    dict create coherence 1.0 direction 0 speed 0.002
}

# Motion for outside dots
proc mp_shape_set_motion_outside {name coherence direction speed} {
    motionpatch_coherence $name $coherence
    motionpatch_direction $name [expr {$direction * 3.14159265 / 180.0}]
    motionpatch_speed $name $speed
}

proc mp_shape_get_motion_outside {name} {
    dict create coherence 1.0 direction 180 speed 0.002
}

proc mp_shape_set_pointsize {name pointsize} {
    motionpatch_pointsize $name $pointsize
}

proc mp_shape_get_pointsize {name} {
    dict create pointsize 4.0
}

# Mask rotation (requires updated motionpatch module)
# Note: rotates both inside and outside patches together
proc mp_shape_set_rotation {rotation} {
    # Convert degrees to radians
    set rad [expr {$rotation * 3.14159265 / 180.0}]
    motionpatch_maskrotation dots_inside $rad
    motionpatch_maskrotation dots_outside $rad
}

proc mp_shape_get_rotation {} {
    dict create rotation 0
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup mp_shape_setup {
    shape   {choice {circle square arrow star} circle "Shape"}
    lumdiff {float 0.0 0.5 0.05 0.0 "Luminance Difference"}
} -adjusters {shape_mask_rotation shape_motion_inside shape_motion_outside shape_pointsize_inside shape_pointsize_outside shape_transform} \
  -label "Shape-Defined Motion"

# Mask rotation (applies to both inside and outside patches)
workspace::adjuster shape_mask_rotation {
    rotation {float 0 360 5 0 "Mask Rotation (deg)"}
} -target {} -proc mp_shape_set_rotation -getter mp_shape_get_rotation \
  -label "Mask Rotation"

workspace::adjuster shape_motion_inside {
    coherence {float 0.0 1.0 0.05 1.0 "Coherence"}
    direction {float 0 360 15 0 "Direction (deg)"}
    speed     {float 0.0 0.01 0.0005 0.002 "Speed"}
} -target dots_inside -proc mp_shape_set_motion_inside -getter mp_shape_get_motion_inside \
  -label "Inside Motion"

workspace::adjuster shape_motion_outside {
    coherence {float 0.0 1.0 0.05 1.0 "Coherence"}
    direction {float 0 360 15 180 "Direction (deg)"}
    speed     {float 0.0 0.01 0.0005 0.002 "Speed"}
} -target dots_outside -proc mp_shape_set_motion_outside -getter mp_shape_get_motion_outside \
  -label "Outside Motion"

workspace::adjuster shape_pointsize_inside {
    pointsize {float 1.0 10.0 0.5 4.0 "Dot Size"}
} -target dots_inside -proc mp_shape_set_pointsize -getter mp_shape_get_pointsize \
  -label "Inside Dot Size"

workspace::adjuster shape_pointsize_outside {
    pointsize {float 1.0 10.0 0.5 4.0 "Dot Size"}
} -target dots_outside -proc mp_shape_set_pointsize -getter mp_shape_get_pointsize \
  -label "Outside Dot Size"

workspace::adjuster shape_transform -template scale -target patch \
  -label "Patch Size"
