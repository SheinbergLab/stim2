# examples/image/image_multi_instance.tcl
# Demonstrate shared texture with multiple image instances
# One texture, many objects - efficient for repeated stimuli

workspace::reset

proc image_multi_instance { count spacing scale } {
    glistInit 1
    resetObjList
    imageTextureReset
    
    set imgdir ./assets
    set imgname backpack.png
    
    # Load texture ONCE
    set tex [imageTextureLoad [file join $imgdir $imgname]]
    
    # Create multiple image objects from same texture
    set half [expr {($count - 1) / 2.0}]
    for {set i 0} {$i < $count} {incr i} {
        set obj [image $tex]
        set x [expr {($i - $half) * $spacing}]
        translateObj $obj $x 0 0
        scaleObj $obj $scale $scale
        
        # Vary brightness across instances for visual distinction
        set brightness [expr {($i / double($count - 1)) * 0.4 - 0.2}]
        imageBrightness $obj $brightness
        
        glistAddObject $obj 0
    }
    
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

workspace::export image_multi_instance {
    count   {int 1 10 1 5 "Number of Instances"}
    spacing {float 0.5 3.0 0.1 1.5 "Spacing"}
    scale   {float 0.5 3.0 0.1 1.0 "Scale"}
}
