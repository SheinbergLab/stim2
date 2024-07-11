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

in vec2 texcoord;
out vec4 fragcolor;

vec2 rand(vec2 p){
   return fract(pow(p + 2.0, p.yx + 2.0) * 22222.0);
}


vec2 rand2(vec2 p){
   return rand(rand(p));
}


void main(void){
    vec3 texColor = texture(tex0, vec2(texcoord.s, 1.0-texcoord.t)).rgb;
    vec2 p = (gl_FragCoord.xy * 2.0 - resolution.xy) / min(resolution.x, resolution.y);
    fragcolor = vec4(texColor, rand2(p - (sin(time) * 0.001)).x);
}
