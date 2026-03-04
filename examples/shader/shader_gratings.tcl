# examples/shader/shader_gratings.tcl
# Drifting grating grid demonstration
# Demonstrates: independence of spatial frequency (CPD) and drift speed (deg/sec)
#
# REQUIRES
#   polygon
#   metagroup
#   shaders
#   text
#   sineshader (in shader path)
#
# Shows a 4x4 grid of drifting gratings:
#   Rows:    spatial frequency (1, 2, 3, 4 CPD)
#   Columns: drift speed (1, 2, 3, 4 deg/sec)
#
# The temporal frequency (what the shader needs) is derived:
#   TF (Hz) = speed (deg/sec) * SF (CPD)
#
# Key observations:
#   - Each row has the same stripe width (same CPD)
#   - Each column has the same physical drift speed (same deg/sec)
#   - TF increases along both axes (bottom-right is fastest flicker)
#
# Note: All patches use the sineshader with Circular=0 (linear gratings),
# vertical orientation (drift is horizontal), and Gaussian envelope.

# ============================================================
# SHADER PATH SETUP
# ============================================================
if {![info exists ::shader_demo_path_set]} {
    foreach dir {
        ./shaders/
        ../shaders/
        /usr/local/share/stim2/shaders/
    } {
        if {[file exists $dir] && [file isdirectory $dir]} {
            shaderAddPath $dir
            break
        }
    }
    set ::shader_demo_path_set 1
}

# ============================================================
# PARAMETERS
# ============================================================

variable grating_cpds   {1.0 2.0 3.0 4.0}    ;# rows: spatial frequency (CPD)
variable grating_speeds {1.0 2.0 3.0 4.0}     ;# cols: drift speed (deg/sec)
variable grating_patch_size  2.28              ;# size of each patch in degrees
variable grating_spacing     3.04              ;# center-to-center spacing
variable grating_y_shift     0.8               ;# shift grid up to make room for bottom labels
variable grating_K           6.0               ;# Gaussian envelope: K (sigma = 1/K normalized)
variable grating_contrast    1.0
variable grating_ori         90.0              ;# 90 = vertical stripes, horizontal drift

# ============================================================
# LABEL HELPERS
# ============================================================

proc make_label { str x y size {justify center} {valign center} } {
    set t [text $str -font sans -size $size]
    textColor $t 0.8 0.8 0.8 1.0
    textJustify $t $justify
    textValign $t $valign
    translateObj $t $x $y
    return $t
}

# ============================================================
# STIM CODE
# ============================================================

proc gratings_grid_setup {} {
    variable grating_cpds
    variable grating_speeds
    variable grating_patch_size
    variable grating_spacing
    variable grating_K
    variable grating_contrast
    variable grating_ori
    variable grating_y_shift

    set nrows [llength $grating_cpds]
    set ncols [llength $grating_speeds]

    # Set background to mid-gray to match grating mean luminance
    setBackground 128 128 128

    glistInit 1
    resetObjList
    shaderDeleteAll

    # Load font for labels
    textFont sans NotoSans-Regular.ttf

    set shader [shaderBuild sineshader]

    # Normalized sigma from K: same convention as experiment loader
    set sigma_norm [expr {1.0 / $grating_K}]

    # Center the grid around origin, shifted up for bottom labels
    set x_offset [expr {-($ncols - 1) * $grating_spacing / 2.0}]
    set y_offset [expr {-($nrows - 1) * $grating_spacing / 2.0 + $grating_y_shift}]

    for {set r 0} {$r < $nrows} {incr r} {
        set cpd [lindex $grating_cpds $r]

        for {set c 0} {$c < $ncols} {incr c} {
            set speed_dps [lindex $grating_speeds $c]

            # Derive temporal frequency: TF = speed * SF
            set tf [expr {$speed_dps * $cpd}]

            # NCycles = CPD * patch_width_in_degrees
            set ncycles [expr {$cpd * $grating_patch_size}]

            # Position: column along X, row along Y (bottom to top)
            set px [expr {$x_offset + $c * $grating_spacing}]
            set py [expr {$y_offset + $r * $grating_spacing}]

            # Build the grating patch
            set sobj [shaderObj $shader]
            objName $sobj "grating_${r}_${c}"
            shaderObjSetUniform $sobj NCycles      $ncycles
            shaderObjSetUniform $sobj CyclesPerSec $tf
            shaderObjSetUniform $sobj Sigma        $sigma_norm
            shaderObjSetUniform $sobj Contrast     $grating_contrast
            shaderObjSetUniform $sobj Envelope     1
            shaderObjSetUniform $sobj Circular     0

            # Orientation: rotate so stripes are vertical, drift is horizontal
            rotateObj $sobj $grating_ori 0 0 1

            scaleObj $sobj $grating_patch_size $grating_patch_size
            translateObj $sobj $px $py

            glistAddObject $sobj 0
        }
    }

    # --- Row labels (left side): CPD values ---
    set label_x [expr {$x_offset - $grating_spacing * 0.6}]
    for {set r 0} {$r < $nrows} {incr r} {
        set cpd [lindex $grating_cpds $r]
        set py [expr {$y_offset + $r * $grating_spacing}]
        set lbl [make_label "[expr {int($cpd)}]" $label_x $py 0.5 right]
        glistAddObject $lbl 0
    }

    # --- Column labels (below grid): speed values ---
    set label_y [expr {$y_offset - $grating_spacing * 0.6}]
    for {set c 0} {$c < $ncols} {incr c} {
        set speed_dps [lindex $grating_speeds $c]
        set px [expr {$x_offset + $c * $grating_spacing}]
        set lbl [make_label "[expr {int($speed_dps)}]" $px $label_y 0.5]
        glistAddObject $lbl 0
    }

    # --- Axis titles ---
    set title_x [expr {$x_offset - $grating_spacing * 1.1}]
    set lbl [make_label "CPD" $title_x 0.0 0.6 right]
    glistAddObject $lbl 0

    set title_y [expr {$y_offset - $grating_spacing * 0.9}]
    set lbl [make_label "deg/s" 0.0 $title_y 0.5]
    glistAddObject $lbl 0

    glistSetCurGroup 0
    glistSetDynamic 0 1
    glistSetVisible 1
    redraw
}

# ============================================================
# ADJUSTER PROCS
# ============================================================

proc set_grating_uniform {uniform val} {
    foreach sobj [objFind -type shader -match "grating_*"] {
        shaderObjSetUniform $sobj $uniform $val
    }
    redraw
}

proc set_gratings_contrast {name val} {
    set_grating_uniform Contrast $val
}

proc set_gratings_sigma {name val} {
    set sigma_norm [expr {1.0 / $val}]
    set_grating_uniform Sigma $sigma_norm
}

proc set_gratings_envelope {name val} {
    set_grating_uniform Envelope [expr {$val ? 1 : 0}]
}

# Rebuild grid when CPD or speed ranges change
proc rebuild_grid {name val} {
    gratings_grid_setup
}

# ============================================================
# WORKSPACE DEMO INTERFACE
# ============================================================
workspace::reset

# Real-time adjusters (no rebuild needed)
workspace::adjuster gratings_contrast {
    value {float 0.0 1.0 0.01 1.0 "Contrast"}
} -target shader_obj -proc set_gratings_contrast -label "Contrast"

workspace::adjuster gratings_sigma {
    value {float 2.0 12.0 0.5 6.0 "K"}
} -target shader_obj -proc set_gratings_sigma -label "Envelope K (σ=1/K)"

workspace::adjuster gratings_envelope {
    value {bool 1 "Enable"}
} -target shader_obj -proc set_gratings_envelope -label "Gaussian Envelope"

# ---- Setup ----
workspace::setup gratings_grid_setup {} \
    -adjusters {gratings_contrast gratings_sigma gratings_envelope} \
    -label "Drifting Gratings (CPD × Speed)"
