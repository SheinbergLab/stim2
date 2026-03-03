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
#ifdef GL_ES
precision mediump float;
#endif

in vec2 texcoord;
out vec4 fragcolor;

uniform float time;
uniform float NCycles;
uniform float CyclesPerSec;
uniform float Phase;
uniform float Contrast;
uniform float Sigma;         // Physical Sigma / Quad Size
uniform int Envelope;
uniform int Circular;

uniform float ColorR; uniform float ColorG; uniform float ColorB; uniform float ColorA;
uniform int InvertR;  uniform int InvertG;  uniform int InvertB;

void main(void)
{
    float TWO_PI = 6.283185307;
    vec2 uv = texcoord - 0.5;
    float d = length(uv);

    // 1. Spatial & Temporal Modulation
    float spatial_t = (Circular == 1) ? d : uv.y;
    float angle = (spatial_t * NCycles * TWO_PI) + Phase + (time * CyclesPerSec * TWO_PI);
    
    // sin() oscillates -1 to 1; we scale to 0 to 1
    float intensity = (sin(angle) * 0.5 * Contrast) + 0.5;
    intensity = clamp(intensity, 0.0, 1.0);

    // 2. Gaussian Envelope
    float alpha = 1.0;
    if (Envelope != 0) {
        // Standard: exp( -dist^2 / (2 * sigma^2) )
        // Using a small epsilon to prevent division by zero
        float s = max(Sigma, 0.001); 
        alpha = exp(-(d * d) / (2.0 * s * s));
    }

    // 3. Color & Inversion Logic
    float r = ColorR * mix(intensity, 1.0 - intensity, float(InvertR));
    float g = ColorG * mix(intensity, 1.0 - intensity, float(InvertG));
    float b = ColorB * mix(intensity, 1.0 - intensity, float(InvertB));

    fragcolor = vec4(r, g, b, ColorA * alpha);
}

-- Uniforms

NCycles 4
CyclesPerSec 4
Phase 0.0
Contrast 1
Sigma 0.2
Envelope 1
Circular 0
ColorR 1.
ColorG 1.
ColorB 1.
ColorA 1.
InvertR 0
InvertG 0
InvertB 0
