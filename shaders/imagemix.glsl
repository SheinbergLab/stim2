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

uniform float MixProp;
uniform sampler2D tex0;
uniform sampler2D tex1;

in vec2 texcoord;
out vec4 fragcolor;

vec2 rand(vec2 p){
   return fract(pow(p + 2.0, p.yx + 2.0) * 22222.0);
}


vec2 rand2(vec2 p){
   return rand(rand(p));
}


void main(void){
    vec3 texColor0 = texture(tex0, vec2(texcoord.s, 1.0-texcoord.t)).rgb;
    vec3 texColor1 = texture(tex1, vec2(texcoord.s, 1.0-texcoord.t)).rgb;
    float alpha0 = texture(tex0, vec2(texcoord.s, 1.0-texcoord.t)).a;
    vec2 p = (gl_FragCoord.xy * 2.0 - resolution.xy) / min(resolution.x, resolution.y);
    fragcolor = vec4(mix(texColor0, texColor1, MixProp), alpha0*rand2(p - (sin(time) * 0.001)).x);
}


-- Uniforms

MixProp 0.5