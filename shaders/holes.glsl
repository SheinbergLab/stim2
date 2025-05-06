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

uniform float radius;        // Radius of each circle
uniform float smoothness;    // Edge smoothness (0.0 = hard edge, higher = softer)
uniform vec4  maskColor;     // RGBA color of the mask

uniform sampler2D tex0;
uniform vec2 circlepos;

in vec2 texcoord;
out vec4 fragcolor;

float drawCircle(vec2 position, vec2 center, float radius, float smoothness) {
    float dist = distance(position, center);
    // 1.0 at the center of the circle, 0.0 outside the circle
    return 1.0 - smoothstep(radius - smoothness, radius + smoothness, dist);
}

void main(void){
    // Scale to pixel coordinates for circle calculations
    vec2 position = gl_FragCoord.xy;
    
    // Start with no holes (solid mask)
    float mask = 0.0;

    // Add this circle's contribution to the mask
    mask = max(mask, drawCircle(position, circlepos, radius, smoothness));

    vec4 textureColor = texture(tex0, vec2(texcoord.s, 1.0-texcoord.t));

    fragcolor = mix(maskColor, textureColor, mask);
}

-- Uniforms

radius 40
smoothness 2
maskColor "0.2 0.1 0.9 1.0"
circlepos "0.0 0.0"