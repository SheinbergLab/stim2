-- Fragment
// A post pass (shaderpass, docs/shaderpass.md): uniform noise added to the
// scene, new every frame.
//   amount   peak deviation, 0..1 of full scale (0.1: +/- 10%)
//   grain    size of a noise element, pixels
//   mono     1: the same value on r, g and b (luminance noise); 0: each
//            channel its own
//   rate     noise fields per second; 0 freezes the field
// It declares `time`, so shaderpass repaints a static display every
// refresh.  `time` is the stim clock: a replay render of a frame draws the
// field the rig's frame had (given the same frame times).
uniform sampler2D scene;
uniform float time;
uniform float amount;
uniform float grain;
uniform float mono;
uniform float rate;
out vec4 frag;

// Dave Hoskins' hash without sine: the same on every GPU
vec3 hash33(vec3 p3) {
  p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yxz + 33.33);
  return fract((p3.xxy + p3.yxx) * p3.zyx);
}

void main() {
  vec4 s = texelFetch(scene, ivec2(gl_FragCoord.xy), 0);
  vec2 cell = floor(gl_FragCoord.xy / max(grain, 1.0));
  float field = rate > 0.0 ? floor(time * rate) : 0.0;
  vec3 n = hash33(vec3(cell, mod(field, 4096.0))) * 2.0 - 1.0;
  if (mono > 0.5) n = n.xxx;
  frag = vec4(clamp(s.rgb + amount * n, 0.0, 1.0), s.a);
}

-- Uniforms
amount "0.1"
grain  "1.0"
mono   "1.0"
rate   "60.0"
