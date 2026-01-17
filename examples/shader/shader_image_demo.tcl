# examples/shader/shader_image_demo.tcl
# Shader with image textures demonstration
# Demonstrates: shaders with texture samplers, image loading, blend mixing
#
# This example shows how to use images as texture inputs to shaders.
# The noisyimage2 shader blends between two images with animated noise.
#
# Key concepts:
#   - assetFind: locates asset files by name
#   - shaderImageLoad: loads an image file as a texture
#   - shaderImageID: gets the OpenGL texture ID
#   - shaderObjSetSampler: binds a texture to a shader sampler slot
#   - tex0, tex1, etc.: sampler uniform names in the shader

# ============================================================
# STIM CODE
# ============================================================

# Image blend shader - smoothly mixes two images with noisy alpha
proc image_blend_setup {image1 image2} {
    glistInit 1
    resetObjList
    shaderImageReset
    shaderDeleteAll
    
    set shader [shaderBuild noisyimage2]
    set sobj [shaderObj $shader]
    objName $sobj shader_obj
    
    # Load images as textures (assetFind locates full path)
    set img1 [shaderImageLoad [assetFind $image1]]
    set img2 [shaderImageLoad [assetFind $image2]]
    
    # Bind textures to sampler slots
    shaderObjSetSampler $sobj [shaderImageID $img1] 0  ;# tex0
    shaderObjSetSampler $sobj [shaderImageID $img2] 1  ;# tex1
    
    scaleObj $sobj 6.0 6.0
    
    glistAddObject $sobj 0
    glistSetCurGroup 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# ---- Uniform setter procs ----
proc set_ib_freq {name val} {
    shaderObjSetUniform $name freq $val
    redraw
}

# Transform adjusters
workspace::adjuster shader_obj_scale -template scale -target shader_obj \
    -defaults {scale 6.0}
workspace::adjuster shader_obj_rotation -template rotation -target shader_obj

# Blend frequency adjuster
workspace::adjuster ib_freq {
    value {float 0.1 2.0 0.05 0.5 "Hz"}
} -target shader_obj -proc set_ib_freq -label "Blend Frequency"

# Main setup with image selection
workspace::setup image_blend_setup {
    image1 {string "backpack.png" "Image 1"}
    image2 {string "movie_ticket.png" "Image 2"}
} -adjusters {shader_obj_scale shader_obj_rotation ib_freq} \
  -label "Image Blend Shader"
