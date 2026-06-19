# examples/polygon/sectors.tcl
# Anti-aliased round shapes: sectors (pac-men), annuli (rings), and arc bands.
# Demonstrates: the polygon module's masked-shape commands
#   polycirc   $p 1          -- solid anti-aliased disc
#   polysector $p mouthDeg   -- disc with a wedge ("mouth") removed (pac-man)
#   polyannulus $p innerFrac -- ring (hole = innerFrac of the outer radius)
# Combine polysector + polyannulus for an arc band. All are drawn on the unit
# quad (scale it to the diameter) and masked in the fragment shader, so the
# edges are anti-aliased and the mouth/inner-radius change live (just uniforms,
# no vertex work). The mouth is centred on +X -- aim it by rotating the object.
#
# Requires modules: polygon, metagroup.

namespace eval sect { variable shapes {} }   ;# the round shapes currently shown

# Build one round shape on the unit quad: sector / annulus / arc / disc.
proc sect_shape { mouth inner } {
    set p [polygon]
    if { $mouth > 0 } { polysector  $p $mouth }
    if { $inner > 0 } { polyannulus $p $inner }
    if { $mouth <= 0 && $inner <= 0 } { polycirc $p 1 }
    return $p
}

# ============================================================
# Single-shape variants (one shape you can tweak live)
# ============================================================
proc sect_setup { startmouth startinner } {
    glistInit 1
    resetObjList
    setBackground 40 40 50

    set p [sect_shape $startmouth $startinner]
    objName $p shape
    polycolor $p 1.0 0.6 0.1

    set mg [metagroup]
    metagroupAdd $mg $p
    objName $mg shape_mg
    scaleObj $mg 4.0 4.0          ;# unit quad -> ~4 units across

    set sect::shapes [list $p]
    glistAddObject $mg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

proc pacman_setup {} { sect_setup 90  0.0 }   ;# sector
proc ring_setup   {} { sect_setup 0   0.6 }   ;# annulus
proc arc_setup    {} { sect_setup 120 0.5 }   ;# annular sector (arc band)
proc disc_setup   {} { sect_setup 0   0.0 }   ;# plain AA disc

# ============================================================
# Kanizsa square -- four sectors aimed inward (the reason polysector exists)
# ============================================================
proc kanizsa_setup {} {
    glistInit 1
    resetObjList
    setBackground 150 150 150     ;# light grey -> bright illusory square

    set mg [metagroup]
    objName $mg shape_mg
    set ids {}
    set h 1.0
    foreach {cx cy} [list $h $h  $h [expr {-$h}]  [expr {-$h}] [expr {-$h}]  [expr {-$h}] $h] {
        set p [polygon]
        scaleObj $p 0.8 0.8
        polysector $p 90
        polycolor $p 0.12 0.12 0.12
        translateObj $p $cx $cy
        rotateObj $p [expr {atan2(-$cy,-$cx)*180.0/$::pi}] 0 0 1   ;# mouth -> centre
        metagroupAdd $mg $p
        lappend ids $p
    }
    scaleObj $mg 2.0 2.0
    set sect::shapes $ids
    glistAddObject $mg 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# Live adjusters (mouth/inner are plain uniforms -> no rebuild)
# ============================================================
proc sect_set_mouth { mouth } { foreach p $sect::shapes { polysector  $p $mouth } ; redraw }
proc sect_get_mouth { {target {}} } {
    dict create mouth [polysector [lindex $sect::shapes 0]]
}
proc sect_set_inner { inner } { foreach p $sect::shapes { polyannulus $p $inner } ; redraw }
proc sect_get_inner { {target {}} } {
    dict create inner [polyannulus [lindex $sect::shapes 0]]
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup pacman_setup {} \
    -adjusters {sect_mouth sect_inner shape_scale shape_rotation shape_color} \
    -label "Pac-man (sector)"

workspace::variant ring {} -proc ring_setup \
    -adjusters {sect_mouth sect_inner shape_scale shape_rotation shape_color} -label "Ring (annulus)"

workspace::variant arc {} -proc arc_setup \
    -adjusters {sect_mouth sect_inner shape_scale shape_rotation shape_color} -label "Arc band"

workspace::variant disc {} -proc disc_setup \
    -adjusters {sect_mouth sect_inner shape_scale shape_rotation shape_color} -label "Disc (AA)"

workspace::variant kanizsa {} -proc kanizsa_setup \
    -adjusters {sect_mouth shape_scale shape_rotation} -label "Kanizsa square"

workspace::adjuster sect_mouth {
    mouth {float 0.0 350.0 5.0 90.0 "Mouth" deg}
} -target {} -proc sect_set_mouth -getter sect_get_mouth -label "Mouth"

workspace::adjuster sect_inner {
    inner {float 0.0 0.95 0.05 0.0 "Inner radius" frac}
} -target {} -proc sect_set_inner -getter sect_get_inner -label "Inner radius"

workspace::adjuster shape_scale    -template scale    -target shape_mg
workspace::adjuster shape_rotation -template rotation -target shape_mg
workspace::adjuster shape_color    -template color    -target shape

# Build something when sourced directly.
pacman_setup
