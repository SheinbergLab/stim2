# examples/image/image_noise.tcl
# Generate procedural textures from DYN_LIST data
# Demonstrates imageTextureFromList with random pixel data

workspace::reset

proc image_noise { size seed grayscale } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    # Seed random generator for reproducibility (0 = auto-seed)
    dl_srand $seed
    
    # Calculate number of values needed
    set channels [expr {$grayscale ? 1 : 3}]
    set n [expr {$size * $size * $channels}]
    
    # Generate random pixel data in one line (0.0-1.0 uniform floats)
    dl_local pixels [dl_urand $n]
    
    # Create texture from DYN_LIST with NEAREST filtering (keeps hard edges)
    set tex [imageTextureFromList $pixels $size $size NEAREST]
    
    # Create image object from texture
    set obj [image $tex]
    scaleObj $obj 6.0 6.0
    
    glistAddObject $obj 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export image_noise {
    size      {choice {4 8 16 32 64} 8 "Grid Size"}
    seed      {int 0 9999 1 42 "Random Seed (0=auto)"}
    grayscale {bool 0 "Grayscale"}
}
