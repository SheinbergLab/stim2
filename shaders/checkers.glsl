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

uniform float time;
uniform vec2 resolution;

uniform float HoleSize;

in vec2 texcoord;
out vec4 fragcolor;


vec2 rand(vec2 p){
   return fract(pow(p + 2.0, p.yx + 2.0) * 22222.0);
}

vec2 rand2(vec2 p){
   return rand(rand(p));
}

void main(void){
   vec2 p = (gl_FragCoord.xy * 2.0 - resolution.xy) / min(resolution.x, resolution.y);

// s and t are x and y coordinates, stimulus is size 1 by 1
  float dx = texcoord.s-.5;
  float dy = texcoord.t-.5;

  // distance from the center
  float d = sqrt(dx*dx+dy*dy);

  // if the point is inside the hole, discard it
  if (d < HoleSize) discard;
  
  fragcolor = vec4(1, 1, 1, rand2(p - (sin(time) * 0.001)).x);
}

-- Uniforms

HoleSize 0.2
