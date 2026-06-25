# examples/motionpatch/mp_doubledrift.tcl
# Double-drift illusion (Lisi & Cavanagh 2015; Tse & Hsieh 2006),
# rendered as a motion-defined patch sweeping through a flickering
# surround -- same architecture as mp_pulsed.tcl's sweep preset, but
# with a sinusoidal back-and-forth envelope path and a carrier that's
# decoupled from the envelope's direction of motion.
#
# Geometry:
#   The aperture (a soft circular mask) oscillates back-and-forth along
#   a configurable line at a configurable eccentricity from fixation.
#   Inside the aperture, dots drift coherently in a direction set by
#   carrier_offset_deg. Outside the aperture, dots flicker (zero
#   coherence) across the entire patch area, providing a wide-field
#   surround that makes the patch motion-defined rather than luminance-
#   defined when patch_lum == surround_lum.
#
#   With the default settings (horizontal envelope stroke + perpendicular
#   carrier drift) the perceived trajectory tilts dramatically off the
#   physical line -- the larger the eccentricity, the smaller the patch,
#   the lower the contrast, the stronger the deflection. The "show
#   reference line" overlay lets you compare percept to physics.
#
# Strength knobs:
#   - eccentricity (illusion grows with peripheral viewing)
#   - aperture size (smaller -> more position uncertainty -> stronger)
#   - softness (Gaussian-like edges -> more uncertainty)
#   - patch vs surround luminance (matched = motion-only definition,
#     biggest illusion; mismatched = adds a luminance cue)
#   - envelope speed vs carrier speed ratio (carrier > envelope -> stronger)
#   - carrier_offset_deg (90 = perpendicular = canonical)
#
# Two carrier modes:
#   reversing (default) -- carrier flips with envelope velocity, so the
#                carrier always points the same way relative to the
#                envelope's instantaneous direction of travel. Both
#                strokes are deflected to the *same* side of the path
#                axis -> apparent trajectory is a single straight tilted
#                line bouncing back and forth. This is the canonical
#                Lisi & Cavanagh configuration.
#   screen    -- carrier direction is fixed in screen coordinates;
#                up-stroke and down-stroke deflect to *opposite* sides
#                of the path axis, producing a zig-zag apparent path
#                with cumulative position drift to one side. Useful as
#                a within-trial control / comparison.
#
# Independent luminance position cue:
#   lum_mod_amp / lum_mod_phase_deg modulate inside-patch dot luminance
#   in lock-step with envelope phase. This injects a position cue that
#   doesn't depend on motion -- handy for asking how much luminance
#   disambiguation discounts the motion-induced deflection.
#
# Controls:
#   Down Arrow - play / pause
#   Up Arrow   - reset (return to stroke center, paused)

load_Impro

# ============================================================
# STIM CODE
# ============================================================

# Render a filled circle into an RGBA texture for use as the mask.
# Same primitive other motionpatch demos use; spans the full texture
# so motionpatch_maskscale alone controls final aperture size.
proc mp_doubledrift_make_circle_tex {size} {
    set depth 4
    set half [expr {double($size) / 2.0}]
    set npoints 64
    set step [expr {2.0 * 3.14159265358979 / double($npoints)}]
    dl_local angles [dl_fromto 0 [expr {2.0 * 3.14159265358979}] $step]
    dl_local x [dl_add [dl_mult [dl_cos $angles] $half] $half]
    dl_local y [dl_add [dl_mult [dl_sin $angles] $half] $half]

    set img  [img_create -width $size -height $size -depth $depth]
    set poly [img_drawPolygonFast $img $x $y 255 255 255 255]
    dl_local pix [img_imgtolist $poly]
    img_delete $img $poly
    return [shaderImageCreate $pix $size $size linear]
}

# ---------- per-frame driver ----------

# Drive shared mask offset, target carrier direction, and inside
# luminance every frame. Sinusoidal back-and-forth along path_angle_deg
# at eccentricity (ecc_dva, ecc_angle_deg). Surround flicker is
# untouched here; both patches share the same mask offset so the
# surround tiles cleanly around the moving aperture.
proc mp_doubledrift_update {} {
    # StimTimeF (float ms) dt source: int StimTime makes dt alternate 8/9 ms
    # at 120 Hz, which the play_t accumulator carries into per-frame judder.
    set t [expr {double($::StimTimeF) / 1000.0}]
    set dt [expr {$t - $::mp_doubledrift::last_t}]
    if {$dt < 0.0 || $dt > 0.1} { set dt 0.016 }
    set ::mp_doubledrift::last_t $t

    if {$::mp_doubledrift::playing} {
        set ::mp_doubledrift::play_t \
            [expr {$::mp_doubledrift::play_t + $dt}]
    }
    set tplay $::mp_doubledrift::play_t

    # Sinusoidal motion. Peak speed |v|_max = amp * 2*pi*freq, where
    # amp = stroke/2. Solve for freq from the requested envelope peak
    # speed so the slider corresponds to peak velocity.
    set stroke [expr {double($::mp_doubledrift::stroke_dva)}]
    set amp    [expr {0.5 * $stroke}]
    set vpk    [expr {double($::mp_doubledrift::envelope_speed_dva_sec)}]
    if {$amp > 1e-6 && $vpk > 0.0} {
        set freq [expr {$vpk / (2.0 * 3.14159265358979 * $amp)}]
    } else {
        set freq 0.0
    }
    set phase [expr {2.0 * 3.14159265358979 * $freq * $tplay}]
    set s [expr {sin($phase)}]
    set c [expr {cos($phase)}]
    set pos_along [expr {$amp * $s}]               ;# dva along path
    set vel_along [expr {$amp * 2.0 * 3.14159265358979 * $freq * $c}]

    # World-space envelope position (eccentric center + along-path
    # displacement). path_angle_deg is interpreted relative to the
    # radial direction from fixation: 0 = along radial (in/out), 90 =
    # tangential (canonical Lisi & Cavanagh: stroke perpendicular to
    # the line from fixation, so the patch stays at constant
    # eccentricity).
    set ecc_a [expr {double($::mp_doubledrift::ecc_angle_deg) \
                     * 3.14159265358979 / 180.0}]
    set ex_dva [expr {double($::mp_doubledrift::ecc_dva) * cos($ecc_a)}]
    set ey_dva [expr {double($::mp_doubledrift::ecc_dva) * sin($ecc_a)}]
    set pa  [expr {$ecc_a + double($::mp_doubledrift::path_angle_deg) \
                            * 3.14159265358979 / 180.0}]
    set ux  [expr {cos($pa)}]
    set uy  [expr {sin($pa)}]
    set x_dva [expr {$ex_dva + $pos_along * $ux}]
    set y_dva [expr {$ey_dva + $pos_along * $uy}]

    # Mask offset is in patch-local centered coords [-0.5, 0.5]; the
    # patch is scaled by patch_size_dva. As long as |pos|+slack <
    # 0.5*patch_size the aperture stays on-patch. double() forces
    # float arithmetic (Tcl int/int = int truncation gotcha).
    set ps [expr {double($::mp_doubledrift::patch_size_dva)}]
    if {$ps <= 0.0} { set ps 1.0 }
    set ox [expr {$x_dva / $ps}]
    set oy [expr {$y_dva / $ps}]
    motionpatch_maskoffset dots_target $ox $oy
    motionpatch_maskoffset dots_bg     $ox $oy

    # Carrier direction.
    set offset_rad [expr {double($::mp_doubledrift::carrier_offset_deg) \
                          * 3.14159265358979 / 180.0}]
    if {$::mp_doubledrift::carrier_lock eq "reversing"} {
        # Canonical Lisi & Cavanagh: carrier rotates with envelope
        # velocity, so both strokes deflect to the same side of the
        # path axis -- apparent trajectory is a single straight tilted
        # line bouncing back and forth.
        if {abs($vel_along) > 1e-6} {
            set vang [expr {atan2($vel_along * $uy, $vel_along * $ux)}]
        } else {
            set vang $pa
        }
        set carrier_dir [expr {$vang + $offset_rad}]
    } else {
        # screen: carrier direction is fixed in screen coords; the
        # two strokes deflect to opposite sides of the path axis (zig-
        # zag percept) with cumulative drift in one direction. Useful
        # as a within-trial control.
        set carrier_dir [expr {$pa + $offset_rad}]
    }
    motionpatch_direction dots_target $carrier_dir

    # Carrier speed -> patch-local speed (patch-units / sec).
    set csp_dva [expr {double($::mp_doubledrift::carrier_speed_dva_sec)}]
    set csp_pu  [expr {$csp_dva / $ps}]
    motionpatch_speed dots_target $csp_pu

    # Optional luminance position cue. Modulates inside-patch dot
    # luminance with envelope phase; the surround stays at its base
    # luminance so the cue is unambiguous and orthogonal to surround
    # flicker.
    set base_lum [expr {double($::mp_doubledrift::patch_lum)}]
    set mod_amp  [expr {double($::mp_doubledrift::lum_mod_amp)}]
    if {$mod_amp != 0.0} {
        set mphase [expr {double($::mp_doubledrift::lum_mod_phase_deg) \
                          * 3.14159265358979 / 180.0}]
        set lum [expr {$base_lum + $mod_amp * sin($phase + $mphase)}]
        if {$lum < 0.0} { set lum 0.0 }
        if {$lum > 1.0} { set lum 1.0 }
        motionpatch_color dots_target $lum $lum $lum 1.0
    }
}

# ---------- setup ----------

proc mp_doubledrift_setup {patch_size_dva dot_density} {
    glistInit 1
    resetObjList
    shaderImageReset

    namespace eval ::mp_doubledrift {
        # play state
        variable playing                1
        variable play_t                 0.0
        variable last_t                 0.0

        # geometry
        variable patch_size_dva         24.0
        variable ecc_dva                8.0
        variable ecc_angle_deg          0.0
        variable path_angle_deg         90.0      ;# 90 = tangential to radial
        variable stroke_dva             4.0       ;# peak-to-peak

        # envelope (mask)
        variable aperture_frac          0.06      ;# of patch_size
        variable softness               0.0       ;# 0=hard (default), 1=very soft
        variable envelope_speed_dva_sec 4.0       ;# peak speed of envelope

        # carrier (inside dots)
        variable carrier_speed_dva_sec  6.0
        variable carrier_offset_deg     90.0      ;# 90 = perpendicular
        variable carrier_lock           reversing ;# reversing (canonical) | screen
        variable coherence              1.0
        variable jitter_deg             0.0
        variable target_lifetime        0.5
        variable patch_lum              0.8
        variable point_size             3.0

        # surround (outside dots)
        variable surround_speed_dva_sec 1.5
        variable bg_lifetime            0.4       ;# longer = smoother flow
        variable surround_lum           0.8

        # luminance position cue (independent of motion)
        variable lum_mod_amp            0.0       ;# 0..0.5 typical
        variable lum_mod_phase_deg      0.0

        # reference overlay
        variable show_path_line         0
        variable path_line_lum          0.25
        variable path_line_id           ""
        variable fix_id                 ""
    }
    set ::mp_doubledrift::patch_size_dva $patch_size_dva
    set ::mp_doubledrift::play_t 0.0
    set ::mp_doubledrift::last_t 0.0

    # Total dots distributed across the patch area; only those inside
    # the soft aperture (target) or outside it (bg) are visible per
    # patch. Dot count tracks density so visible-aperture density is
    # independent of patch_size.
    set nDots [expr {int(double($dot_density) * $patch_size_dva * $patch_size_dva)}]
    if {$nDots < 100} { set nDots 100 }

    set tex   [mp_doubledrift_make_circle_tex 256]
    set texID [shaderImageID $tex]

    set initialBgSpeed [expr {double($::mp_doubledrift::surround_speed_dva_sec) \
                              / double($patch_size_dva)}]
    set initialTgSpeed [expr {double($::mp_doubledrift::carrier_speed_dva_sec) \
                              / double($patch_size_dva)}]

    set mg [metagroup]
    objName $mg patch

    # Surround dots: outside the soft aperture (samplermaskmode 2),
    # zero coherence + short lifetime = flicker. Fills the entire
    # patch area, providing a wide-field random-dot context that the
    # inside-patch motion is defined against.
    set bgL $::mp_doubledrift::surround_lum
    set mp_bg [motionpatch $nDots $initialBgSpeed 0.5]
    objName $mp_bg dots_bg
    motionpatch_pointsize  $mp_bg $::mp_doubledrift::point_size
    motionpatch_color      $mp_bg $bgL $bgL $bgL 1.0
    motionpatch_masktype   $mp_bg 0
    motionpatch_coherence  $mp_bg 0.0
    motionpatch_lifetime   $mp_bg $::mp_doubledrift::bg_lifetime
    motionpatch_direction  $mp_bg 0.0
    motionpatch_speed      $mp_bg $initialBgSpeed
    motionpatch_setSampler $mp_bg $texID 0
    motionpatch_samplermaskmode $mp_bg 2
    motionpatch_maskscale  $mp_bg $::mp_doubledrift::aperture_frac
    # Surround uses a HARD edge: with both masks complementary at the
    # same radius, a soft surround would let bg dots bleed into the
    # target region (alphas summing to 1 across the transition band).
    # Hard cutoff here + soft target = Gabor-envelope softness on the
    # patch with no surround leakage.
    motionpatch_masksoftness $mp_bg 0.0
    metagroupAdd $mg $mp_bg

    # Target dots: inside the soft aperture (samplermaskmode 1).
    # Coherent motion at carrier_dir with target_lifetime; per-frame
    # driver overrides direction and speed.
    set tgL $::mp_doubledrift::patch_lum
    set mp_tg [motionpatch $nDots $initialTgSpeed 0.5]
    objName $mp_tg dots_target
    motionpatch_pointsize  $mp_tg $::mp_doubledrift::point_size
    motionpatch_color      $mp_tg $tgL $tgL $tgL 1.0
    motionpatch_masktype   $mp_tg 0
    motionpatch_coherence  $mp_tg $::mp_doubledrift::coherence
    motionpatch_lifetime   $mp_tg $::mp_doubledrift::target_lifetime
    motionpatch_direction  $mp_tg 0.0
    motionpatch_speed      $mp_tg $initialTgSpeed
    motionpatch_setSampler $mp_tg $texID 0
    motionpatch_samplermaskmode $mp_tg 1
    motionpatch_maskscale  $mp_tg $::mp_doubledrift::aperture_frac
    motionpatch_masksoftness $mp_tg $::mp_doubledrift::softness
    if {$::mp_doubledrift::jitter_deg > 0.0} {
        motionpatch_directionjitter $mp_tg \
            [expr {$::mp_doubledrift::jitter_deg * 3.14159265358979 / 180.0}]
    }
    metagroupAdd $mg $mp_tg

    # Drive shared mask offset, carrier, and luminance every frame.
    # One preScript on the bg patch is sufficient; it writes to both.
    addPreScript $mp_bg mp_doubledrift_update

    # Patch covers patch_size_dva on each side. Mask offset operates
    # in patch-local units so the per-frame driver expresses positions
    # in dva and divides by patch_size. patch_size must be large enough
    # to accommodate ecc + stroke/2 with margin for the aperture.
    scaleObj $mg $::mp_doubledrift::patch_size_dva \
                $::mp_doubledrift::patch_size_dva
    glistAddObject $mg 0

    # Path-line reference overlay (toggleable). Built as a polygon
    # rendered with polytype LINES so we get an actual line segment
    # connecting the two stroke endpoints in screen coords.
    set pl [polygon]
    polytype $pl LINES
    set pl_lum $::mp_doubledrift::path_line_lum
    polycolor $pl $pl_lum $pl_lum $pl_lum
    set ::mp_doubledrift::path_line_id $pl
    glistAddObject $pl 0
    mp_doubledrift_apply_path_line

    # Fixation: small yellow ring with black center at screen origin.
    # Stable eye anchor is essential for the illusion -- subjects who
    # track the patch get only a weak effect. Kept small (0.1 dva
    # outer radius) so it doesn't compete visually with the peripheral
    # patch.
    set fix_r 0.1
    set fix_mg [metagroup]
    set fix_outer [polygon]
    polycirc $fix_outer 1
    polycolor $fix_outer 0.7 0.7 0.1
    scaleObj $fix_outer [expr {2.0 * $fix_r}]
    set fix_inner [polygon]
    polycirc $fix_inner 1
    polycolor $fix_inner 0.0 0.0 0.0
    scaleObj $fix_inner [expr {0.6 * $fix_r}]
    metagroupAdd $fix_mg $fix_outer
    metagroupAdd $fix_mg $fix_inner
    set ::mp_doubledrift::fix_id $fix_mg
    glistAddObject $fix_mg 0

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1

    # Pre-position aperture at stroke center (eccentric) so the patch
    # is held still at its starting location until "play".
    set ecc_a [expr {double($::mp_doubledrift::ecc_angle_deg) \
                     * 3.14159265358979 / 180.0}]
    set ox0 [expr {double($::mp_doubledrift::ecc_dva) * cos($ecc_a) \
                   / double($::mp_doubledrift::patch_size_dva)}]
    set oy0 [expr {double($::mp_doubledrift::ecc_dva) * sin($ecc_a) \
                   / double($::mp_doubledrift::patch_size_dva)}]
    motionpatch_maskoffset dots_target $ox0 $oy0
    motionpatch_maskoffset dots_bg     $ox0 $oy0
    redraw
}

# ---------- helpers ----------

# Recompute the path-line overlay's vertices in screen coords (dva)
# from the current eccentricity, path angle, and stroke length.
# Re-called whenever geometry changes.
proc mp_doubledrift_apply_path_line {} {
    set pl $::mp_doubledrift::path_line_id
    if {$pl eq ""} { return }
    if {!$::mp_doubledrift::show_path_line} {
        polyverts $pl [dl_flist 0 0] [dl_flist 0 0] [dl_flist 0 0]
        return
    }
    set ecc_a [expr {double($::mp_doubledrift::ecc_angle_deg) \
                     * 3.14159265358979 / 180.0}]
    set ex [expr {double($::mp_doubledrift::ecc_dva) * cos($ecc_a)}]
    set ey [expr {double($::mp_doubledrift::ecc_dva) * sin($ecc_a)}]
    set pa [expr {$ecc_a + double($::mp_doubledrift::path_angle_deg) \
                           * 3.14159265358979 / 180.0}]
    set amp [expr {0.5 * double($::mp_doubledrift::stroke_dva)}]
    set ux [expr {cos($pa)}]
    set uy [expr {sin($pa)}]
    set x0 [expr {$ex - $amp * $ux}]
    set y0 [expr {$ey - $amp * $uy}]
    set x1 [expr {$ex + $amp * $ux}]
    set y1 [expr {$ey + $amp * $uy}]
    polyverts $pl [dl_flist $x0 $x1] [dl_flist $y0 $y1] [dl_flist 0 0]
    set L $::mp_doubledrift::path_line_lum
    polycolor $pl $L $L $L
}

# ---------- actions ----------

proc mp_doubledrift_trigger {action} {
    switch -- $action {
        play_pause {
            set ::mp_doubledrift::playing \
                [expr {!$::mp_doubledrift::playing}]
        }
        reset {
            set ::mp_doubledrift::playing 0
            set ::mp_doubledrift::play_t 0.0
            set ecc_a [expr {double($::mp_doubledrift::ecc_angle_deg) \
                             * 3.14159265358979 / 180.0}]
            set ox0 [expr {double($::mp_doubledrift::ecc_dva) * cos($ecc_a) \
                           / double($::mp_doubledrift::patch_size_dva)}]
            set oy0 [expr {double($::mp_doubledrift::ecc_dva) * sin($ecc_a) \
                           / double($::mp_doubledrift::patch_size_dva)}]
            catch {motionpatch_maskoffset dots_target $ox0 $oy0}
            catch {motionpatch_maskoffset dots_bg     $ox0 $oy0}
            # Restore base luminance in case lum modulation was on.
            set L $::mp_doubledrift::patch_lum
            catch {motionpatch_color dots_target $L $L $L 1.0}
        }
    }
    return
}

proc onDownArrow {} { mp_doubledrift_trigger play_pause }
proc onUpArrow   {} { mp_doubledrift_trigger reset }

# ---------- adjuster targets ----------

proc mp_doubledrift_set_geometry {ecc_dva ecc_angle_deg path_angle_deg stroke_dva} {
    set ::mp_doubledrift::ecc_dva        [expr {double($ecc_dva)}]
    set ::mp_doubledrift::ecc_angle_deg  [expr {double($ecc_angle_deg)}]
    set ::mp_doubledrift::path_angle_deg [expr {double($path_angle_deg)}]
    set ::mp_doubledrift::stroke_dva     [expr {double($stroke_dva)}]
    mp_doubledrift_apply_path_line
}
proc mp_doubledrift_get_geometry {{target {}}} {
    dict create ecc_dva 8.0 ecc_angle_deg 0.0 path_angle_deg 90.0 stroke_dva 4.0
}

proc mp_doubledrift_set_envelope {aperture_frac softness envelope_speed} {
    set ::mp_doubledrift::aperture_frac          [expr {double($aperture_frac)}]
    set ::mp_doubledrift::softness               [expr {double($softness)}]
    set ::mp_doubledrift::envelope_speed_dva_sec [expr {double($envelope_speed)}]
    catch {motionpatch_maskscale    dots_target $::mp_doubledrift::aperture_frac}
    catch {motionpatch_maskscale    dots_bg     $::mp_doubledrift::aperture_frac}
    catch {motionpatch_masksoftness dots_target $::mp_doubledrift::softness}
    # Surround stays hard (see setup comment) -- only target is soft.
}
proc mp_doubledrift_get_envelope {{target {}}} {
    dict create aperture_frac 0.06 softness 0.0 envelope_speed 4.0
}

proc mp_doubledrift_set_carrier {carrier_speed carrier_offset_deg carrier_lock} {
    set ::mp_doubledrift::carrier_speed_dva_sec [expr {double($carrier_speed)}]
    set ::mp_doubledrift::carrier_offset_deg    [expr {double($carrier_offset_deg)}]
    set ::mp_doubledrift::carrier_lock          $carrier_lock
}
proc mp_doubledrift_get_carrier {{target {}}} {
    dict create carrier_speed 6.0 carrier_offset_deg 90.0 carrier_lock reversing
}

proc mp_doubledrift_set_dots {coherence jitter_deg target_lifetime patch_lum} {
    set ::mp_doubledrift::coherence       [expr {double($coherence)}]
    set ::mp_doubledrift::jitter_deg      [expr {double($jitter_deg)}]
    set ::mp_doubledrift::target_lifetime [expr {double($target_lifetime)}]
    set ::mp_doubledrift::patch_lum       [expr {double($patch_lum)}]
    catch {motionpatch_coherence dots_target $::mp_doubledrift::coherence}
    catch {motionpatch_directionjitter dots_target \
               [expr {$::mp_doubledrift::jitter_deg \
                      * 3.14159265358979 / 180.0}]}
    catch {motionpatch_lifetime  dots_target $::mp_doubledrift::target_lifetime}
    set L $::mp_doubledrift::patch_lum
    catch {motionpatch_color dots_target $L $L $L 1.0}
}
proc mp_doubledrift_get_dots {{target {}}} {
    dict create coherence 1.0 jitter_deg 0.0 target_lifetime 0.5 patch_lum 0.8
}

proc mp_doubledrift_set_surround {surround_speed bg_lifetime surround_lum} {
    set ::mp_doubledrift::surround_speed_dva_sec [expr {double($surround_speed)}]
    set ::mp_doubledrift::bg_lifetime            [expr {double($bg_lifetime)}]
    set ::mp_doubledrift::surround_lum           [expr {double($surround_lum)}]
    set sp_pu [expr {$::mp_doubledrift::surround_speed_dva_sec \
                     / double($::mp_doubledrift::patch_size_dva)}]
    catch {motionpatch_speed    dots_bg $sp_pu}
    catch {motionpatch_lifetime dots_bg $::mp_doubledrift::bg_lifetime}
    set L $::mp_doubledrift::surround_lum
    catch {motionpatch_color    dots_bg $L $L $L 1.0}
}
proc mp_doubledrift_get_surround {{target {}}} {
    dict create surround_speed 1.5 bg_lifetime 0.4 surround_lum 0.8
}

proc mp_doubledrift_set_lum_mod {lum_mod_amp lum_mod_phase_deg} {
    set ::mp_doubledrift::lum_mod_amp       [expr {double($lum_mod_amp)}]
    set ::mp_doubledrift::lum_mod_phase_deg [expr {double($lum_mod_phase_deg)}]
    if {$::mp_doubledrift::lum_mod_amp == 0.0} {
        set L $::mp_doubledrift::patch_lum
        catch {motionpatch_color dots_target $L $L $L 1.0}
    }
}
proc mp_doubledrift_get_lum_mod {{target {}}} {
    dict create lum_mod_amp 0.0 lum_mod_phase_deg 0.0
}

proc mp_doubledrift_set_overlay {show_path_line path_line_lum} {
    set ::mp_doubledrift::show_path_line [expr {int($show_path_line)}]
    set ::mp_doubledrift::path_line_lum  [expr {double($path_line_lum)}]
    mp_doubledrift_apply_path_line
}
proc mp_doubledrift_get_overlay {{target {}}} {
    dict create show_path_line 0 path_line_lum 0.25
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup mp_doubledrift_setup {
    patch_size_dva {float 8.0 32.0 1.0 24.0 "Patch Size (dva)"}
    dot_density    {float 0.5 60.0 0.5 40.0 "Dot Density (dots/dva^2)"}
} -adjusters {dd_actions dd_geometry dd_envelope dd_carrier dd_dots dd_surround dd_lum_mod dd_overlay dd_transform} \
  -label "Double Drift"

workspace::adjuster dd_actions {
    play_pause {action "Play / Pause (↓)"}
    reset      {action "Reset (↑)"}
} -target {} -proc mp_doubledrift_trigger -label "Actions"

# Geometry of the physical envelope path. ecc/ecc_angle place the patch
# in the visual field; path_angle orients the back-and-forth axis;
# stroke is peak-to-peak length. Keep |ecc| + stroke/2 < 0.5*patch_size
# so the aperture stays inside the patch's local extent.
workspace::adjuster dd_geometry {
    ecc_dva        {float 0.0 14.0  0.5 8.0 "Eccentricity (dva)"}
    ecc_angle_deg  {float 0.0 360.0 5.0 0.0 "Eccentricity Angle (deg, 0=right)"}
    path_angle_deg {float 0.0 360.0 5.0 90.0 "Path Angle (deg, 0=radial, 90=tangential)"}
    stroke_dva     {float 0.5 12.0  0.25 4.0 "Stroke Length (peak-to-peak, dva)"}
} -target {} -proc mp_doubledrift_set_geometry \
  -getter mp_doubledrift_get_geometry -label "Path Geometry"

# Envelope (mask) properties. Smaller aperture + higher softness =
# more position uncertainty = stronger illusion.
workspace::adjuster dd_envelope {
    aperture_frac  {float 0.02 0.3 0.005 0.06 "Aperture (fraction of patch)"}
    softness       {float 0.0  1.0 0.05  0.0  "Edge Softness (target only)"}
    envelope_speed {float 0.0 12.0 0.25  4.0  "Envelope Peak Speed (dva/sec)"}
} -target {} -proc mp_doubledrift_set_envelope \
  -getter mp_doubledrift_get_envelope -label "Envelope (mask)"

# Carrier (internal dot motion). carrier_offset_deg = 90 is the
# canonical perpendicular configuration; flip the sign to mirror the
# illusion direction.
#   screen    -- direction fixed in screen coords; deflection accumulates
#   reversing -- direction flips with envelope velocity; deflection
#                alternates per stroke (control condition)
workspace::adjuster dd_carrier {
    carrier_speed       {float 0.0 16.0 0.5 6.0  "Carrier Speed (dva/sec)"}
    carrier_offset_deg  {float -180.0 180.0 5.0 90.0 "Carrier Offset from Path (deg)"}
    carrier_lock        {choice {reversing screen} reversing "Carrier Lock"}
} -target {} -proc mp_doubledrift_set_carrier \
  -getter mp_doubledrift_get_carrier -label "Carrier (internal dots)"

# Dot statistics + base luminance. Set patch_lum == surround_lum for
# motion-only definition (strongest illusion). Raising patch_lum above
# surround_lum adds a luminance cue that disambiguates position.
workspace::adjuster dd_dots {
    coherence        {float 0.0 1.0 0.05 1.0 "Inside Coherence"}
    jitter_deg       {float 0.0 90.0 1.0 0.0 "Inside Direction Jitter (deg)"}
    target_lifetime  {float 0.05 2.0 0.05 0.5 "Inside Lifetime (sec)"}
    patch_lum        {float 0.0 1.0 0.05 0.8 "Inside Luminance"}
} -target {} -proc mp_doubledrift_set_dots \
  -getter mp_doubledrift_get_dots -label "Inside Patch"

# Surround (outside aperture) flicker properties. Short lifetime +
# zero coherence = pure spatiotemporal noise. surround_lum should
# typically match patch_lum so the patch is motion-defined.
workspace::adjuster dd_surround {
    surround_speed {float 0.0 15.0 0.25 1.5 "Surround Speed (dva/sec)"}
    bg_lifetime    {float 0.01 1.0 0.01 0.4 "Surround Lifetime (sec)"}
    surround_lum   {float 0.0  1.0 0.05 0.8  "Surround Luminance"}
} -target {} -proc mp_doubledrift_set_surround \
  -getter mp_doubledrift_get_surround -label "Surround"

# Independent luminance position cue. Modulates inside dot luminance
# in lock-step with envelope phase. amp 0 = off; phase shifts where
# the bright/dark extremes fall along the stroke.
workspace::adjuster dd_lum_mod {
    lum_mod_amp        {float 0.0 0.5 0.02 0.0 "Luminance Mod Amp"}
    lum_mod_phase_deg  {float 0.0 360.0 5.0 0.0 "Luminance Mod Phase (deg)"}
} -target {} -proc mp_doubledrift_set_lum_mod \
  -getter mp_doubledrift_get_lum_mod -label "Luminance Cue (independent)"

# Reference overlay. Toggle the physical-path line on to compare
# percept to physics.
workspace::adjuster dd_overlay {
    show_path_line {choice {0 1} 0 "Show Physical Path"}
    path_line_lum  {float 0.05 1.0 0.05 0.25 "Path Line Luminance"}
} -target {} -proc mp_doubledrift_set_overlay \
  -getter mp_doubledrift_get_overlay -label "Reference Overlay"

workspace::adjuster dd_transform -template scale -target patch \
  -label "Scene Size"
