# examples/motionpatch/mp_postcue.tcl
# Post-cue / "postception" color-binding demo.
#
# Idea (Sperling/Sergent retro-perception x Treisman binding):
#   A brief, weak, SPATIALLY-SOFT color flash is painted across the dots
#   in the periphery. On its own it is near-/sub-threshold -- there is no
#   object for the color to belong to, so it is hard to localize/identify.
#   A separate COHERENCE PULSE then segments a motion-defined shape at the
#   same location. Question: does the coherence pulse act as a retro-cue /
#   object token that binds the fragile color to the now-visible shape,
#   "reviving" the color into report?
#
#   Primary manipulation = SOA between the color flash and the coherence
#   pulse (negative SOA = color BEFORE coherence = the surprising
#   postdictive case). Secondary = spatial overlap of blob vs. shape
#   ("is the binding loose?"), set by overlap_dx/dy and blob_sigma.
#
# This is a LOOK-AND-TURN-KNOBS demo, not a data-collection paradigm.
# The real dserv paradigm will borrow this stim code.
#
# Architecture (two-patch idiom + one extra sampler):
#   dots_bg     : full-field noise, dots OUTSIDE the shape aperture
#                 (samplermaskmode 2), held incoherent.
#   dots_target : dots INSIDE the shape aperture (samplermaskmode 1),
#                 coherence pulsed by an mp_sim timeline.
#   tex0 (both) : the shape aperture (soft circle), placed at eccentricity
#                 via maskoffset. Shape is defined ONLY by motion.
#   tex1 (both) : a Gaussian color blob in the R channel; applied as a
#                 TINT layer (layermode R = 2) only during the flash
#                 window. Color a/b comes from layercolor (texture is
#                 color-agnostic), so a/b switches for free per trial.
#
# Controls:
#   Down Arrow - trigger a trial now
#   Up Arrow   - reset / idle
#   (auto-loops by default so you can just watch)

load_Impro
package require mp_sim

# ------------------------------------------------------------------
# Texture builders
# ------------------------------------------------------------------

# Soft shape aperture (circle) -> tex0. Reused from the pulsed demos.
proc mp_pc_make_circle_tex {size} {
    set half [expr {$size / 2.0}]
    set npoints 64
    set step [expr {2.0 * 3.14159265 / $npoints}]
    dl_local angles [dl_fromto 0 [expr {2.0 * 3.14159265}] $step]
    dl_local x [dl_add [dl_mult [dl_cos $angles] $half] $half]
    dl_local y [dl_add [dl_mult [dl_sin $angles] $half] $half]
    set img  [img_create -width $size -height $size -depth 4]
    set poly [img_drawPolygonFast $img $x $y 255 255 255 255]
    dl_local pix [img_imgtolist $poly]
    img_delete $img $poly
    return [shaderImageCreate $pix $size $size linear]
}

# Gaussian color blob -> R channel of an RGBA texture (tex1).
#   cx,cy,sigma are in texture fraction [0,1]. The R channel holds the
#   per-dot spatial weight; the tint COLOR is supplied separately by
#   motionpatch_layercolor, so this texture is color-agnostic and does
#   not need rebuilding when a/b changes -- only when geometry changes.
# amp scales the peak weight (0..1) for coarse strength control; keep 1.0
# and titrate the chromatic distance with color_delta instead.
proc mp_pc_make_blob_tex {size cx cy sigma amp} {
    set n [expr {$size * $size}]
    dl_local idx [dl_fromto 0 $n]
    dl_local py  [dl_int [dl_div $idx $size]]
    dl_local px  [dl_sub $idx [dl_mult $py $size]]
    dl_local fx  [dl_div [dl_add [dl_float $px] 0.5] [expr {double($size)}]]
    dl_local fy  [dl_div [dl_add [dl_float $py] 0.5] [expr {double($size)}]]
    dl_local dx  [dl_sub $fx $cx]
    dl_local dy  [dl_sub $fy $cy]
    dl_local r2  [dl_add [dl_mult $dx $dx] [dl_mult $dy $dy]]
    dl_local g   [dl_exp [dl_mult [dl_div $r2 [expr {$sigma*$sigma}]] -0.5]]
    dl_local R   [dl_int [dl_mult $g [expr {255.0 * $amp}]]]
    dl_local Z   [dl_int [dl_zeros $n]]
    dl_local A   [dl_int [dl_add [dl_zeros $n] 255]]
    dl_local cols [dl_llist $R $Z $Z $A]
    # MUST be DF_CHAR: shaderImageCreate uploads a DF_LONG list as GL_INT
    # (normalized -> ~0), which silently produces a black texture. img_imgtolist
    # returns char for the same reason. dl_char keeps the raw 0..255 bytes.
    dl_local flat [dl_char [dl_unpack [dl_transpose $cols]]]
    return [shaderImageCreate $flat $size $size linear]
}

# (Re)build tex0 (shape aperture) and the blob texture(s), and bind tex0
# on both patches. Called at setup and whenever a geometry/location knob
# changed (tex_dirty). Builds a bounded SET of blob textures -- one per
# candidate location -- so trial-to-trial location randomization is just
# a rebind (motionpatch_setSampler), never a per-trial texture create.
#
# tex0 (the circle) is positioned per-trial via maskoffset, so it never
# needs rebuilding. Each blob texture bakes in its location + overlap
# offset. The shader samples the blob at (s, 1-t), so the vertical
# texture coordinate is flipped relative to patch-local y.
#
# shaderImageReset first keeps the image list bounded (K+1) no matter how
# often geometry knobs change, instead of leaking textures.
proc mp_pc_build_textures {} {
    set ts $::mp_pc::tex_size
    shaderImageReset

    set tex0 [mp_pc_make_circle_tex $ts]
    set ::mp_pc::tex0_id [shaderImageID $tex0]

    # Candidate locations (patch-local, centered). rand_loc -> ring of
    # n_locations at ecc_radius (random ring phase each rebuild); else a
    # single fixed location at ecc_x/ecc_y.
    set exs {}; set eys {}
    if {$::mp_pc::rand_loc && $::mp_pc::n_locations > 1} {
        set K $::mp_pc::n_locations
        set r $::mp_pc::ecc_radius
        set phase [expr {rand() * 2.0 * 3.14159265358979}]
        for {set k 0} {$k < $K} {incr k} {
            set a [expr {$phase + $k * 2.0 * 3.14159265358979 / $K}]
            lappend exs [expr {$r * cos($a)}]
            lappend eys [expr {$r * sin($a)}]
        }
    } else {
        lappend exs $::mp_pc::ecc_x
        lappend eys $::mp_pc::ecc_y
    }

    # One blob texture per location (center = location + overlap offset).
    set texs {}
    foreach ex $exs ey $eys {
        set bx [expr {$ex + $::mp_pc::overlap_dx}]
        set by [expr {$ey + $::mp_pc::overlap_dy}]
        set cx [expr {0.5 + $bx}]
        set cy [expr {0.5 - $by}]        ;# tex1 sampled at (s, 1-t)
        set t [mp_pc_make_blob_tex $ts $cx $cy $::mp_pc::blob_sigma 1.0]
        lappend texs [shaderImageID $t]
    }
    set ::mp_pc::loc_ex  $exs
    set ::mp_pc::loc_ey  $eys
    set ::mp_pc::loc_tex $texs

    foreach mp {dots_target dots_bg} {
        motionpatch_setSampler $mp $::mp_pc::tex0_id 0
        motionpatch_maskscale  $mp $::mp_pc::shape_size
    }
    set ::mp_pc::tex_dirty 0
}

# ------------------------------------------------------------------
# Timeline (single coherence pulse) via mp_sim
# ------------------------------------------------------------------
proc mp_pc_speed_pu {v} {
    set ps $::mp_pc::patch_size
    if {$ps <= 0} { return 0.0 }
    return [expr {double($v) / double($ps)}]
}

proc mp_pc_build_timeline {} {
    set tgt_pu [mp_pc_speed_pu $::mp_pc::target_speed]
    set sur_pu [mp_pc_speed_pu $::mp_pc::surround_speed]

    set spec [dict create \
        meta [dict create \
            duration       $::mp_pc::duration \
            dt             0.0167 \
            patch_size_dva $::mp_pc::patch_size] \
        endpoints [dict create \
            target   [dict create coh 1.0 speed $tgt_pu \
                          dir $::mp_pc::target_dir_rad life $::mp_pc::target_lifetime] \
            surround [dict create coh 0.0 speed $sur_pu \
                          dir 0.0 life $::mp_pc::bg_lifetime]] \
        envelope [dict create \
            kind     sum_gaussians \
            n_pulses 1 \
            sigma_ms $::mp_pc::coh_sigma_ms \
            base_coh 1.0] \
        trajectory {kind static}]

    if {[dg_exists mp_pc_tl]} { dg_delete mp_pc_tl }
    set ::mp_pc::timeline [mp_sim::compile_spec $spec -gname mp_pc_tl]

    # Coherence-pulse peak time (s): argmax of the coherence column. The
    # color flash is scheduled relative to THIS. (dl_last of the ascending
    # sort-index list = index of the maximum.)
    set peak_i [dl_last [dl_sortIndices mp_pc_tl:coherence]]
    set ::mp_pc::coh_dt      [dl_get mp_pc_tl:dt 0]
    set ::mp_pc::coh_nframes [dl_length mp_pc_tl:t]
    set ::mp_pc::coh_peak_s  [expr {$peak_i * $::mp_pc::coh_dt}]
}

# ------------------------------------------------------------------
# Trial lifecycle
# ------------------------------------------------------------------
proc mp_pc_pick_color {} {
    # Catch trial (no color) with probability catch_frac, mixed randomly
    # with real a/b trials so false alarms can be measured against them.
    if {rand() < $::mp_pc::catch_frac} { return none }
    switch -- $::mp_pc::probe_color {
        a { return a }
        b { return b }
        default { return [expr {rand() < 0.5 ? "a" : "b"}] }
    }
}

# sRGB <-> linear-light helpers (standard sRGB EOTF). Used to equate the
# a/b probe luminances in LINEAR light, where the Rec709 luminance
# weights actually apply.
proc mp_pc_s2l {c} {
    expr {$c <= 0.04045 ? $c/12.92 : pow(($c+0.055)/1.055, 2.4)}
}
proc mp_pc_l2s {c} {
    if {$c < 0.0} { set c 0.0 } elseif {$c > 1.0} { set c 1.0 }
    expr {$c <= 0.0031308 ? 12.92*$c : 1.055*pow($c, 1.0/2.4) - 0.055}
}

# Probe RGB for a/b, built to be ISOLUMINANT with the gray dot (and with
# each other) so detection/ID is chromatic, not luminance-driven.
#
# Work in linear light: hold luminance L = 0.2126 R + 0.7152 G + 0.0722 B
# constant by balancing R against G ( dG = -(0.2126/0.7152) dR ~ -0.30 dR),
# B fixed -- i.e. modulate along the red/green cardinal axis. a and b are
# mirror images through the gray point, so they are equal-luminance and
# equal-chroma by construction.
#
# lum_balance is an observer-nulled correction (minimum-motion / flicker):
# a small common-mode red-up/green-down luminance tilt to absorb the
# residual asymmetry of an UNCALIBRATED monitor (where the sRGB-gamma /
# standard-primary assumptions above don't exactly hold).
proc mp_pc_probe_rgb {which} {
    set y0 [mp_pc_s2l $::mp_pc::base_lum]
    set c  $::mp_pc::color_delta
    set k  [expr {0.2126/0.7152}]
    set off [expr {$::mp_pc::lum_balance * $y0}]
    if {$which eq "a"} {                 ;# red
        set rl [expr {$y0 + $c      + $off}]
        set gl [expr {$y0 - $k*$c   + $off}]
        set bl [expr {$y0           + $off}]
    } else {                             ;# green
        set rl [expr {$y0 - $c      - $off}]
        set gl [expr {$y0 + $k*$c   - $off}]
        set bl [expr {$y0           - $off}]
    }
    return [list [mp_pc_l2s $rl] [mp_pc_l2s $gl] [mp_pc_l2s $bl]]
}

proc mp_pc_tint_off {} {
    foreach mp {dots_target dots_bg} { motionpatch_layermode $mp R 0 }
    set ::mp_pc::tint_on 0
}

proc mp_pc_start_trial {} {
    if {$::mp_pc::tex_dirty} { mp_pc_build_textures }
    mp_pc_build_timeline

    # Pick this trial's location from the pre-built set and point both
    # patches at it (shape via maskoffset, blob via the matching tex1).
    set nloc [llength $::mp_pc::loc_tex]
    if {$::mp_pc::rand_loc && $nloc > 1} {
        set k [expr {int(rand() * $nloc)}]
    } else {
        set k 0
    }
    set ::mp_pc::ecc_x [lindex $::mp_pc::loc_ex $k]
    set ::mp_pc::ecc_y [lindex $::mp_pc::loc_ey $k]
    foreach mp {dots_target dots_bg} {
        motionpatch_maskoffset $mp $::mp_pc::ecc_x $::mp_pc::ecc_y
        motionpatch_setSampler $mp [lindex $::mp_pc::loc_tex $k] 1
    }

    set ::mp_pc::trial_color [mp_pc_pick_color]
    if {$::mp_pc::trial_color ne "none"} {
        lassign [mp_pc_probe_rgb $::mp_pc::trial_color] pr pg pb
        foreach mp {dots_target dots_bg} { motionpatch_layercolor $mp R $pr $pg $pb 1.0 }
    }
    mp_pc_tint_off

    # Flash schedule (s). SOA < 0 => color BEFORE the coherence peak.
    set ::mp_pc::color_on_s  [expr {$::mp_pc::coh_peak_s + $::mp_pc::soa_ms/1000.0}]
    set ::mp_pc::color_off_s [expr {$::mp_pc::color_on_s + $::mp_pc::color_dur_ms/1000.0}]

    set ::mp_pc::t0    $::mp_pc::clk
    set ::mp_pc::phase playing

    puts [format "mp_postcue: trial  color=%s  soa=%+.0f ms  loc=(%+.2f,%+.2f)  coh=%s color=%s" \
              $::mp_pc::trial_color $::mp_pc::soa_ms \
              $::mp_pc::ecc_x $::mp_pc::ecc_y \
              [expr {$::mp_pc::show_coherence ? "on" : "OFF"}] \
              [expr {$::mp_pc::show_color ? "on" : "OFF"}]]
}

# Collapse the inside dots to surround statistics so the shape vanishes.
proc mp_pc_collapse {} {
    motionpatch_coherence dots_target 0.0
    motionpatch_speed     dots_target [mp_pc_speed_pu $::mp_pc::surround_speed]
    motionpatch_lifetime  dots_target $::mp_pc::bg_lifetime
}

# ------------------------------------------------------------------
# Per-frame driver
# ------------------------------------------------------------------
proc mp_pc_update {} {
    set t  [expr {$::StimTimeF / 1000.0}]
    set dt [expr {$t - $::mp_pc::last_t}]
    if {$dt < 0.0 || $dt > 0.1} { set dt 0.016 }
    set ::mp_pc::last_t $t
    set ::mp_pc::clk [expr {$::mp_pc::clk + $dt}]

    # --- flicker calibration mode: drive the bar field, skip trials ---
    # Counterphase red/green square-wave reversing at flicker_hz. Adjacent
    # bars are opposite; all swap each half-period. Observer nulls
    # lum_balance until the flicker/shimmer is minimized (isoluminance).
    if {$::mp_pc::flicker_mode} {
        set hp [expr {0.5 / $::mp_pc::flicker_hz}]
        set ph [expr {int($::mp_pc::clk / $hp) % 2}]
        lassign [mp_pc_probe_rgb a] ar ag ab
        lassign [mp_pc_probe_rgb b] br bg bb
        set idx 0
        foreach bar $::mp_pc::flick_bars {
            if {(($idx + $ph) % 2) == 0} {
                polycolor $bar $ar $ag $ab
            } else {
                polycolor $bar $br $bg $bb
            }
            incr idx
        }
        return
    }

    switch -- $::mp_pc::phase {
        idle { return }
        iti {
            if {$::mp_pc::loop && \
                ($::mp_pc::clk - $::mp_pc::iti_start) >= $::mp_pc::iti_s} {
                mp_pc_start_trial
            }
            return
        }
    }

    # phase == playing
    set tplay [expr {$::mp_pc::clk - $::mp_pc::t0}]

    # --- coherence pulse (timeline lookup) ---
    set i [expr {int($tplay / $::mp_pc::coh_dt)}]
    if {$i >= $::mp_pc::coh_nframes} {
        mp_pc_collapse
        mp_pc_tint_off
        set ::mp_pc::phase     iti
        set ::mp_pc::iti_start $::mp_pc::clk
        return
    }
    if {$::mp_pc::show_coherence} {
        motionpatch_coherence dots_target [dl_get mp_pc_tl:coherence  $i]
        motionpatch_speed     dots_target [dl_get mp_pc_tl:speed      $i]
        motionpatch_lifetime  dots_target [dl_get mp_pc_tl:lifetime_s $i]
        motionpatch_direction dots_target [dl_get mp_pc_tl:direction  $i]
    } else {
        mp_pc_collapse   ;# color-only baseline: no motion-defined shape
    }

    # --- color flash (temporal window; spatial profile from tex1) ---
    # Suppressed on catch trials (trial_color == none) and by show_color.
    set want [expr {$::mp_pc::show_color && \
                    $::mp_pc::trial_color ne "none" && \
                    $tplay >= $::mp_pc::color_on_s && \
                    $tplay <  $::mp_pc::color_off_s}]
    if {$want && !$::mp_pc::tint_on} {
        foreach mp {dots_target dots_bg} { motionpatch_layermode $mp R 2 }
        set ::mp_pc::tint_on 1
    } elseif {!$want && $::mp_pc::tint_on} {
        mp_pc_tint_off
    }
}

# ------------------------------------------------------------------
# State init
#
# Tunable knobs are initialized ONCE and then PERSIST across setup re-runs,
# so a red/green null found in the Flicker Calibration variant carries into
# the Post-cue Trials variant (and vice versa). Runtime / scene state is
# reset on every (re)build.
# ------------------------------------------------------------------
proc mp_pc_init_params {} {
    namespace eval ::mp_pc {}
    foreach {k v} {
        loop 1  iti_s 0.8
        patch_size 16.0  tex_size 128
        shape_size 0.16  ecc_x 0.35  ecc_y 0.0
        rand_loc 0  n_locations 8  ecc_radius 0.32
        duration 1.6  coh_sigma_ms 70.0  show_coherence 1
        target_speed 5.0  target_dir_deg 0.0  target_dir_rad 0.0  target_lifetime 0.5
        surround_speed 1.5  bg_lifetime 0.08  surround_alpha 1.0
        show_color 1  probe_color random  catch_frac 0.2
        color_delta 0.16  color_dur_ms 33.0  soa_ms -100.0
        base_lum 0.5  lum_balance 0.0
        blob_sigma 0.09  overlap_dx 0.0  overlap_dy 0.0
        pointsize 3.0  fix_r 0.15  flicker_hz 12.0
    } {
        if {![info exists ::mp_pc::$k]} { set ::mp_pc::$k $v }
    }
}

proc mp_pc_init_runtime {} {
    foreach {k v} {
        phase iti  clk 0.0  last_t 0.0  t0 0.0  iti_start -1.0e9
        tex_dirty 0  tex0_id -1  trial_color a  tint_on 0
        coh_dt 0.0167  coh_nframes 0  coh_peak_s 0.8
        color_on_s 0.0  color_off_s 0.0  flicker_mode 0
    } { set ::mp_pc::$k $v }
    foreach k {loc_ex loc_ey loc_tex timeline patch_mg flick_mg flick_bars fix_id} {
        set ::mp_pc::$k {}
    }
}

# ------------------------------------------------------------------
# Scene builder (shared by both top-level variants)
# ------------------------------------------------------------------
proc mp_pc_build_scene {patch_size_dva dot_density} {
    glistInit 1
    resetObjList
    shaderImageReset
    set ::mp_pc::patch_size $patch_size_dva

    set nDots [expr {int($dot_density * $patch_size_dva * $patch_size_dva)}]
    if {$nDots < 100} { set nDots 100 }

    set g   $::mp_pc::base_lum
    set sur [mp_pc_speed_pu $::mp_pc::surround_speed]

    set mg [metagroup]
    objName $mg patch
    set ::mp_pc::patch_mg $mg

    # Background: full-field flicker, dots OUTSIDE the aperture.
    set mp_bg [motionpatch $nDots $sur 0.5]
    objName $mp_bg dots_bg
    motionpatch_pointsize $mp_bg $::mp_pc::pointsize
    motionpatch_color     $mp_bg $g $g $g $::mp_pc::surround_alpha
    motionpatch_masktype  $mp_bg 0
    motionpatch_coherence $mp_bg 0.0
    motionpatch_lifetime  $mp_bg $::mp_pc::bg_lifetime
    motionpatch_speed     $mp_bg $sur
    motionpatch_samplermaskmode $mp_bg 2
    metagroupAdd $mg $mp_bg

    # Target: dots INSIDE the aperture; coherence pulsed. Held collapsed
    # (invisible) until a trial starts.
    set mp_tg [motionpatch $nDots $sur 0.5]
    objName $mp_tg dots_target
    motionpatch_pointsize $mp_tg $::mp_pc::pointsize
    motionpatch_color     $mp_tg $g $g $g 1.0
    motionpatch_masktype  $mp_tg 0
    motionpatch_coherence $mp_tg 0.0
    motionpatch_lifetime  $mp_tg $::mp_pc::bg_lifetime
    motionpatch_speed     $mp_tg $sur
    motionpatch_samplermaskmode $mp_tg 1
    metagroupAdd $mg $mp_tg

    # Build + bind both samplers (tex0 shape, tex1 blob).
    mp_pc_build_textures

    addPreScript $mp_bg mp_pc_update

    scaleObj $mg $::mp_pc::patch_size $::mp_pc::patch_size
    glistAddObject $mg 0

    # Central fixation spot (screen-space size independent of patch).
    set fr $::mp_pc::fix_r
    set fix_mg [metagroup]
    set fo [polygon]; polycirc $fo 1; polycolor $fo 0.7 0.7 0.1; scaleObj $fo [expr {2.0*$fr}]
    set fi [polygon]; polycirc $fi 1; polycolor $fi 0.0 0.0 0.0; scaleObj $fi [expr {1.2*$fr}]
    metagroupAdd $fix_mg $fo
    metagroupAdd $fix_mg $fi
    set ::mp_pc::fix_id $fix_mg
    glistAddObject $fix_mg 0

    # Flicker-calibration bar field (own top-level metagroup, hidden until
    # flicker mode). Vertical bars in dva, centered; recolored each frame by
    # mp_pc_update's flicker branch. NB: the driver prescript lives on a
    # metagroup MEMBER (dots_bg) yet must run while the patch is hidden --
    # which now works thanks to the visibility-independent pre-script pass.
    set nbars   12
    set fieldW  [expr {0.7 * $::mp_pc::patch_size}]
    set fieldH  [expr {0.45 * $::mp_pc::patch_size}]
    set barW    [expr {$fieldW / $nbars}]
    set fmg [metagroup]
    objName $fmg flicker
    set bars {}
    for {set i 0} {$i < $nbars} {incr i} {
        set b [polygon]
        polycolor $b 0.5 0.5 0.5
        scaleObj $b $barW $fieldH
        translateObj $b [expr {-$fieldW/2.0 + ($i+0.5)*$barW}] 0
        metagroupAdd $fmg $b
        lappend bars $b
    }
    set ::mp_pc::flick_bars $bars
    set ::mp_pc::flick_mg   $fmg
    glistAddObject $fmg 0
    catch {setVisible $fmg 0}

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
}

# ------------------------------------------------------------------
# Top-level variants (the setup dropdown)
# ------------------------------------------------------------------
proc mp_pc_setup {patch_size_dva dot_density} {
    mp_pc_init_params
    mp_pc_init_runtime
    mp_pc_build_scene $patch_size_dva $dot_density
    mp_pc_set_flicker 0 $::mp_pc::flicker_hz   ;# trial mode
    redraw
    # NB: the workspace applies each adjuster's initial state after this
    # returns (workspace::apply_adjuster_state), so setter side-effects run
    # at load -- no demo-side re-apply needed.
}

proc mp_pc_flicker_setup {} {
    mp_pc_init_params
    mp_pc_init_runtime
    mp_pc_build_scene $::mp_pc::patch_size 20.0
    mp_pc_set_flicker 1 $::mp_pc::flicker_hz   ;# calibration mode
    redraw
}

# ------------------------------------------------------------------
# Actions
# ------------------------------------------------------------------
proc mp_pc_trigger {action} {
    switch -- $action {
        drop  { mp_pc_start_trial }
        reset {
            set ::mp_pc::phase idle
            catch { mp_pc_collapse; mp_pc_tint_off }
        }
    }
    return
}
proc onDownArrow {} { mp_pc_trigger drop }
proc onUpArrow   {} { mp_pc_trigger reset }

# ------------------------------------------------------------------
# Adjusters
# ------------------------------------------------------------------
# All getters read LIVE namespace state, so the panel reflects the actual
# (and persisted) values -- e.g. a lum_balance nulled in the Flicker
# Calibration variant shows up when you switch to Post-cue Trials.
proc mp_pc_set_timing {soa_ms color_dur_ms coh_sigma_ms duration} {
    set ::mp_pc::soa_ms       $soa_ms
    set ::mp_pc::color_dur_ms $color_dur_ms
    set ::mp_pc::coh_sigma_ms $coh_sigma_ms
    set ::mp_pc::duration     $duration
}
proc mp_pc_get_timing {} {
    dict create soa_ms $::mp_pc::soa_ms color_dur_ms $::mp_pc::color_dur_ms \
        coh_sigma_ms $::mp_pc::coh_sigma_ms duration $::mp_pc::duration
}

proc mp_pc_set_color {probe_color catch_frac color_delta base_lum lum_balance} {
    set ::mp_pc::probe_color $probe_color
    set ::mp_pc::catch_frac  $catch_frac
    set ::mp_pc::color_delta $color_delta
    set ::mp_pc::base_lum    $base_lum
    set ::mp_pc::lum_balance $lum_balance
    catch {
        motionpatch_color dots_bg     $base_lum $base_lum $base_lum $::mp_pc::surround_alpha
        motionpatch_color dots_target $base_lum $base_lum $base_lum 1.0
    }
}
proc mp_pc_get_color {} {
    dict create probe_color $::mp_pc::probe_color catch_frac $::mp_pc::catch_frac \
        color_delta $::mp_pc::color_delta base_lum $::mp_pc::base_lum \
        lum_balance $::mp_pc::lum_balance
}

proc mp_pc_set_conditions {show_coherence show_color surround_alpha loop} {
    set ::mp_pc::show_coherence $show_coherence
    set ::mp_pc::show_color     $show_color
    set ::mp_pc::surround_alpha $surround_alpha
    set ::mp_pc::loop           $loop
    catch {
        motionpatch_color dots_bg $::mp_pc::base_lum $::mp_pc::base_lum \
                                   $::mp_pc::base_lum $surround_alpha
    }
}
proc mp_pc_get_conditions {} {
    dict create show_coherence $::mp_pc::show_coherence show_color $::mp_pc::show_color \
        surround_alpha $::mp_pc::surround_alpha loop $::mp_pc::loop
}

# Shape aperture + color blob + location uncertainty -- all spatial config
# for the stimulus. Rebuilds textures (tex_dirty) since geometry changed.
proc mp_pc_set_spatial {shape_size ecc_x ecc_y blob_sigma overlap_dx overlap_dy \
                        rand_loc n_locations ecc_radius} {
    set ::mp_pc::shape_size  $shape_size
    set ::mp_pc::ecc_x       $ecc_x
    set ::mp_pc::ecc_y       $ecc_y
    set ::mp_pc::blob_sigma  $blob_sigma
    set ::mp_pc::overlap_dx  $overlap_dx
    set ::mp_pc::overlap_dy  $overlap_dy
    set ::mp_pc::rand_loc    $rand_loc
    set ::mp_pc::n_locations $n_locations
    set ::mp_pc::ecc_radius  $ecc_radius
    set ::mp_pc::tex_dirty   1
}
proc mp_pc_get_spatial {} {
    dict create shape_size $::mp_pc::shape_size ecc_x $::mp_pc::ecc_x ecc_y $::mp_pc::ecc_y \
        blob_sigma $::mp_pc::blob_sigma overlap_dx $::mp_pc::overlap_dx overlap_dy $::mp_pc::overlap_dy \
        rand_loc $::mp_pc::rand_loc n_locations $::mp_pc::n_locations ecc_radius $::mp_pc::ecc_radius
}

# Target/surround motion + inter-trial interval + dot size.
proc mp_pc_set_motion {target_speed target_dir_deg target_lifetime \
                       surround_speed bg_lifetime iti_s pointsize} {
    set ::mp_pc::target_speed    $target_speed
    set ::mp_pc::target_dir_deg  $target_dir_deg
    set ::mp_pc::target_dir_rad  [expr {$target_dir_deg * 3.14159265 / 180.0}]
    set ::mp_pc::target_lifetime $target_lifetime
    set ::mp_pc::surround_speed  $surround_speed
    set ::mp_pc::bg_lifetime     $bg_lifetime
    set ::mp_pc::iti_s           $iti_s
    set ::mp_pc::pointsize       $pointsize
    catch {
        motionpatch_speed     dots_bg [mp_pc_speed_pu $surround_speed]
        motionpatch_lifetime  dots_bg $bg_lifetime
        motionpatch_pointsize dots_bg     $pointsize
        motionpatch_pointsize dots_target $pointsize
    }
}
proc mp_pc_get_motion {} {
    dict create target_speed $::mp_pc::target_speed target_dir_deg $::mp_pc::target_dir_deg \
        target_lifetime $::mp_pc::target_lifetime surround_speed $::mp_pc::surround_speed \
        bg_lifetime $::mp_pc::bg_lifetime iti_s $::mp_pc::iti_s pointsize $::mp_pc::pointsize
}

# Flicker mode is driven by the chosen variant (mp_pc_setup vs
# mp_pc_flicker_setup): hide the patch + fixspot, show the bar field, and
# stop trials. The per-frame driver (a prescript on the metagroup member
# dots_bg) keeps running while the patch is hidden and animates the bars.
proc mp_pc_set_flicker {flicker_mode flicker_hz} {
    set ::mp_pc::flicker_mode $flicker_mode
    set ::mp_pc::flicker_hz   $flicker_hz
    if {$flicker_mode} {
        set ::mp_pc::phase idle
        catch { mp_pc_collapse; mp_pc_tint_off }
        catch { setVisible $::mp_pc::patch_mg 0 }
        catch { setVisible $::mp_pc::fix_id   0 }
        catch { setVisible $::mp_pc::flick_mg 1 }
    } else {
        catch { setVisible $::mp_pc::flick_mg 0 }
        catch { setVisible $::mp_pc::patch_mg 1 }
        catch { setVisible $::mp_pc::fix_id   1 }
        set ::mp_pc::phase     iti
        set ::mp_pc::iti_start -1.0e9
    }
}
# Live Hz tweak used by the flicker-variant adjuster (mode stays as set).
proc mp_pc_set_flicker_hz {flicker_hz} { set ::mp_pc::flicker_hz $flicker_hz }
proc mp_pc_get_flicker_hz {} { dict create flicker_hz $::mp_pc::flicker_hz }

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup mp_pc_setup {
    patch_size_dva {float 8.0 30.0 1.0 16.0 "Patch/Field Size (dva)"}
    dot_density    {float 0.5 60.0 0.5 20.0 "Dot Density (dots/dva^2)"}
} -adjusters {pc_actions pc_conditions pc_timing pc_color pc_spatial pc_motion} \
  -label "Post-cue Trials"

# Flicker Calibration: same scene, patch hidden, red/green bar field flickering
# so the observer nulls lum_balance by minimizing flicker. lum_balance (and the
# other color knobs) persist back into Post-cue Trials.
workspace::variant flicker {} -proc mp_pc_flicker_setup \
  -adjusters {pc_color pc_flicker} -label "Flicker Calibration"

workspace::adjuster pc_actions {
    drop  {action "Trial now (↓)"}
    reset {action "Reset / idle (↑)"}
} -target {} -proc mp_pc_trigger -label "Actions"

workspace::adjuster pc_conditions {
    show_coherence {bool 1 "Coherence pulse ON"}
    show_color     {bool 1 "Color flash ON"}
    surround_alpha {float 0.0 1.0 0.05 1.0 "Surround Alpha (lower to see stimulus)"}
    loop           {bool 1 "Auto-loop trials"}
} -target {} -proc mp_pc_set_conditions -getter mp_pc_get_conditions \
  -label "Conditions & Visibility (baselines: turn one OFF)"

workspace::adjuster pc_timing {
    soa_ms       {float -600 600 10 -100 "Color SOA (ms; <0 = color BEFORE pulse)"}
    color_dur_ms {float 8 200 8 33 "Color Flash Duration (ms)"}
    coh_sigma_ms {float 20 200 5 70 "Coherence Pulse Sigma (ms)"}
    duration     {float 0.8 4.0 0.1 1.6 "Trial Duration (sec)"}
} -target {} -proc mp_pc_set_timing -getter mp_pc_get_timing \
  -label "Timing (primary manipulation)"

workspace::adjuster pc_color {
    probe_color {choice {a b random} random "Probe Color (a=red b=green)"}
    catch_frac  {float 0.0 1.0 0.05 0.2 "Catch Trial Fraction (no color)"}
    color_delta {float 0.0 0.5 0.02 0.16 "Chromatic Delta (subthreshold knob)"}
    base_lum    {float 0.0 1.0 0.05 0.5 "Base Dot Luminance"}
    lum_balance {float -0.5 0.5 0.02 0.0 "R/G Luminance Null (by eye)"}
} -target {} -proc mp_pc_set_color -getter mp_pc_get_color \
  -label "Color Probe" -colorpicker

workspace::adjuster pc_flicker {
    flicker_hz {float 4.0 20.0 1.0 12.0 "Flicker Rate (Hz)"}
} -target {} -proc mp_pc_set_flicker_hz -getter mp_pc_get_flicker_hz \
  -label "Flicker Rate"

workspace::adjuster pc_spatial {
    shape_size  {float 0.05 0.4 0.01 0.16 "Shape Aperture (patch fraction)"}
    ecc_x       {float -0.45 0.45 0.05 0.35 "Fixed Eccentricity X (patch fraction)"}
    ecc_y       {float -0.45 0.45 0.05 0.0 "Fixed Eccentricity Y (patch fraction)"}
    blob_sigma  {float 0.02 0.3 0.01 0.09 "Blob Sigma (patch fraction)"}
    overlap_dx  {float -0.3 0.3 0.02 0.0 "Blob Offset X (looseness)"}
    overlap_dy  {float -0.3 0.3 0.02 0.0 "Blob Offset Y (looseness)"}
    rand_loc    {bool 0 "Randomize Location (spatial uncertainty)"}
    n_locations {int 2 16 1 8 "Number of Locations (ring)"}
    ecc_radius  {float 0.1 0.45 0.05 0.32 "Eccentricity Radius (patch fraction)"}
} -target {} -proc mp_pc_set_spatial -getter mp_pc_get_spatial \
  -label "Shape, Blob & Location"

workspace::adjuster pc_motion {
    target_speed    {float 0.0 30.0 0.5 5.0 "Peak Speed (dva/sec)"}
    target_dir_deg  {float 0 360 15 0 "Direction (deg)"}
    target_lifetime {float 0.05 2.0 0.05 0.5 "Peak Lifetime (sec)"}
    surround_speed  {float 0.0 15.0 0.5 1.5 "Surround Speed (dva/sec)"}
    bg_lifetime     {float 0.01 1.0 0.01 0.08 "Surround Lifetime (sec)"}
    iti_s           {float 0.2 3.0 0.1 0.8 "Inter-trial Interval (sec)"}
    pointsize       {float 1.0 8.0 0.5 3.0 "Dot Size (px)"}
} -target {} -proc mp_pc_set_motion -getter mp_pc_get_motion \
  -label "Motion & Dots"
