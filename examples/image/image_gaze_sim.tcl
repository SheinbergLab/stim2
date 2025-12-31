# examples/image/image_gaze_sim.tcl
# Simulate gaze-contingent display
# In practice, mask center would be updated from eye tracker data
# This demo allows manual positioning to test the effect

workspace::reset

# Store texture and image ID globally for updates
set ::gaze_texture_id -1
set ::gaze_image_id -1

proc gaze_sim_init { radius feather } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    set imgdir ./assets
    set imgname backpack.png
    
    # Load texture once
    set ::gaze_texture_id [imageTextureLoad [file join $imgdir $imgname]]
    
    # Create image object from texture
    set ::gaze_image_id [image $::gaze_texture_id]
    
    # Start with mask at center
    imageMask $::gaze_image_id 1 0.5 0.5 $radius $feather
    
    scaleObj $::gaze_image_id 8.0 8.0
    
    glistAddObject $::gaze_image_id 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

proc gaze_sim_update { gaze_x gaze_y radius feather } {
    if {$::gaze_image_id < 0} {
        gaze_sim_init $radius $feather
    }
    
    # Update mask position (simulating eye tracker input)
    imageMask $::gaze_image_id 1 $gaze_x $gaze_y $radius $feather
    redraw
}

workspace::export gaze_sim_update {
    gaze_x  {float 0.0 1.0 0.01 0.5 "Gaze X"}
    gaze_y  {float 0.0 1.0 0.01 0.5 "Gaze Y"}
    radius  {float 0.05 0.5 0.01 0.15 "Window Radius"}
    feather {float 0.0 0.2 0.01 0.05 "Edge Feather"}
}
