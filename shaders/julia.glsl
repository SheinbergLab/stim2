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

in vec2 texcoord;
out vec4 fragcolor;

uniform float time;
uniform vec2  resolution;

uniform vec2  c_param;      // Julia constant, e.g. (-0.7, 0.27015)
uniform int   max_iter;     // iteration cap (50-500)
uniform float zoom;         // zoom level (1.0 = full view)
uniform vec2  center;       // pan offset
uniform int   colormap;     // 0=smooth rainbow, 1=hot, 2=cool, 3=grayscale
uniform float aspect;       // aspect ratio correction (0=auto from resolution)
uniform float brightness;   // overall brightness (0.0-2.0)
uniform int   animate;      // if 1, slowly vary c_param with time

// Attempt a smooth palette using cosine interpolation
// Based on Inigo Quilez palette technique
vec3 palette(float t, int cmap)
{
  if (cmap == 1) {
    // Hot: black -> red -> yellow -> white
    return vec3(
      clamp(3.0 * t, 0.0, 1.0),
      clamp(3.0 * t - 1.0, 0.0, 1.0),
      clamp(3.0 * t - 2.0, 0.0, 1.0)
    );
  }
  if (cmap == 2) {
    // Cool: dark blue -> cyan -> white
    return vec3(
      clamp(2.0 * t - 1.0, 0.0, 1.0),
      clamp(2.0 * t - 0.5, 0.0, 1.0),
      clamp(1.5 * t, 0.0, 1.0)
    );
  }
  if (cmap == 3) {
    // Grayscale
    return vec3(t);
  }
  // Default: smooth rainbow via cosine palette
  vec3 a = vec3(0.5, 0.5, 0.5);
  vec3 b = vec3(0.5, 0.5, 0.5);
  vec3 c = vec3(1.0, 1.0, 1.0);
  vec3 d = vec3(0.0, 0.33, 0.67);
  return a + b * cos(6.28318 * (c * t + d));
}

void main(void)
{
  // Map texcoord to complex plane
  float asp = aspect;
  if (asp <= 0.0) asp = resolution.x / resolution.y;
  
  vec2 uv = (texcoord - 0.5) * 2.0;  // -1 to 1
  uv.x *= asp;
  uv = uv / zoom + center;
  
  // Optionally animate the c parameter
  vec2 c = c_param;
  if (animate == 1) {
    c = vec2(
      c_param.x + 0.05 * sin(time * 0.3),
      c_param.y + 0.05 * cos(time * 0.4)
    );
  }
  
  // Julia iteration
  vec2 z = uv;
  int i;
  for (i = 0; i < max_iter; i++) {
    float x = z.x * z.x - z.y * z.y + c.x;
    float y = 2.0 * z.x * z.y + c.y;
    z = vec2(x, y);
    if (dot(z, z) > 256.0) break;  // large bailout for smooth coloring
  }
  
  // Coloring
  if (i >= max_iter) {
    // Interior: black (in the set)
    fragcolor = vec4(0.0, 0.0, 0.0, 1.0);
  } else {
    // Smooth iteration count (avoids banding)
    float sl = float(i) - log2(log2(dot(z, z))) + 4.0;
    float t = sl / float(max_iter);
    t = clamp(t * brightness, 0.0, 1.0);
    vec3 col = palette(t, colormap);
    fragcolor = vec4(col, 1.0);
  }
}

-- Uniforms

c_param {-0.7 0.27015}
max_iter 200
zoom 1.0
center {0.0 0.0}
colormap 0
aspect 0.0
brightness 1.0
animate 0
