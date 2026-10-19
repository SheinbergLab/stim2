# examples/shader/shader_spotlight.tcl
# Spotlight occluder: a soft-edged view window over a scrolling scene
# Demonstrates: shaderBuildInline, a full-screen shader object working in
#   eye-space dva (modelviewMat applied in the vertex shader), a separable
#   falloff with independent x / y feathers, frame-script scrolling
#
# The occluder from the Door Runner paradigm (~/systems/ess/runner/door):
# a dark field with a window around the avatar that reaches `lead` dva
# ahead (the direction of travel), `trail` dva behind and +/- `halfh` dva
# vertically. Feathers of 0 give the hard-edged door; feathers > 0 fade the
# world into the occluder over that many dva. Visibility is a product
#   v(x, y) = vx(x) * vy(y)
# of 1-D falloffs from each edge, so feather_y = 0 keeps the top and bottom
# hard while the lookahead edge is soft.
#
#   inset    where the falloff sits: 0 = it starts at the nominal edge (all
#            of the box fully visible), 0.5 = centred on it, 1 = it ends
#            there (nothing beyond the box)
#   profile  smoothstep | gaussian (feather = sigma; never quite 0) | linear
#
# The shader source below is the reference for runner/door's spotlight
# aperture: the uniform names match its stimdg columns
# (door_lead door_trail door_halfh spot_feather_x spot_feather_y spot_inset
# spot_profile). Keep the two copies in step.
#
# No physics here: the avatar does not jump, the world just scrolls.
#
# REQUIRES
#   polygon
#   metagroup
#   shaders
#   text

# ============================================================
# SHADER CODE
# ============================================================

set ::spotlight_vertex {
in vec3 vertex_position;

out vec2 pos;   // eye space: dva from the screen centre

uniform mat4 projMat;
uniform mat4 modelviewMat;

void main(void)
{
    vec4 p = modelviewMat * vec4(vertex_position, 1.0);
    pos = p.xy;
    gl_Position = projMat * p;
}
}

set ::spotlight_fragment {
uniform float lead;       // window reach ahead of the avatar (dva)
uniform float trail;      // reach behind (dva)
uniform float halfh;      // half-height (dva)
uniform float featherX;   // horizontal falloff width (dva), 0 = hard
uniform float featherY;   // vertical falloff width (dva), 0 = hard
uniform float inset;      // 0 falloff outside the edge .. 1 inside it
uniform int   profile;    // 0 smoothstep, 1 gaussian, 2 linear
uniform float side;       // +1 travel to the right, -1 to the left
uniform vec4  occColor;   // occluder colour; alpha scales the whole mask

in vec2 pos;
out vec4 fragcolor;

// visibility at distance d beyond an edge (d <= 0 is inside)
float falloff(float d, float w)
{
    d += inset * w;
    if (d <= 0.0) return 1.0;
    if (w <= 0.0) return 0.0;
    if (profile == 1) return exp(-0.5 * d * d / (w * w));
    if (profile == 2) return clamp(1.0 - d / w, 0.0, 1.0);
    return 1.0 - smoothstep(0.0, w, d);
}

void main(void)
{
    float x  = pos.x * side;
    float dx = max(x - lead, -trail - x);
    float dy = abs(pos.y) - halfh;
    float v  = falloff(dx, featherX) * falloff(dy, featherY);
    fragcolor = vec4(occColor.rgb, occColor.a * (1.0 - v));
}
}

# ============================================================
# STATE
# ============================================================

namespace eval spotdemo {
    # Door Runner palette (door_stim.tcl)
    variable col [dict create bg {28 31 34} plat {93 100 107} plattop {182 190 198} \
                      occ {55 60 65} avatar {223 230 236} fix {224 65 59} guide {242 179 61}]
    variable P 120.0          ;# world period (dva): the scene repeats seamlessly
    variable AW 0.9; variable AH 1.2
    variable seed 1
    variable side 1
    variable speed 6.0
    variable scrolling 1
    variable guides 0
    variable s 0.0            ;# scroll distance (dva), mod P
    variable world ""; variable spot ""; variable guide ""; variable info ""
    variable profiles {smoothstep gaussian linear}
}

proc spotdemo::rgb { name } {
    variable col
    lmap c [dict get $col $name] { expr {$c / 255.0} }
}

proc spotdemo::rect { x0 x1 y0 y1 colname } {
    set p [polygon]
    scaleObj $p [expr {abs($x1 - $x0)}] [expr {abs($y1 - $y0)}]
    translateObj $p [expr {($x0 + $x1) / 2.0}] [expr {($y0 + $y1) / 2.0}]
    polycolor $p {*}[rgb $colname]
    return $p
}

# a platform (body + bright top edge) into metagroup mg, built for travel
# to the right; the world group is mirrored (scaleObj -1 1) for the left
proc spotdemo::platform { mg x0 x1 h } {
    metagroupAdd $mg [rect $x0 $x1 [expr {$h - 40.0}] $h plat]
    set top [rect $x0 $x1 [expr {$h - 0.1}] $h plattop]
    priorityObj $top 1
    metagroupAdd $mg $top
}

# runner-like layout over one period: ground runs broken by gaps, raised
# blocks and level steps; drawn three times (-P, 0, +P) so scrolling mod P
# never shows an edge
proc spotdemo::build_world {} {
    variable seed; variable P
    expr {srand($seed)}
    set segs {}; set blocks {}
    set x 0.0; set h 0.0
    while { $x < $P } {
        set x1 [expr {min($P, $x + 3.0 + 5.0 * rand())}]
        lappend segs [list $x $x1 $h]
        set x $x1
        if { $x >= $P } break
        set r [expr {rand()}]
        if { $r < 0.45 } {
            set x [expr {$x + 1.2 + 1.6 * rand()}]                ;# gap
        } elseif { $r < 0.75 } {
            set w [expr {0.8 + 0.8 * rand()}]                     ;# block
            set x1 [expr {min($P, $x + 2.0 + 2.0 * rand())}]
            lappend segs [list $x $x1 $h]
            set bx [expr {$x + 0.5 * ($x1 - $x - $w)}]
            lappend blocks [list $bx [expr {$bx + $w}] [expr {$h + 0.6 + 0.8 * rand()}]]
            set x $x1
        } else {
            set h [expr {max(-0.8, min(1.6, $h + (rand() < 0.5 ? -0.8 : 0.8)))}]  ;# step
        }
    }
    set mg [metagroup]
    objName $mg spot_world
    foreach k {-1 0 1} {
        set o [expr {$k * $P}]
        foreach sg [concat $segs $blocks] {
            lassign $sg a b hh
            platform $mg [expr {$a + $o}] [expr {$b + $o}] $hh
        }
    }
    addPreScript $mg spotdemo::frame
    return $mg
}

proc spotdemo::frame {} {
    variable scrolling; variable speed; variable s; variable P; variable side; variable world; variable AH
    if { $scrolling } {
        set dt [expr {min($::StimDeltaTimeF, 100.0) / 1000.0}]
        set s [expr {fmod($s + $speed * $dt, $P)}]
    }
    translateObj $world [expr {-$side * $s}] [expr {-$AH / 2.0}]
}

# the nominal box (inset/feather ignored): where a door would put its edges
proc spotdemo::update_guide {} {
    variable spot; variable guide; variable side
    set lead [shaderObjSetUniform $spot lead]
    set trail [shaderObjSetUniform $spot trail]
    set hh [shaderObjSetUniform $spot halfh]
    if { $side > 0 } { set l [expr {-$trail}]; set r $lead } else { set l [expr {-$lead}]; set r $trail }
    polyverts $guide [dl_flist $l $r $r $l] [dl_flist -$hh -$hh $hh $hh]
}

proc spotdemo::update_info {} {
    variable spot; variable speed; variable info; variable profiles
    set lead [shaderObjSetUniform $spot lead]
    set ms [expr {$speed > 0 ? round(1000.0 * $lead / $speed) : 0}]
    set prof [lindex $profiles [shaderObjSetUniform $spot profile]]
    textString $info [format "lead %.1f dva = %d ms at %.1f dva/s   feather x %.2f  y %.2f  inset %.2f  %s" \
        $lead $ms $speed [shaderObjSetUniform $spot featherX] [shaderObjSetUniform $spot featherY] \
        [shaderObjSetUniform $spot inset] $prof]
}

proc spotdemo::preset { name } {
    variable spot
    switch -- $name {
        door { set u {featherX 0.0 featherY 0.0 inset 0.0 profile 0} }
        soft { set u {featherX 1.5 featherY 0.0 inset 0.0 profile 0} }
        glow { set u {featherX 2.5 featherY 1.0 inset 0.5 profile 1} }
    }
    dict for {k v} $u { shaderObjSetUniform $spot $k $v }
}

# ============================================================
# SETUP
# ============================================================

proc spotlight_setup { seed preset } {
    glistInit 1
    resetObjList
    shaderDeleteAll
    textFont sans NotoSans-Regular.ttf

    set spotdemo::seed $seed
    set spotdemo::s 0.0
    setBackground {*}[dict get $spotdemo::col bg]

    if { [catch { set hx [screen_set HalfScreenDegreeX]; set hy [screen_set HalfScreenDegreeY] }] } {
        set hx 30.0; set hy 20.0
    }

    set spotdemo::world [spotdemo::build_world]
    scaleObj $spotdemo::world $spotdemo::side 1
    glistAddObject $spotdemo::world 0

    # the occluder: one quad covering the screen, priority above the world
    # (uniform defaults: one "name value" per line)
    set shader [shaderBuildInline $::spotlight_vertex $::spotlight_fragment {
        lead 3.0
        trail 2.2
        halfh 4.6
        featherX 1.5
        featherY 0.0
        inset 0.0
        profile 0
        side 1.0
        occColor "0.216 0.235 0.255 1.0"
    }]
    set spot [shaderObj $shader]
    objName $spot spotlight
    scaleObj $spot [expr {2.0 * $hx + 4.0}] [expr {2.0 * $hy + 4.0}]
    shaderObjSetUniform $spot side $spotdemo::side
    priorityObj $spot 10
    set spotdemo::spot $spot
    spotdemo::preset $preset
    glistAddObject $spot 0

    set g [polygon]
    objName $g spot_guide
    polytype $g line_loop
    polycolor $g {*}[spotdemo::rgb guide]
    priorityObj $g 11
    setVisible $g $spotdemo::guides
    set spotdemo::guide $g
    spotdemo::update_guide
    glistAddObject $g 0

    set av [metagroup]
    set aw $spotdemo::AW; set ah $spotdemo::AH
    metagroupAdd $av [spotdemo::rect [expr {-$aw / 2}] [expr {$aw / 2}] [expr {-$ah / 2}] [expr {$ah / 2}] avatar]
    set fs [polygon]
    polycirc $fs 1
    scaleObj $fs 0.3 0.3
    polycolor $fs {*}[spotdemo::rgb fix]
    priorityObj $fs 1
    metagroupAdd $av $fs
    priorityObj $av 20
    glistAddObject $av 0

    set t [text " " -font sans -size 0.5]
    textColor $t 0.75 0.75 0.75 1.0
    textJustify $t center
    translateObj $t 0 [expr {$hy - 1.2}]
    priorityObj $t 30
    set spotdemo::info $t
    spotdemo::update_info
    glistAddObject $t 0

    glistSetDynamic 0 1
    glistSetCurGroup 0
    glistSetVisible 1
    redraw
}

# ============================================================
# ADJUSTER PROCS
# ============================================================

proc spot_set_shape { target lead trail halfh } {
    shaderObjSetUniform $target lead $lead
    shaderObjSetUniform $target trail $trail
    shaderObjSetUniform $target halfh $halfh
    spotdemo::update_guide
    spotdemo::update_info
    redraw
    return
}

proc spot_get_shape { {target {}} } {
    dict create lead [shaderObjSetUniform $target lead] \
        trail [shaderObjSetUniform $target trail] \
        halfh [shaderObjSetUniform $target halfh]
}

proc spot_set_falloff { target feather_x feather_y inset profile } {
    shaderObjSetUniform $target featherX $feather_x
    shaderObjSetUniform $target featherY $feather_y
    shaderObjSetUniform $target inset $inset
    shaderObjSetUniform $target profile [lsearch $spotdemo::profiles $profile]
    spotdemo::update_info
    redraw
    return
}

proc spot_get_falloff { {target {}} } {
    dict create feather_x [shaderObjSetUniform $target featherX] \
        feather_y [shaderObjSetUniform $target featherY] \
        inset [shaderObjSetUniform $target inset] \
        profile [lindex $spotdemo::profiles [shaderObjSetUniform $target profile]]
}

proc spot_set_color { target r g b a } {
    shaderObjSetUniform $target occColor [list $r $g $b $a]
    redraw
    return
}

proc spot_get_color { {target {}} } {
    lassign [shaderObjSetUniform $target occColor] r g b a
    dict create r $r g $g b $b a $a
}

proc spot_set_scroll { target speed scrolling direction guides } {
    set spotdemo::speed $speed
    set spotdemo::scrolling $scrolling
    set side [expr {$direction eq "left" ? -1 : 1}]
    set spotdemo::side $side
    scaleObj $target $side 1
    shaderObjSetUniform $spotdemo::spot side $side
    set spotdemo::guides $guides
    setVisible $spotdemo::guide $guides
    spotdemo::update_guide
    spotdemo::update_info
    redraw
    return
}

proc spot_get_scroll { {target {}} } {
    dict create speed $spotdemo::speed scrolling $spotdemo::scrolling \
        direction [expr {$spotdemo::side < 0 ? "left" : "right"}] \
        guides $spotdemo::guides
}

proc spot_action { target action } {
    switch -- $action {
        door - soft - glow { spotdemo::preset $action }
        match_bg { shaderObjSetUniform $target occColor [list {*}[spotdemo::rgb bg] 1.0] }
        door_occ { shaderObjSetUniform $target occColor [list {*}[spotdemo::rgb occ] 1.0] }
    }
    spotdemo::update_info
    redraw
    return
}

# ============================================================
# WORKSPACE INTERFACE
# ============================================================
workspace::reset

workspace::setup spotlight_setup {
    seed   {int 1 999 1 1 "World seed"}
    preset {choice {soft door glow} soft "Start as"}
} -adjusters {spot_shape spot_falloff spot_color spot_scroll spot_actions} \
  -label "Spotlight occluder"

workspace::variant spotlight_door {seed 1 preset door} -label "Door (hard edges)"
workspace::variant spotlight_glow {seed 1 preset glow} -label "Gaussian glow"

workspace::adjuster spot_shape {
    lead  {float 0.0 15.0 0.1 3.0 "Lead" dva}
    trail {float 0.0 15.0 0.1 2.2 "Trail" dva}
    halfh {float 0.5 12.0 0.1 4.6 "Half-height" dva}
} -target spotlight -proc spot_set_shape -getter spot_get_shape -label "Window"

workspace::adjuster spot_falloff {
    feather_x {float 0.0 8.0 0.05 1.5 "Feather X" dva}
    feather_y {float 0.0 8.0 0.05 0.0 "Feather Y" dva}
    inset     {float 0.0 1.0 0.05 0.0 "Inset (0 out, 1 in)"}
    profile   {choice {smoothstep gaussian linear} smoothstep "Profile"}
} -target spotlight -proc spot_set_falloff -getter spot_get_falloff -label "Falloff"

workspace::adjuster spot_color {
    r {float 0 1 0.01 0.216 "R"}
    g {float 0 1 0.01 0.235 "G"}
    b {float 0 1 0.01 0.255 "B"}
    a {float 0 1 0.01 1.0 "Opacity"}
} -target spotlight -proc spot_set_color -getter spot_get_color -label "Occluder" -colorpicker

workspace::adjuster spot_scroll {
    speed     {float 0.0 15.0 0.5 6.0 "Speed" dva/s}
    scrolling {bool 1 "Scroll"}
    direction {choice {right left} right "Direction"}
    guides    {bool 0 "Nominal edges"}
} -target spot_world -proc spot_set_scroll -getter spot_get_scroll -label "Scene"

workspace::adjuster spot_actions {
    door     {action "Door"}
    soft     {action "Soft"}
    glow     {action "Glow"}
    match_bg {action "Occluder = background"}
    door_occ {action "Occluder = door grey"}
} -target spotlight -proc spot_action -label "Presets"
