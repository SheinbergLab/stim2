# examples/video/video_basic.tcl
# Basic video playback demonstration
# Demonstrates: video loading, playback controls, visual adjustments, masking
#
# Uses metagroup to separate video object from display transforms:
#   - demo_video (video object): the video with its shader controls
#   - demo_group (metagroup): scale, rotation, position
#
# The video module uses FFmpeg for decoding with OpenGL for rendering,
# providing real-time shader-based visual adjustments suitable for
# psychophysics experiments.

# ============================================================
# VIDEO ASSET HELPER
# ============================================================
# Add this to your initialization code alongside imageAsset/textureAsset:
#
#   proc videoAsset {filename} {
#       return [video [assetFind $filename]]
#   }

proc videoAsset {filename} {
    return [video [assetFind $filename]]
}

# ============================================================
# FIND AVAILABLE VIDEOS
# ============================================================

proc find_video_files {} {
    set videos {}
    
    # Search in asset directories using assetFind's search path
    if {[info commands assetPath] ne ""} {
        foreach dir [assetPath] {
            foreach pattern {"*.mp4" "*.avi" "*.mov" "*.mkv"} {
                foreach f [glob -nocomplain [file join $dir $pattern]] {
                    lappend videos $f
                }
                # Also check a videos subdirectory
                foreach f [glob -nocomplain [file join $dir videos $pattern]] {
                    lappend videos $f
                }
            }
        }
    }
    
    # Also check current directory and common local paths
    foreach pattern {
        "*.mp4" "*.avi" "*.mov" "*.mkv"
        "videos/*.mp4" "videos/*.avi" "videos/*.mov"
        "pixabay_vids/*.mp4"
    } {
        foreach f [glob -nocomplain $pattern] {
            lappend videos $f
        }
    }
    
    return [lsort -unique $videos]
}

# Build list of available videos with display names
set ::video_files [find_video_files]
set ::video_choices {}
if {[llength $::video_files] > 0} {
    set ::video_file [lindex $::video_files 0]
    foreach f $::video_files {
        # Create {value label} pair: full path for value, short name for display
        lappend ::video_choices [list $f "[file tail [file rootname $f]]"]
    }
} else {
    set ::video_file ""
    set ::video_choices {{" " "(no videos found)"}}
}

# ============================================================
# STIM CODE - Copy this section to your project
# ============================================================

proc video_basic_setup {{videofile ""}} {
    # Use provided file, or global default
    if {$videofile eq "" || $videofile eq "(no videos found)"} {
        set videofile $::video_file
    }
    
    if {$videofile eq "" || $videofile eq "(no videos found)" || ![file exists $videofile]} {
        puts "Error: No video file found. Set ::video_file or pass a path."
        puts "Usage: video_basic_setup /path/to/video.mp4"
        return
    }
    
    glistInit 1
    resetObjList
    
    # Load video
    set v [video $videofile]
    if {$v < 0} {
        puts "Error: Failed to load video: $videofile"
        return
    }
    objName $v demo_video
    set ::demo_video $v
    
    # Start paused
    videoPause $v 1
    videoRepeat $v 0
    
    # Wrap in metagroup for display transforms
    set mg [metagroup]
    metagroupAdd $mg $v
    objName $mg demo_group
    
    # Scale to reasonable size
    set w [expr {0.8 * [screen_set HalfScreenDegreeX] * 2}]
    set h [expr {0.8 * [screen_set HalfScreenDegreeY] * 2}]
    scaleObj $mg $w $h
    
    glistAddObject $mg 0
    glistSetDynamic 0 1    ;# Enable animation timer for video
    glistSetVisible 1
    glistSetCurGroup 0
    redraw
    
    puts "Video loaded: [file tail $videofile]"
    puts "Commands: play, pause, restart, grayscale, invert, reset_effects"
}

# ============================================================
# SIMPLE CONTROL PROCS
# ============================================================

proc play {} {
    videoPause $::demo_video 0
}

proc pause {} {
    videoPause $::demo_video 1
}

proc restart {} {
    videoSeek $::demo_video 0.0
    videoPause $::demo_video 0
}

proc grayscale {{on 1}} {
    videoGrayscale $::demo_video $on
    redraw
}

proc invert {{on 1}} {
    videoInvert $::demo_video $on
    redraw
}

proc reset_effects {} {
    videoBrightness $::demo_video 0.0
    videoContrast $::demo_video 1.0
    videoGamma $::demo_video 1.0
    videoOpacity $::demo_video 1.0
    videoGrayscale $::demo_video 0
    videoInvert $::demo_video 0
    videoColorGains $::demo_video 1.0 1.0 1.0
    videoMask $::demo_video 0 0.5 0.5 0.2 0.2 0.05
    redraw
}

# ============================================================
# ADJUSTER HELPER PROCS (for workspace UI)
# ============================================================

# ---- Playback Controls ----

proc video_playback_action {target action} {
    switch $action {
        play    { videoPause $target 0 }
        pause   { videoPause $target 1 }
        restart { videoSeek $target 0.0; videoPause $target 0 }
    }
    return
}

# ---- Visual Adjustments ----

proc video_set_display {target brightness contrast gamma opacity} {
    videoBrightness $target $brightness
    videoContrast $target $contrast
    videoGamma $target $gamma
    videoOpacity $target $opacity
    redraw
    return
}

proc video_get_display {{target {}}} {
    dict create \
        brightness [videoBrightness $target] \
        contrast [videoContrast $target] \
        gamma [videoGamma $target] \
        opacity [videoOpacity $target]
}

# ---- Color Adjustments ----

proc video_set_color_gains {target r g b} {
    videoColorGains $target $r $g $b
    redraw
    return
}

proc video_get_color_gains {{target {}}} {
    # videoColorGains doesn't have a getter - return defaults
    dict create r 1.0 g 1.0 b 1.0
}

# ---- Visual Effects ----

proc video_set_effects {target grayscale invert} {
    videoGrayscale $target $grayscale
    videoInvert $target $invert
    redraw
    return
}

proc video_get_effects {{target {}}} {
    dict create \
        grayscale [videoGrayscale $target] \
        invert [videoInvert $target]
}

# ---- Gaze-Contingent Mask ----

proc video_set_mask {target mode cx cy radius height feather} {
    videoMask $target $mode $cx $cy $radius $height $feather
    redraw
    return
}

proc video_get_mask {{target {}}} {
    lassign [videoMask $target] mode cx cy radius width height feather
    dict create mode $mode cx $cx cy $cy radius $radius height $height feather $feather
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Main setup with video file dropdown
workspace::setup video_basic_setup [list \
    videofile [list choice $::video_choices $::video_file "Video File"] \
] -adjusters {
    video_playback
    video_display
    video_color
    video_effects
    video_mask
    video_transform
    video_position
} -label "Video Basic"

# ---- Playback Controls (Action Buttons) ----
workspace::adjuster video_playback {
    play    {action "▶ Play"}
    pause   {action "⏸ Pause"}
    restart {action "⏮ Restart"}
} -target demo_video -proc video_playback_action -label "Playback"

# ---- Basic Display Adjustments ----
workspace::adjuster video_display {
    brightness {float -1.0 1.0 0.01 0.0 "Brightness"}
    contrast   {float 0.0 2.0 0.01 1.0 "Contrast"}
    gamma      {float 0.1 3.0 0.01 1.0 "Gamma"}
    opacity    {float 0.0 1.0 0.01 1.0 "Opacity"}
} -target demo_video -proc video_set_display -getter video_get_display -label "Display"

# ---- Color Adjustments ----
workspace::adjuster video_color {
    r {float 0.0 2.0 0.01 1.0 "Red Gain"}
    g {float 0.0 2.0 0.01 1.0 "Green Gain"}
    b {float 0.0 2.0 0.01 1.0 "Blue Gain"}
} -target demo_video -proc video_set_color_gains -getter video_get_color_gains \
  -label "Color Gains" -colorpicker

# ---- Visual Effects ----
workspace::adjuster video_effects {
    grayscale {bool 0 "Grayscale"}
    invert    {bool 0 "Invert Colors"}
} -target demo_video -proc video_set_effects -getter video_get_effects -label "Effects"

# ---- Gaze-Contingent Mask ----
workspace::adjuster video_mask {
    mode    {choice {0 1 2 3} 0 "Mode (0=off, 1=circle, 2=rect, 3=inv)"}
    cx      {float 0.0 1.0 0.01 0.5 "Center X"}
    cy      {float 0.0 1.0 0.01 0.5 "Center Y"}
    radius  {float 0.0 1.0 0.01 0.2 "Radius/Width"}
    height  {float 0.0 1.0 0.01 0.2 "Height"}
    feather {float 0.0 0.5 0.01 0.05 "Feather"}
} -target demo_video -proc video_set_mask -getter video_get_mask -label "Mask"

# ---- Transform (on metagroup) ----
workspace::adjuster video_transform -template scale -target demo_group \
    -label "Scale"

workspace::adjuster video_position -template position -target demo_group \
    -label "Position"

# ============================================================
# AUTO-RUN if video file is available
# ============================================================
if {$::video_file ne "" && $::video_file ne "(no videos found)"} {
    video_basic_setup $::video_file
}
