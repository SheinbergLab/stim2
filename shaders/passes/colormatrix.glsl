-- Fragment
// A post pass (shaderpass, docs/shaderpass.md): each output channel is a
// weighted sum of the scene's r, g, b plus an offset.
//   identity     red "1 0 0 0"   green "0 1 0 0"   blue "0 0 1 0"
//   swap r, b    red "0 0 1 0"   green "0 1 0 0"   blue "1 0 0 0"
//   invert       red "-1 0 0 1"  green "0 -1 0 1"  blue "0 0 -1 1"
//   grey         all three "0.2126 0.7152 0.0722 0"
uniform sampler2D scene;
uniform vec4 red;
uniform vec4 green;
uniform vec4 blue;
out vec4 frag;
void main() {
  vec4 s = texelFetch(scene, ivec2(gl_FragCoord.xy), 0);
  vec4 c = vec4(s.rgb, 1.0);
  frag = vec4(clamp(vec3(dot(red, c), dot(green, c), dot(blue, c)), 0.0, 1.0), s.a);
}

-- Uniforms
red   "1.0 0.0 0.0 0.0"
green "0.0 1.0 0.0 0.0"
blue  "0.0 0.0 1.0 0.0"
