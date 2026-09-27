# test_dynlist_numeric.tcl -- every numeric dynlist type, in a real stim2
#
#   stim2 -w 600 -h 600 -f /abs/path/to/examples/test_dynlist_numeric.tcl
#
# dlsh lists are char, short, long (32-bit), float and, since 2026, int64
# and double. The modules that take coordinate, vector, matrix, id and pixel
# lists read all of them through stimdlls/src/dlnumeric.h. This draws each
# object from float lists and from the other types and compares the frames
# byte for byte, checks the non-drawing commands directly, prints OK/FAIL
# lines and exits non-zero on any failure.
#
# Frames go to a temporary directory; mesh shaders come from ../shaders.
set SP [file join [expr {[info exists ::env(TMPDIR)] ? $::env(TMPDIR) : "/tmp"}] stim2_dynlist_numeric]
file mkdir $SP
set SHADERS [file normalize [file join [file dirname [file normalize [info script]]] .. shaders]]
set ::log {}
proc note { s } { lappend ::log $s }
proc ck { label cond } { note "[expr {$cond ? {OK  } : {FAIL}}] $label" }

note "dl_dlist available: [expr {[info commands dl_dlist] ne {}}]"

# --- direct checks --------------------------------------------------------
set mf [dl_tcllist [mat4_setTranslation [mat4_identity] [dl_flist 1 2 3]]]
set md [dl_tcllist [mat4_setTranslation [mat4_identity] [dl_dlist 1 2 3]]]
set mw [dl_tcllist [mat4_setTranslation [mat4_identity] [dl_wlist 1 2 3]]]
set mm [dl_tcllist [mat4_setTranslation [dl_double [mat4_identity]] [dl_flist 1 2 3]]]
ck "mat4: double vec3 == float vec3" [expr {$md eq $mf}]
ck "mat4: int64 vec3 == float vec3" [expr {$mw eq $mf}]
ck "mat4: double matrix accepted" [expr {$mm eq $mf}]
ck "mat4: wrong length still refused" [catch { mat4_setTranslation [mat4_identity] [dl_dlist 1 2] }]

glistInit 1
resetObjList
set a [polygon]; set b [polygon]
set mg [metagroup]
ck "metagroupAdd: int64 ids" [expr {![catch { metagroupAdd $mg [dl_wlist $a $b] } e]}]
ck "metagroupRemove: int64 ids" [expr {![catch { metagroupRemove $mg [dl_wlist $a] } e]}]
ck "metagroupAdd: long ids still work" [expr {![catch { metagroupAdd $mg [dl_ilist $a] } e]}]
ck "metagroupAdd: float ids refused" [expr {[catch { metagroupAdd $mg [dl_flist 1.5] } e] && [string match "*integer ids*" $e]}]
ck "polyverts: string list refused" [expr {[catch { polyverts $a [dl_slist a b c] [dl_flist 1 2 3] } e] && [string match "*numeric*" $e]}]
ck "polyverts: length mismatch refused" [catch { polyverts $a [dl_dlist 1 2 3] [dl_flist 1 2] }]

set g4 [dl_fromto 0 16]
ck "shaderImageCreate: double 4x4" [expr {![catch { shaderImageCreate [dl_div [dl_double $g4] 15.] 4 4 } e]}]
ck "shaderImageCreate: long 4x4" [expr {![catch { shaderImageCreate [dl_int $g4] 4 4 } e]}]
ck "shaderImageCreate: int64 4x4" [expr {![catch { shaderImageCreate [dl_int64 $g4] 4 4 } e]}]
set rgbf [dl_llist [dl_flist 1 0 0 1] [dl_flist 0 1 0 1] [dl_flist 0 0 1 1]]
ck "shaderImageCreate: RGB of float sublists now an error" [catch { shaderImageCreate $rgbf 2 2 }]
set rgbc [dl_llist [dl_char [dl_ilist 255 0 0 9]] [dl_char [dl_ilist 0 255 0 9]] [dl_char [dl_ilist 0 0 255 9]]]
ck "shaderImageCreate: RGB of char sublists still works" [expr {![catch { shaderImageCreate $rgbc 2 2 } e]}]
ck "shaderImageCreate: 8 values at 2x2 = two gray layers" [expr {![catch { shaderImageCreate [dl_dlist 1 2 3 4 5 6 7 8] 2 2 } e]}]
ck "shaderImageCreate: 5 values at 2x2 refused" [catch { shaderImageCreate [dl_dlist 1 2 3 4 5] 2 2 }]

# --- meshObj (needs a mesh shader; no frame comparison, the shader animates)
catch { load_modules mesh }
if { [info commands meshObj] ne "" && [file exists [file join $SHADERS noisyimage2.glsl]] } {
    meshShaderSetPath $SHADERS/
    set sh [meshShaderBuild noisyimage2]
    set v {-.5 -.5 0 -.5 .5 0 .5 .5 0 .5 .5 0 .5 -.5 0 -.5 -.5 0}
    set t {0 0 0 1 1 1 1 1 1 0 0 0}
    ck "meshObj: float verts/uvs" [expr {![catch { meshObj [dl_flist {*}$v] [dl_flist {*}$t] $sh } e]}]
    ck "meshObj: double verts/uvs" [expr {![catch { meshObj [dl_dlist {*}$v] [dl_dlist {*}$t] $sh } e]}]
    ck "meshObj: mixed double verts, float uvs" [expr {![catch { meshObj [dl_dlist {*}$v] [dl_flist {*}$t] $sh } e]}]
    ck "meshObj: string verts refused" [expr {[catch { meshObj [dl_slist a b c] [dl_flist {*}$t] $sh } e] && [string match "*vertex datatype*" $e]}]
    ck "meshObj: uv count mismatch refused" [catch { meshObj [dl_dlist {*}$v] [dl_dlist 0 1] $sh }]
} else {
    note "SKIP meshObj: no mesh module or no $SHADERS/noisyimage2.glsl"
}

# --- rendered comparisons -------------------------------------------------
set tri_x {-5 5 0}; set tri_y {-4 -4 6}
set stages {}
foreach {name kind} {
    poly_float float poly_double double poly_long long poly_int64 int64
    shape_float float shape_double double
    img_float float img_long long img_double double img_int64 int64
} { lappend stages [list $name $kind] }

proc mk { kind vals } {
    switch $kind {
        float  { return [dl_flist {*}$vals] }
        double { return [dl_dlist {*}$vals] }
        long   { return [dl_ilist {*}$vals] }
        int64  { return [dl_int64 [dl_ilist {*}$vals]] }
    }
}

proc build { name kind } {
    glistInit 1
    resetObjList
    switch -glob $name {
        poly_* {
            set o [polygon]
            polyverts $o [mk $kind $::tri_x] [mk $kind $::tri_y]
            polycolor $o 1 1 1
        }
        shape_* {
            set o [shape [mk $kind {-6 6 6 -6}] [mk $kind {-3 -3 3 3}] -fill {0.9 0.8 0.2}]
        }
        img_* {
            # 2x2 gray levels 0, 85, 170, 255 as 0-1 reals or 0-255 integers
            if { $kind in {float double} } {
                set vals {0 0.33333334 0.6666667 1.0}
            } else {
                set vals {0 85 170 255}
            }
            set t [imageTextureFromList [mk $kind $vals] 2 2 nearest]
            set o [image $t]
            scaleObj $o 10 10
        }
    }
    glistAddObject $o 0
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

proc run_stage { i } {
    if { $i >= [llength $::stages] } { finish; return }
    lassign [lindex $::stages $i] name kind
    if { [catch { build $name $kind } e] } {
        note "FAIL $name: $e"
        after 50 [list run_stage [expr {$i+1}]]
        return
    }
    after 300 [list dump_stage $i $name]
}
proc dump_stage { i name } {
    dumpRaw $::SP/frame_$name.raw
    after 50 [list run_stage [expr {$i+1}]]
}

proc same { a b } {
    set fa $::SP/frame_$a.raw; set fb $::SP/frame_$b.raw
    if { ![file exists $fa] || ![file exists $fb] } { return 0 }
    set f [open $fa rb]; set da [read $f]; close $f
    set f [open $fb rb]; set db [read $f]; close $f
    return [expr {$da eq $db}]
}
proc drawn { a } {
    # the frame is not just background: some byte differs from the first pixel
    if { ![file exists $::SP/frame_$a.raw] } { return 0 }
    set f [open $::SP/frame_$a.raw rb]; set d [read $f]; close $f
    set px [string range $d 20 23]
    return [expr {[string map [list $px ""] [string range $d 20 end]] ne ""}]
}

proc finish {} {
    if { [catch { finish_checks } e] } { note "FAIL finish: $e" }
    puts [join $::log "\n"]
    set nfail [llength [lsearch -all $::log FAIL*]]
    puts [expr {$nfail ? "$nfail FAILURE(S)" : "ALL PASS ([llength [lsearch -all $::log OK*]] checks)"}]
    exit [expr {$nfail ? 1 : 0}]
}
proc finish_checks {} {
    ck "polygon: something drawn" [drawn poly_float]
    ck "polygon: double verts render identically to float" [same poly_float poly_double]
    ck "polygon: long verts render identically to float" [same poly_float poly_long]
    ck "polygon: int64 verts render identically to float" [same poly_float poly_int64]
    ck "shape: something drawn" [drawn shape_float]
    ck "shape: double outline renders identically to float" [same shape_float shape_double]
    ck "image: something drawn" [drawn img_float]
    ck "image: long levels == float intensities (the long* bug)" [same img_float img_long]
    ck "image: double == float" [same img_float img_double]
    ck "image: int64 == long" [same img_long img_int64]
}

after 200 { run_stage 0 }
