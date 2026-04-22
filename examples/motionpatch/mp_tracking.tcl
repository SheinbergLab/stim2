# examples/motionpatch/mp_tracking.tcl
# Motion-Defined Moving Target
# Demonstrates: maskOffset uniform driving a real-time shape translation
#
# Two overlapping motionpatches share one circular mask texture.
# Background patch (samplermaskmode 2) renders dots OUTSIDE the shape.
# Target patch   (samplermaskmode 1) renders dots INSIDE  the shape.
# Both patches use identical dot color/size/density, so when all dot
# speeds are zero the scene is a uniform random dot field — the shape
# is invisible. When motion resumes, inside and outside dots move in
# different directions and the shape emerges. The mask offset is
# updated every frame via a preScript so the target glides along a
# Lissajous path.

load_Impro

# ============================================================
# STIM CODE
# ============================================================

# Render a filled circle into an RGBA texture for use as the mask.
proc mp_tracking_make_circle_tex {size} {
    set depth 4
    set half [expr {$size / 2.0}]
    set npoints 64
    set step [expr {2.0 * 3.14159265 / $npoints}]
    dl_local angles [dl_fromto 0 [expr {2.0 * 3.14159265}] $step]
    # circle spans the full texture so maskScale controls final size
    dl_local x [dl_add [dl_mult [dl_cos $angles] $half] $half]
    dl_local y [dl_add [dl_mult [dl_sin $angles] $half] $half]

    set img  [img_create -width $size -height $size -depth $depth]
    set poly [img_drawPolygon $img $x $y 255 255 255 255]
    dl_local pix [img_imgtolist $poly]
    img_delete $img $poly
    set tex [shaderImageCreate $pix $size $size linear]
    return $tex
}

# Box-Muller standard normal (used by random-walk path mode).
proc mp_tracking_randn {} {
    set u1 [expr {rand()}]
    if {$u1 < 1e-9} { set u1 1e-9 }
    set u2 [expr {rand()}]
    return [expr {sqrt(-2.0 * log($u1)) * cos(2.0 * 3.14159265 * $u2)}]
}

# Pre-bake a simplex-noise path using dlsh's vectorized dl_simplexNoise2D.
# Stores the path as two Tcl lists for O(1) per-frame lookup. This is the
# same path-generation strategy you'd use for a real experiment: bake
# once at trial setup, replay deterministically (same seed => same path).
#
# Smooth-pursuit-friendly defaults: rate=0.25 gives dominant spectral
# content <~0.5 Hz, which human smooth-pursuit can track with near-unity
# gain. Bump rate higher to stress the system.
proc mp_tracking_bake_simplex {} {
    set dur   $::mp_tracking::sp_duration
    set dt    $::mp_tracking::sp_dt
    set rate  $::mp_tracking::sp_rate
    set amp   $::mp_tracking::sp_amp
    set sx    $::mp_tracking::sp_seed_x
    set sy    $::mp_tracking::sp_seed_y
    set nFrames [expr {int(round($dur / $dt)) + 1}]

    dl_local ts [dl_mult [dl_fromto 0 $nFrames] $dt]
    dl_local us [dl_mult $ts $rate]
    dl_local zero [dl_flist 0.0]

    # Decorrelated x/y paths by sampling the same noise axis with
    # different seeds. Result is in [-1, 1] approximately; scale to amp.
    dl_local px [dl_mult [dl_simplexNoise2D $sx $us $zero] $amp]
    dl_local py [dl_mult [dl_simplexNoise2D $sy $us $zero] $amp]

    set ::mp_tracking::sp_path_x [dl_tcllist $px]
    set ::mp_tracking::sp_path_y [dl_tcllist $py]
    set ::mp_tracking::sp_nframes $nFrames
}

# Per-frame preScript: update the shared mask offset along the currently
# selected path.
# Uses ::StimTime (ms) so freezing motion is independent of time-driving.
# The offset is in patch-local centered coords; patch spans [-0.5, 0.5]
# before the metagroup scale, so |offset| < 0.5 keeps the shape on-screen.
#
# Three path modes:
#   lissajous        -- sum of two orthogonal sinusoids (predictable, periodic)
#   random           -- Ornstein-Uhlenbeck on velocity: damped random walk with
#                       mean-reverting pull toward origin. Smooth, bounded,
#                       unpredictable across trials when reseeded.
#   simplex_prebaked -- pre-baked simplex-noise path via dl_simplexNoise2D;
#                       deterministic, reproducible, and suitable for smooth
#                       pursuit at low rates.
proc mp_tracking_update_offset {} {
    set t [expr {$::StimTime / 1000.0}]
    set dt [expr {$t - $::mp_tracking::last_t}]
    # Guard against absurd first-frame dt or paused-then-resumed spikes.
    if {$dt < 0.0 || $dt > 0.1} { set dt 0.016 }
    if {!$::mp_tracking::playing} { set dt 0.0 }
    set ::mp_tracking::last_t $t
    set ::mp_tracking::path_t [expr {$::mp_tracking::path_t + $dt}]

    if {$::mp_tracking::path_mode eq "random"} {
        # OU process on velocity:
        #   dv = (-damping * v + sigma * N(0,1) / sqrt(dt)) * dt
        # then dx = v * dt, then soft-clip to amp to keep on-patch.
        set k     $::mp_tracking::rand_damping
        set sigma $::mp_tracking::rand_sigma
        set amp   $::mp_tracking::rand_amp
        if {$dt > 0.0} {
            set sqdt [expr {sqrt($dt)}]
            set vx $::mp_tracking::rand_vx
            set vy $::mp_tracking::rand_vy
            set vx [expr {$vx + (-$k * $vx) * $dt + $sigma * [mp_tracking_randn] * $sqdt}]
            set vy [expr {$vy + (-$k * $vy) * $dt + $sigma * [mp_tracking_randn] * $sqdt}]
            set x [expr {$::mp_tracking::rand_x + $vx * $dt}]
            set y [expr {$::mp_tracking::rand_y + $vy * $dt}]
            # Reflect at amp boundary so target stays on-patch.
            if {$x >  $amp} { set x [expr { 2.0*$amp - $x}]; set vx [expr {-$vx}] }
            if {$x < -$amp} { set x [expr {-2.0*$amp - $x}]; set vx [expr {-$vx}] }
            if {$y >  $amp} { set y [expr { 2.0*$amp - $y}]; set vy [expr {-$vy}] }
            if {$y < -$amp} { set y [expr {-2.0*$amp - $y}]; set vy [expr {-$vy}] }
            set ::mp_tracking::rand_vx $vx
            set ::mp_tracking::rand_vy $vy
            set ::mp_tracking::rand_x  $x
            set ::mp_tracking::rand_y  $y
        }
        set ox $::mp_tracking::rand_x
        set oy $::mp_tracking::rand_y
    } elseif {$::mp_tracking::path_mode eq "simplex_prebaked"} {
        set n $::mp_tracking::sp_nframes
        if {$n > 0} {
            set idx [expr {int($::mp_tracking::path_t / $::mp_tracking::sp_dt) % $n}]
            set ox [lindex $::mp_tracking::sp_path_x $idx]
            set oy [lindex $::mp_tracking::sp_path_y $idx]
        } else {
            set ox 0
            set oy 0
        }
    } else {
        set pt $::mp_tracking::path_t
        set ax $::mp_tracking::amp_x
        set ay $::mp_tracking::amp_y
        set fx $::mp_tracking::freq_x
        set fy $::mp_tracking::freq_y
        set ox [expr {$ax * sin(2.0 * 3.14159265 * $fx * $pt)}]
        set oy [expr {$ay * sin(2.0 * 3.14159265 * $fy * $pt + 1.5708)}]
    }

    motionpatch_maskoffset dots_bg     $ox $oy
    motionpatch_maskoffset dots_target $ox $oy
}

proc mp_tracking_setup {nDots shapeSize} {
    glistInit 1
    resetObjList
    shaderImageReset

    namespace eval ::mp_tracking {
        variable playing      1
        variable path_t       0.0
        variable last_t       0.0
        variable path_mode    lissajous
        variable amp_x        0.35
        variable amp_y        0.25
        variable freq_x       0.17
        variable freq_y       0.23
        variable rand_x       0.0
        variable rand_y       0.0
        variable rand_vx      0.0
        variable rand_vy      0.0
        variable rand_damping 1.5
        variable rand_sigma   0.6
        variable rand_amp     0.35
        # Prebaked simplex path
        variable sp_duration  60.0
        variable sp_dt        0.01667
        variable sp_rate      0.25
        variable sp_amp       0.3
        variable sp_seed_x    42
        variable sp_seed_y    137
        variable sp_path_x    {}
        variable sp_path_y    {}
        variable sp_nframes   0
    }
    mp_tracking_bake_simplex

    set texSize 256
    set tex   [mp_tracking_make_circle_tex $texSize]
    set texID [shaderImageID $tex]

    set color 0.8
    set ptSize 3.0
    set speed 0.003

    set mg [metagroup]
    objName $mg patch

    # Background dots (OUTSIDE mask shape): moving leftward
    set mp_bg [motionpatch $nDots 0.01 30]
    objName $mp_bg dots_bg
    motionpatch_pointsize $mp_bg $ptSize
    motionpatch_color $mp_bg $color $color $color 1.0
    motionpatch_masktype $mp_bg 0
    motionpatch_coherence $mp_bg 1.0
    motionpatch_direction $mp_bg 3.14159265
    motionpatch_speed $mp_bg $speed
    motionpatch_setSampler $mp_bg $texID 0
    motionpatch_samplermaskmode $mp_bg 2
    motionpatch_maskscale $mp_bg $shapeSize
    metagroupAdd $mg $mp_bg

    # Target dots (INSIDE mask shape): moving rightward
    set mp_tg [motionpatch $nDots 0.01 30]
    objName $mp_tg dots_target
    motionpatch_pointsize $mp_tg $ptSize
    motionpatch_color $mp_tg $color $color $color 1.0
    motionpatch_masktype $mp_tg 0
    motionpatch_coherence $mp_tg 1.0
    motionpatch_direction $mp_tg 0.0
    motionpatch_speed $mp_tg $speed
    motionpatch_setSampler $mp_tg $texID 0
    motionpatch_samplermaskmode $mp_tg 1
    motionpatch_maskscale $mp_tg $shapeSize
    metagroupAdd $mg $mp_tg

    # Drive the shared mask offset every frame from a preScript.
    addPreScript $mp_bg mp_tracking_update_offset

    scaleObj $mg 10.0 10.0

    glistAddObject $mg 0
    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ---- Adjuster helper procs ----

proc mp_tracking_set_motion {bg_coh tg_coh speed} {
    motionpatch_coherence dots_bg     $bg_coh
    motionpatch_coherence dots_target $tg_coh
    motionpatch_speed     dots_bg     $speed
    motionpatch_speed     dots_target $speed
}

proc mp_tracking_get_motion {} {
    dict create bg_coh 1.0 tg_coh 1.0 speed 0.003
}

proc mp_tracking_set_directions {bg_dir tg_dir} {
    motionpatch_direction dots_bg     [expr {$bg_dir * 3.14159265 / 180.0}]
    motionpatch_direction dots_target [expr {$tg_dir * 3.14159265 / 180.0}]
}

proc mp_tracking_get_directions {} {
    dict create bg_dir 180 tg_dir 0
}

proc mp_tracking_set_shape_size {shape_size} {
    motionpatch_maskscale dots_bg     $shape_size
    motionpatch_maskscale dots_target $shape_size
}

proc mp_tracking_get_shape_size {} {
    dict create shape_size 0.25
}

# Set a grayscale luminance (R=G=B) for each patch independently.
# Set a value to 0 to hide a patch, making segmentation trivial.
proc mp_tracking_set_luminance {bg_lum tg_lum} {
    motionpatch_color dots_bg     $bg_lum $bg_lum $bg_lum 1.0
    motionpatch_color dots_target $tg_lum $tg_lum $tg_lum 1.0
}

proc mp_tracking_get_luminance {} {
    dict create bg_lum 0.8 tg_lum 0.8
}

proc mp_tracking_set_path {amp_x amp_y freq_x freq_y} {
    set ::mp_tracking::amp_x  $amp_x
    set ::mp_tracking::amp_y  $amp_y
    set ::mp_tracking::freq_x $freq_x
    set ::mp_tracking::freq_y $freq_y
}

proc mp_tracking_get_path {} {
    dict create amp_x 0.35 amp_y 0.25 freq_x 0.17 freq_y 0.23
}

# Switch path mode. Reset state so each trial starts from a consistent
# initial condition.
proc mp_tracking_set_path_mode {mode} {
    set ::mp_tracking::path_mode $mode
    set ::mp_tracking::path_t 0.0
    if {$mode eq "random"} {
        set ::mp_tracking::rand_x  0.0
        set ::mp_tracking::rand_y  0.0
        set ::mp_tracking::rand_vx 0.0
        set ::mp_tracking::rand_vy 0.0
    }
}

proc mp_tracking_get_path_mode {} {
    dict create mode lissajous
}

# Set simplex path parameters and re-bake. Smooth-pursuit-friendly
# defaults: rate 0.25 gives low-bandwidth motion trackable with near-
# unity pursuit gain; amp 0.3 keeps the target on-patch at any offset.
proc mp_tracking_set_simplex {rate amp seed_x seed_y} {
    set ::mp_tracking::sp_rate   $rate
    set ::mp_tracking::sp_amp    $amp
    set ::mp_tracking::sp_seed_x $seed_x
    set ::mp_tracking::sp_seed_y $seed_y
    mp_tracking_bake_simplex
    set ::mp_tracking::path_t 0.0
}

proc mp_tracking_get_simplex {} {
    dict create rate 0.25 amp 0.3 seed_x 42 seed_y 137
}

proc mp_tracking_set_random_path {damping sigma amp} {
    set ::mp_tracking::rand_damping $damping
    set ::mp_tracking::rand_sigma   $sigma
    set ::mp_tracking::rand_amp     $amp
}

proc mp_tracking_get_random_path {} {
    dict create damping 1.5 sigma 0.6 amp 0.35
}

# Freeze toggles BOTH the dot motion (speed=0) and the mask translation,
# so a frozen trial is statistically indistinguishable from a uniform
# random dot field.
proc mp_tracking_set_freeze {frozen speed} {
    if {$frozen} {
        motionpatch_speed dots_bg     0.0
        motionpatch_speed dots_target 0.0
        set ::mp_tracking::playing 0
    } else {
        motionpatch_speed dots_bg     $speed
        motionpatch_speed dots_target $speed
        set ::mp_tracking::playing 1
    }
}

proc mp_tracking_get_freeze {} {
    dict create frozen 0 speed 0.003
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

workspace::setup mp_tracking_setup {
    nDots     {int 200 4000 100 1500 "Number of Dots (per patch)"}
    shapeSize {float 0.05 0.5 0.01 0.2 "Shape Size (fraction of patch)"}
} -adjusters {track_freeze track_motion track_directions track_shape track_luminance track_mode track_path track_random_path track_simplex track_transform} \
  -label "Motion Tracking"

workspace::adjuster track_freeze {
    frozen {choice {0 1} 0 "Frozen (1=invisible)"}
    speed  {float 0.0 0.01 0.0005 0.003 "Speed when playing"}
} -target {} -proc mp_tracking_set_freeze -getter mp_tracking_get_freeze \
  -label "Freeze / Play"

workspace::adjuster track_motion {
    bg_coh {float 0.0 1.0 0.05 1.0 "Background Coherence"}
    tg_coh {float 0.0 1.0 0.05 1.0 "Target Coherence"}
    speed  {float 0.0 0.01 0.0005 0.003 "Dot Speed"}
} -target {} -proc mp_tracking_set_motion -getter mp_tracking_get_motion \
  -label "Dot Motion"

workspace::adjuster track_directions {
    bg_dir {float 0 360 15 180 "Background Direction (deg)"}
    tg_dir {float 0 360 15 0   "Target Direction (deg)"}
} -target {} -proc mp_tracking_set_directions -getter mp_tracking_get_directions \
  -label "Inside / Outside Directions"

workspace::adjuster track_shape {
    shape_size {float 0.05 0.5 0.01 0.2 "Shape Size"}
} -target {} -proc mp_tracking_set_shape_size -getter mp_tracking_get_shape_size \
  -label "Shape Size"

workspace::adjuster track_luminance {
    bg_lum {float 0.0 1.0 0.05 0.8 "Background Luminance"}
    tg_lum {float 0.0 1.0 0.05 0.8 "Target Luminance"}
} -target {} -proc mp_tracking_set_luminance -getter mp_tracking_get_luminance \
  -label "Luminance"

workspace::adjuster track_mode {
    mode {choice {lissajous random simplex_prebaked} lissajous "Path Mode"}
} -target {} -proc mp_tracking_set_path_mode -getter mp_tracking_get_path_mode \
  -label "Path Mode"

workspace::adjuster track_path {
    amp_x  {float 0.0 0.5 0.05 0.35 "Amplitude X"}
    amp_y  {float 0.0 0.5 0.05 0.25 "Amplitude Y"}
    freq_x {float 0.0 1.0 0.01 0.17 "Frequency X (Hz)"}
    freq_y {float 0.0 1.0 0.01 0.23 "Frequency Y (Hz)"}
} -target {} -proc mp_tracking_set_path -getter mp_tracking_get_path \
  -label "Lissajous Path"

# damping : how fast velocity decays (higher = tighter, less inertia)
# sigma   : noise drive amplitude (higher = wilder motion)
# amp     : reflective boundary radius (keeps target on-patch)
workspace::adjuster track_random_path {
    damping {float 0.1 5.0 0.1 1.5  "Damping"}
    sigma   {float 0.0 2.0 0.05 0.6 "Noise Sigma"}
    amp     {float 0.05 0.5 0.05 0.35 "Extent"}
} -target {} -proc mp_tracking_set_random_path -getter mp_tracking_get_random_path \
  -label "Random Path (OU)"

# Prebaked simplex path. rate = traversal speed through the noise field;
# low values (~0.15-0.35) give smooth-pursuable motion, higher values
# (~>0.6) break pursuit and elicit catch-up saccades. Seeds allow exact
# reproducibility across trials / subjects. Changes rebake the path.
workspace::adjuster track_simplex {
    rate   {float 0.05 1.5 0.05 0.25 "Rate (traversal speed)"}
    amp    {float 0.05 0.5 0.05 0.3  "Amplitude"}
    seed_x {int   0    9999 1 42     "Seed X"}
    seed_y {int   0    9999 1 137    "Seed Y"}
} -target {} -proc mp_tracking_set_simplex -getter mp_tracking_get_simplex \
  -label "Simplex Prebaked Path"

workspace::adjuster track_transform -template scale -target patch \
  -label "Scene Size"
