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

uniform sampler2D tex0;

uniform float center_x;
uniform float center_y;
uniform float radius;

in vec2 texcoord;
out vec4 fragcolor;

void main(void) {
  float dx = (texcoord.s-.5)-center_x;
  float dy = (texcoord.t-.5)-center_y;
  float d2 = dx*dx+dy*dy;

  if (d2 > radius*radius) discard;

  vec4 texColor0 = texture(tex0, vec2(texcoord.s, 1.0-texcoord.t));
  fragcolor = texColor0;
}

-- Uniforms

center_x 0.0
center_y 0.0
radius 1.0

