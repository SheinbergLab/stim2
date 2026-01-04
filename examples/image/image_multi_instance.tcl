# examples/image/image_multi_instance.tcl
# Shared texture with multiple image instances
# Demonstrates: texture pooling, efficient repeated stimuli
#
# One texture loaded once, multiple image objects created from it.
# Each instance can have independent transforms and shader settings.
#
# Setup parameters (all require re-creation):
#   - filename: source image
#   - count: number of instances
#   - spacing: horizontal distance between instances
#   - scale: size of each instance

# ============================================================
# STIM CODE
# ============================================================

proc multi_setup {filename count spacing scale} {
    glistInit 1
    resetObjList
    imageTextureReset
    
    # Load texture ONCE
    set tex [textureAsset $filename]
    
    # Create multiple image objects from same texture
    set half [expr {($count - 1) / 2.0}]
    for {set i 0} {$i < $count} {incr i} {
        set obj [image $tex]
        set x [expr {($i - $half) * $spacing}]
        translateObj $obj $x 0 0
        scaleObj $obj $scale $scale
        
        # Vary brightness across instances for visual distinction
        if {$count > 1} {
            set brightness [expr {($i / double($count - 1)) * 0.4 - 0.2}]
            imageBrightness $obj $brightness
        }
        
        glistAddObject $obj 0
    }
    
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup multi_setup {
    filename {choice {backpack.png movie_ticket.png} backpack.png "Image"}
    count    {int 1 10 1 5 "Number of Instances"}
    spacing  {float 0.5 3.0 0.1 1.5 "Spacing"}
    scale    {float 0.5 3.0 0.1 1.0 "Scale"}
} -label "Multi-Instance (Shared Texture)"
