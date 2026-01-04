# examples/image/image_noise.tcl
# Generate procedural textures from DYN_LIST data
# Demonstrates imageTextureFromList with random pixel data
#
# Setup parameters (require regeneration):
#   - size: grid size determines texture resolution
#   - seed: random seed for reproducibility (0 = auto-seed)
#   - grayscale: single channel vs RGB
#
# Adjusters (real-time):
#   - scale: display size
#   - rotation: orientation

# ============================================================
# STIM CODE
# ============================================================

proc noise_setup {size seed grayscale} {
    glistInit 1
    resetObjList
    imageTextureReset
    
    # Seed random generator for reproducibility (0 = auto-seed)
    dl_srand $seed
    
    # Calculate number of values needed
    set channels [expr {$grayscale ? 1 : 3}]
    set n [expr {$size * $size * $channels}]
    
    # Generate random pixel data (0.0-1.0 uniform floats)
    dl_local pixels [dl_urand $n]
    
    # Create texture from DYN_LIST with NEAREST filtering (keeps hard edges)
    set tex [imageTextureFromList $pixels $size $size NEAREST]
    
    # Create image object from texture
    set img [image $tex]
    objName $img noise_img
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

# Main setup - all params require texture regeneration
workspace::setup noise_setup {
    size      {choice {4 8 16 32 64 128 256} 32 "Grid Size"}
    seed      {int 0 9999 1 42 "Random Seed (0=auto)"}
    grayscale {bool 0 "Grayscale"}
} -adjusters {noise_scale noise_rotation} -label "Procedural Noise"

# Adjusters for real-time display manipulation
workspace::adjuster noise_scale -template size2d -target noise_img \
    -defaults {width 6.0 height 6.0}
workspace::adjuster noise_rotation -template rotation -target noise_img
