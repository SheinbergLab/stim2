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

uniform int   max_iter;
uniform float zoom;
uniform vec2  center;
uniform int   colormap;     // 0=rainbow, 1=hot, 2=cool, 3=grayscale, 4=orbit trap
uniform float aspect;
uniform float brightness;

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
  // Cosine palette
  vec3 a = vec3(0.5, 0.5, 0.5);
  vec3 b = vec3(0.5, 0.5, 0.5);
  vec3 c = vec3(1.0, 1.0, 1.0);
  vec3 d = vec3(0.0, 0.10, 0.20);
  return a + b * cos(6.28318 * (c * t + d));
}

void main(void)
{
  float asp = aspect;
  if (asp <= 0.0) asp = resolution.x / resolution.y;
  
  // Map to complex plane: default view covers roughly -2.5..1 x -1..1
  vec2 uv = (texcoord - 0.5) * 2.0;
  uv.x *= asp;
  uv = uv / zoom + center;
  
  vec2 c = uv;
  vec2 z = vec2(0.0);
  float min_dist = 1000.0;  // for orbit trap coloring
  
  int i;
  for (i = 0; i < max_iter; i++) {
    float x = z.x * z.x - z.y * z.y + c.x;
    float y = 2.0 * z.x * z.y + c.y;
    z = vec2(x, y);
    
    // Orbit trap: track min distance to origin
    if (colormap == 4) {
      min_dist = min(min_dist, length(z));
    }
    
    if (dot(z, z) > 256.0) break;
  }
  
  if (i >= max_iter) {
    fragcolor = vec4(0.0, 0.0, 0.0, 1.0);
  } else {
    float t;
    if (colormap == 4) {
      // Orbit trap coloring: uses minimum distance to origin
      t = clamp(min_dist * 0.5, 0.0, 1.0);
      vec3 col = vec3(t * 0.2, t * 0.5, t);
      fragcolor = vec4(col * brightness, 1.0);
    } else {
      float sl = float(i) - log2(log2(dot(z, z))) + 4.0;
      t = clamp(sl / float(max_iter) * brightness, 0.0, 1.0);
      vec3 col = palette(t, colormap);
      fragcolor = vec4(col, 1.0);
    }
  }
}

-- Uniforms

max_iter 200
zoom 0.8
center {-0.5 0.0}
colormap 0
aspect 0.0
brightness 1.2
