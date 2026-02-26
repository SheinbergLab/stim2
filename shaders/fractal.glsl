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
// fractal.glsl - Multi-type fractal generator for visual stimuli
//
// fractal_type: 0=Julia, 1=Mandelbrot, 2=Burning Ship, 3=Tricorn, 4=Newton
//
// For stimulus generation, varying seed + fractal_type gives maximum
// novelty while keeping spatial frequency content controllable via
// max_iter and zoom.
//

in vec2 texcoord;
out vec4 fragcolor;

uniform float time;
uniform vec2  resolution;

uniform int   fractal_type;  // 0-4
uniform vec2  seed;          // c parameter (Julia) or center offset
uniform int   max_iter;      // controls detail/complexity
uniform float zoom;
uniform vec2  center;
uniform int   colormap;      // 0=rainbow, 1=hot, 2=cool, 3=grayscale
uniform float brightness;
uniform float aspect;
uniform float power;         // exponent for z^power + c (default 2)
uniform float envelope_sigma; // Gaussian envelope sigma (0=none)

vec3 palette(float t, int cmap)
{
  if (cmap == 1) {
    return vec3(
      clamp(3.0 * t, 0.0, 1.0),
      clamp(3.0 * t - 1.0, 0.0, 1.0),
      clamp(3.0 * t - 2.0, 0.0, 1.0)
    );
  }
  if (cmap == 2) {
    return vec3(
      clamp(2.0 * t - 1.0, 0.0, 1.0),
      clamp(2.0 * t - 0.5, 0.0, 1.0),
      clamp(1.5 * t, 0.0, 1.0)
    );
  }
  if (cmap == 3) {
    return vec3(t);
  }
  vec3 a = vec3(0.5, 0.5, 0.5);
  vec3 b = vec3(0.5, 0.5, 0.5);
  vec3 c = vec3(1.0, 1.0, 1.0);
  vec3 d = vec3(0.0, 0.33, 0.67);
  return a + b * cos(6.28318 * (c * t + d));
}

// Complex multiply
vec2 cmul(vec2 a, vec2 b) {
  return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

// Complex divide
vec2 cdiv(vec2 a, vec2 b) {
  float d = dot(b, b);
  return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / d;
}

// z^n for integer power via repeated squaring
vec2 cpow(vec2 z, int n) {
  vec2 result = vec2(1.0, 0.0);
  vec2 base = z;
  int p = n;
  if (p < 0) p = -p;
  for (int i = 0; i < 16; i++) {
    if (p <= 0) break;
    if (p - 2 * (p / 2) == 1) {
      result = cmul(result, base);
    }
    base = cmul(base, base);
    p = p / 2;
  }
  return result;
}

void main(void)
{
  float asp = aspect;
  if (asp <= 0.0) asp = resolution.x / resolution.y;
  
  vec2 uv = (texcoord - 0.5) * 2.0;
  uv.x *= asp;
  uv = uv / zoom + center;
  
  int ipow = int(power);
  if (ipow < 2) ipow = 2;
  
  vec2 z, c;
  
  // Setup based on fractal type
  if (fractal_type == 0) {
    // Julia: z starts at pixel, c is seed parameter
    z = uv;
    c = seed;
  } else {
    // Mandelbrot/Burning Ship/Tricorn: z starts at 0, c is pixel
    z = vec2(0.0);
    c = uv;
  }
  
  int i;
  for (i = 0; i < max_iter; i++) {
    if (dot(z, z) > 256.0) break;
    
    if (fractal_type == 2) {
      // Burning Ship: take abs of components before squaring
      z = abs(z);
    }
    if (fractal_type == 3) {
      // Tricorn: conjugate z
      z.y = -z.y;
    }
    
    if (fractal_type == 4) {
      // Newton's method for z^3 - 1 = 0
      // z_new = z - (z^3 - 1) / (3*z^2)
      vec2 z2 = cmul(z, z);
      vec2 z3 = cmul(z2, z);
      vec2 num = z3 - vec2(1.0, 0.0);
      vec2 den = 3.0 * z2;
      if (dot(den, den) < 1.0e-10) break;
      z = z - cdiv(num, den);
      // Check convergence to one of three roots
      vec2 r1 = vec2(1.0, 0.0);
      vec2 r2 = vec2(-0.5, 0.866025);
      vec2 r3 = vec2(-0.5, -0.866025);
      float d1 = length(z - r1);
      float d2 = length(z - r2);
      float d3 = length(z - r3);
      if (min(d1, min(d2, d3)) < 1.0e-4) break;
    } else {
      // Standard iteration: z = z^power + c
      if (ipow == 2) {
        z = cmul(z, z) + c;
      } else {
        z = cpow(z, ipow) + c;
      }
    }
  }
  
  vec3 col;
  
  if (fractal_type == 4) {
    // Newton: color by which root was reached
    vec2 r1 = vec2(1.0, 0.0);
    vec2 r2 = vec2(-0.5, 0.866025);
    vec2 r3 = vec2(-0.5, -0.866025);
    float d1 = length(z - r1);
    float d2 = length(z - r2);
    float d3 = length(z - r3);
    float shade = 1.0 - float(i) / float(max_iter);
    shade = clamp(shade * brightness, 0.0, 1.0);
    if (d1 < d2 && d1 < d3) {
      col = vec3(shade, 0.2 * shade, 0.2 * shade);
    } else if (d2 < d3) {
      col = vec3(0.2 * shade, shade, 0.2 * shade);
    } else {
      col = vec3(0.2 * shade, 0.2 * shade, shade);
    }
  } else if (i >= max_iter) {
    col = vec3(0.0);
  } else {
    float sl = float(i) - log2(log2(dot(z, z))) + 4.0;
    float t = clamp(sl / float(max_iter) * brightness, 0.0, 1.0);
    col = palette(t, colormap);
  }
  
  // Optional Gaussian envelope (useful for controlled stimulus presentation)
  float alpha = 1.0;
  if (envelope_sigma > 0.0) {
    vec2 d = texcoord - 0.5;
    float r2 = dot(d, d);
    alpha = exp(-r2 / (2.0 * envelope_sigma * envelope_sigma));
  }
  
  fragcolor = vec4(col, alpha);
}

-- Uniforms

fractal_type 0
seed {-0.7 0.27015}
max_iter 200
zoom 1.0
center {0.0 0.0}
colormap 0
brightness 1.0
aspect 0.0
power 2.0
envelope_sigma 0.0
