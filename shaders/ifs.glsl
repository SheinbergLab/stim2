-- Vertex

in vec3 vertex_position;
in vec2 vertex_texcoord;

out vec2 texcoord;

uniform mat4 projMat;
uniform mat4 modelviewMat;

void main(void)
{
  texcoord  = vertex_texcoord;
  gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);
}


-- Fragment

//
// ifs.glsl - IFS / Sierpinski / fractal noise patterns
//
// ifs_type:
//   0 = Sierpinski triangle
//   1 = Sierpinski carpet
//   2 = Cantor dust / grid
//   3 = Fractal noise (fBm - controllable complexity)
//
// For neuroscience use, type 3 (fBm) is particularly useful:
// varying 'octaves' smoothly changes fractal dimension / 1/f slope,
// giving you a parametric complexity axis for psychophysics.
//

in vec2 texcoord;
out vec4 fragcolor;

uniform float time;
uniform vec2  resolution;

uniform int   ifs_type;
uniform int   subdivisions;   // recursion depth for Sierpinski (4-8)
uniform int   octaves;        // fBm octaves (1-8), controls fractal dimension
uniform float lacunarity;     // fBm frequency multiplier (default 2.0)
uniform float gain;           // fBm amplitude multiplier (default 0.5)
uniform float noise_scale;    // spatial frequency scale
uniform float envelope_sigma;
uniform int   colormap;       // 0=grayscale, 1=hot, 2=cool
uniform float brightness;
uniform float contrast;
uniform float aspect;
uniform float fg_intensity;   // foreground brightness for IFS types (default 1.0)
uniform float bg_intensity;   // background brightness for IFS types (default 0.0)

// Hash function for noise
float hash(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

// 2D value noise
float noise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);  // smoothstep
  
  float a = hash(i);
  float b = hash(i + vec2(1.0, 0.0));
  float c = hash(i + vec2(0.0, 1.0));
  float d = hash(i + vec2(1.0, 1.0));
  
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Fractional Brownian motion
float fbm(vec2 p) {
  float value = 0.0;
  float amplitude = 0.5;
  float frequency = 1.0;
  
  for (int i = 0; i < 8; i++) {
    if (i >= octaves) break;
    value += amplitude * noise(p * frequency);
    frequency *= lacunarity;
    amplitude *= gain;
  }
  return value;
}

// Sierpinski triangle: at each level, scale by 2 and check
// if the point falls in the removed sub-triangle
float sierpinski_triangle(vec2 p, int depth) {
  for (int i = 0; i < 10; i++) {
    if (i >= depth) break;
    p *= 2.0;
    // In a 2x scaled triangle, the upper-right quadrant is the hole:
    // if both x and y are in the upper half, the point is removed
    if (p.x > 1.0 && p.y > 1.0) {
      return bg_intensity;
    }
    // Wrap back to [0,1]
    p = fract(p);
  }
  return fg_intensity;
}

// Sierpinski carpet: at each level, scale by 3 and remove center cell
float sierpinski_carpet(vec2 p, int depth) {
  for (int i = 0; i < 10; i++) {
    if (i >= depth) break;
    p *= 3.0;
    vec2 cell = floor(p);
    // Center cell (1,1) is the hole
    if (cell.x > 0.5 && cell.x < 1.5 && cell.y > 0.5 && cell.y < 1.5) {
      return bg_intensity;
    }
    p = fract(p);
  }
  return fg_intensity;
}

// Cantor dust: at each level, scale by 3 and keep only corner cells
float cantor_dust(vec2 p, int depth) {
  for (int i = 0; i < 10; i++) {
    if (i >= depth) break;
    p *= 3.0;
    vec2 cell = floor(p);
    // Remove if in center row OR center column
    if ((cell.x > 0.5 && cell.x < 1.5) || (cell.y > 0.5 && cell.y < 1.5)) {
      return bg_intensity;
    }
    p = fract(p);
  }
  return fg_intensity;
}

vec3 apply_colormap(float v, int cmap) {
  if (cmap == 1) {
    return vec3(
      clamp(3.0 * v, 0.0, 1.0),
      clamp(3.0 * v - 1.0, 0.0, 1.0),
      clamp(3.0 * v - 2.0, 0.0, 1.0)
    );
  }
  if (cmap == 2) {
    return vec3(
      clamp(2.0 * v - 1.0, 0.0, 1.0),
      clamp(2.0 * v - 0.5, 0.0, 1.0),
      clamp(1.5 * v, 0.0, 1.0)
    );
  }
  return vec3(v);
}

void main(void)
{
  // For IFS types, use texcoord directly in [0,1]
  // (the patterns tile in unit square coordinates)
  vec2 uv = texcoord;
  
  float value = 0.0;
  
  if (ifs_type == 0) {
    value = sierpinski_triangle(uv, subdivisions);
  } else if (ifs_type == 1) {
    value = sierpinski_carpet(uv, subdivisions);
  } else if (ifs_type == 2) {
    value = cantor_dust(uv, subdivisions);
  } else {
    // fBm noise - parametric complexity
    float asp = aspect;
    if (asp <= 0.0) asp = resolution.x / resolution.y;
    vec2 p = (texcoord - 0.5) * noise_scale;
    p.x *= asp;
    value = fbm(p);
    value = clamp((value - 0.5) * contrast + 0.5, 0.0, 1.0);
    value *= brightness;
  }
  
  vec3 col = apply_colormap(clamp(value, 0.0, 1.0), colormap);
  
  // Optional Gaussian envelope
  float alpha = 1.0;
  if (envelope_sigma > 0.0) {
    vec2 d = texcoord - 0.5;
    float r2 = dot(d, d);
    alpha = exp(-r2 / (2.0 * envelope_sigma * envelope_sigma));
  }
  
  fragcolor = vec4(col, alpha);
}


-- Uniforms

ifs_type 3
subdivisions 6
octaves 5
lacunarity 2.0
gain 0.5
noise_scale 8.0
envelope_sigma 0.0
colormap 0
brightness 1.0
contrast 1.5
aspect 0.0
fg_intensity 1.0
bg_intensity 0.0
