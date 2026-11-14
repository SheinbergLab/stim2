# test_shader_fragment_only.tcl -- the shader module's default vertex shader
#
#   stim2 --headless -w 320 -h 180 --port-base 5610 -f /abs/path/to/tests/test_shader_fragment_only.tcl
#
# A shader file with no "-- Vertex" section, and a shaderBuildInline with
# an empty vertex source, are built with the standard vertex shader: the
# object's quad, placed by the projection and modelview matrices, with
# `texcoord` passed to the fragment shader.  A file with its own Vertex
# section still uses it.  Also the shader search path: the user's list
# (add, prepend, remove, clear, set) and the system directory of shipped
# shaders that survives all of them.  Stepped, into a manual frame sink.
# Prints OK/FAIL lines and exits non-zero on any failure.

set ::log {}
proc note { s } { lappend ::log $s }
proc ck { label cond } { note "[expr {$cond ? {OK  } : {FAIL}}] $label" }
proc finish {} {
    puts [join $::log \n]
    set nf [llength [lsearch -all $::log FAIL*]]
    puts "[expr {[llength $::log] - $nf}] ok, $nf FAIL"
    exit [expr {$nf > 0}]
}

set W [screen_set WinWidth]; set H [screen_set WinHeight]
screen_set ScreenWidthCm 32; screen_set ScreenHeightCm 18; screen_set DistanceToMonitor 57
screen_config
proc deg2px { xd yd } {
    list [expr {int(round($::W / 2.0 + $xd * $::W / (2.0 * [screen_set HalfScreenDegreeX])))}] \
         [expr {int(round($::H / 2.0 - $yd * $::H / (2.0 * [screen_set HalfScreenDegreeY])))}]
}

# three shader files in a directory of their own
set dir [file join [file dirname [info script]] shader_fragment_only_tmp]
file mkdir $dir
proc put { name text } { set f [open [file join $::dir $name.glsl] w]; puts $f $text; close $f }
# fragment only: red left of the quad's centre, green right (by texcoord)
put fragonly {-- Fragment
in vec2 texcoord;
uniform float level;
out vec4 frag;
void main() { frag = texcoord.x < 0.5 ? vec4(level, 0.0, 0.0, 1.0) : vec4(0.0, level, 0.0, 1.0); }

-- Uniforms
level 1.0}
# its own Vertex section, which mirrors the texture coordinate
put ownvertex {-- Vertex
in vec3 vertex_position;
in vec2 vertex_texcoord;
out vec2 texcoord;
uniform mat4 projMat;
uniform mat4 modelviewMat;
void main(void) {
  texcoord = vec2(1.0 - vertex_texcoord.x, vertex_texcoord.y);
  gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);
}

-- Fragment
in vec2 texcoord;
out vec4 frag;
void main() { frag = texcoord.x < 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0); }}
put badfrag "-- Fragment\nthis is not glsl;\n"

# --- the search path ---------------------------------------------------------
# The user's list, then the system directory (stim2's shipped shaders),
# which nothing done to the list removes.  Here the system directory is
# this checkout's shaders/, so the test does not depend on an install.
put checkers "-- Fragment\nout vec4 frag;\nvoid main() { frag = vec4(0.0, 0.0, 1.0, 1.0); }\n"
set shipped [file normalize [file join [file dirname [info script]] .. shaders]]
set sys0 [shaderSystemPath]
set user0 [shaderGetPaths -user]
ck "the module starts with a system directory" [expr {$sys0 ne ""}]
ck "  which shaderGetPaths lists last" [expr {[lindex [shaderGetPaths] end] eq $sys0}]
shaderSystemPath $shipped
shaderClearPaths
ck "shaderClearPaths: the user's list empty, the system directory still searched" \
    [expr {[shaderGetPaths -user] eq "" && [shaderGetPaths] eq [list $shipped]}]
ck "  a shipped shader still builds" [expr {![catch { shaderBuild checkers }] && [shaderGetResolvedPath] eq $shipped}]

set old [shaderSetPath $dir]
ck "shaderSetPath returns the directory that was searched first" [expr {$old eq $shipped}]
ck "  the user's list is that one directory" [expr {[shaderGetPaths -user] eq [list $dir]}]
ck "  the system directory still follows it" [expr {[shaderGetPaths] eq [list $dir $shipped]}]
ck "  with no argument it only reports" [expr {[shaderSetPath] eq $dir && [shaderGetPaths -user] eq [list $dir]}]
ck "a user directory overrides a shipped shader of the same name" \
    [expr {![catch { shaderBuild checkers }] && [shaderGetResolvedPath] eq $dir}]
ck "  and other shipped shaders are still found" \
    [expr {![catch { shaderBuild sineshader }] && [shaderGetResolvedPath] eq $shipped}]

shaderAddPath /tmp/a; shaderAddPath /tmp/b; shaderAddPath /tmp/a/
ck "shaderAddPath: a directory already listed is not added again" [expr {[shaderGetPaths -user] eq [list $dir /tmp/a /tmp/b]}]
shaderPrependPath /tmp/b
ck "shaderPrependPath: one already listed moves to the front" [expr {[shaderGetPaths -user] eq [list /tmp/b $dir /tmp/a]}]
ck "shaderRemovePath: 1 for one that was there" [expr {[shaderRemovePath /tmp/b/] == 1 && [shaderGetPaths -user] eq [list $dir /tmp/a]}]
ck "  0 for one that was not" [expr {[shaderRemovePath /tmp/zzz] == 0}]
ck "  the system directory is not in the list to remove" [expr {[shaderRemovePath $shipped] == 0 && [lindex [shaderGetPaths] end] eq $shipped}]
shaderRemovePath /tmp/a
shaderRemovePath $dir
ck "with the user directory removed, the shipped shader is found again" \
    [expr {![catch { shaderBuild checkers }] && [shaderGetResolvedPath] eq $shipped}]
shaderSetPath $dir

glistInit 1; resetObjList; setBackground 0 0 0
ck "a fragment-only file builds" [expr {![catch { set ::s1 [shaderBuild fragonly] } e]}]
ck "a file with its own Vertex section builds" [expr {![catch { set ::s2 [shaderBuild ownvertex] }]}]
ck "a fragment that does not compile is still refused" [catch { shaderBuild badfrag }]
ck "a file that is not there is still refused" [catch { shaderBuild nosuchshader }]
ck "inline, empty vertex source: builds" [expr {![catch {
    set ::s3 [shaderBuildInline "" {
        in vec2 texcoord; out vec4 frag;
        void main() { frag = vec4(0.0, 0.0, texcoord.x < 0.5 ? 1.0 : 0.5, 1.0); }
    }] }]}]

set o1 [shaderObj $s1]; scaleObj $o1 8 4
ck "the default vertex shader's matrices are found" \
    [expr {[lsearch [shaderObjUniformNames $o1] projMat] >= 0 && [lsearch [shaderObjUniformNames $o1] modelviewMat] >= 0}]
ck "the file's Uniforms default is set" [expr {[shaderObjSetUniform $o1 level] == 1.0}]
set o2 [shaderObj $s2]; scaleObj $o2 8 4
set o3 [shaderObj $s3]; scaleObj $o3 8 4
foreach o [list $o1 $o2 $o3] { glistAddObject $o 0; setVisible $o 0 }
glistSetCurGroup 0; glistSetVisible 1

set fsz [expr {$W * $H * 4}]
set fd [file tempfile path shaderfrag.rgba]; close $fd
frameSink open -manual $path
set nframes 0
proc keep {} { frameSink write 1; set n $::nframes; incr ::nframes; return $n }
stepMode 1 5000
setVisible $o1 1
stepFrame 5010; set f1 [keep]
translateObj $o1 5 0                          ;# the modelview matrix moves it
stepFrame 5020; set f1m [keep]
setVisible $o1 0; setVisible $o2 1
stepFrame 5030; set f2 [keep]
setVisible $o2 0; setVisible $o3 1
stepFrame 5040; set f3 [keep]
frameSink close
set fd [open $path rb]; set d [read $fd]; close $fd
file delete $path
stepMode 0
shaderClearPaths
foreach p $user0 { shaderAddPath $p }
shaderSystemPath $sys0
file delete -force $dir

proc px { f xd yd } {
    lassign [deg2px $xd $yd] x y
    binary scan $::d @[expr {$f * $::fsz + ($y * $::W + $x) * 4}]cu3 p
    return $p
}
proc is { f xd yd rgb } { expr {[px $f $xd $yd] eq $rgb} }

ck "sink: $nframes frames" [expr {[string length $d] == $nframes * $fsz}]
ck "fragment-only: the quad, 8 x 4 deg at the centre ([px $f1 -2 0] / [px $f1 2 0])" \
    [expr {[is $f1 -2 0 {255 0 0}] && [is $f1 2 0 {0 255 0}]}]
ck "  texcoord runs across it; outside is background" \
    [expr {[is $f1 -3.5 1.5 {255 0 0}] && [is $f1 3.5 -1.5 {0 255 0}] && [is $f1 -5 0 {0 0 0}] && [is $f1 0 3 {0 0 0}]}]
ck "  translateObj moves it ([px $f1m 3 0] / [px $f1m 7 0])" \
    [expr {[is $f1m 3 0 {255 0 0}] && [is $f1m 7 0 {0 255 0}] && [is $f1m -2 0 {0 0 0}]}]
ck "own Vertex section used: mirrored ([px $f2 -2 0] / [px $f2 2 0])" \
    [expr {[is $f2 -2 0 {0 255 0}] && [is $f2 2 0 {255 0 0}]}]
ck "inline with the default vertex shader ([px $f3 -2 0] / [px $f3 2 0])" \
    [expr {[is $f3 -2 0 {0 0 255}] && [lindex [px $f3 2 0] 2] in {127 128}}]
finish
