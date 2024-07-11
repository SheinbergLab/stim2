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

uniform float NCycles;
uniform float CyclesPerSec;
uniform float Phase;
uniform float Contrast;
uniform float Sigma;
uniform int Envelope;
uniform int Circular;
uniform float ColorR;
uniform float ColorG;
uniform float ColorB;
uniform float ColorA;
uniform int InvertR;
uniform int InvertG;
uniform int InvertB;

void main(void)
{
  float r;
  float g;
  float b;
  float alpha = 1.0;
  float dx = texcoord.s-.5;
  float dy = texcoord.t-.5;
  float d = sqrt(dx*dx+dy*dy);
  float scaled_t;
  if (Circular == 1) scaled_t = 2. * d * NCycles;
  else scaled_t = 2. * dy * NCycles;
  float angle = scaled_t*6.283+Phase+(time*CyclesPerSec);
  float modulation = (sin(angle)/2.)*Contrast;
  float intensity = clamp(modulation+0.5,0.,1.);
  if (Envelope != 0) {
    float scale = .3989423/Sigma;
    float t1 = -1.*d*d;
      float t2 = 2.*Sigma*Sigma;
      alpha = exp(t1/t2);
  }
  if (InvertR == 1) r = ColorR-(ColorR*intensity);
  else r = ColorR*intensity;
  if (InvertG == 1) g = ColorG-(ColorG*intensity);
  else g = ColorG*intensity;
  if (InvertB == 1) b = ColorB-(ColorB*intensity);
  else b = ColorB*intensity;
  fragcolor = vec4 (r, g, b, ColorA*alpha);
}

-- Uniforms

NCycles 4
CyclesPerSec 4
Circular 1
Contrast 1
Sigma 0.2
Envelope 1
ColorR .3
ColorG .8
ColorB .5
ColorA 1.




