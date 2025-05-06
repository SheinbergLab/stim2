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

#define MAX_CIRCLES 20
uniform float time;
uniform vec2 resolution;

uniform float radius;        // Radius of each circle
uniform float smoothness;    // Edge smoothness (0.0 = hard, higher = softer)
uniform vec4  maskColor;     // RGBA color of the mask

uniform int nCircles;
uniform float rotationAngle;
uniform vec2 circlePos[MAX_CIRCLES];

uniform sampler2D tex0;
in vec2 texcoord;
out vec4 fragcolor;

vec2 rotate(vec2 point, float angle) {
    float s = sin(radians(angle));
    float c = cos(radians(angle));

    return vec2(
        point.x * c - point.y * s,
        point.x * s + point.y * c
    );
}

float drawCircle(vec2 position, vec2 center, float radius, float smoothness) {
    float dist = distance(position, rotate(center, rotationAngle));

    // 1.0 at the center of the circle, 0.0 outside the circle
    return 1.0 - smoothstep(radius - smoothness, radius + smoothness, dist);
}

void main(void){
    vec2 position = vec2(texcoord.s-.5, texcoord.y-0.5);

    // Start with no holes (solid mask)
    float mask = 0.0;

    // Add each circle's contribution to the mask
    for (int i = 0; i < MAX_CIRCLES; i++) {
       if (i >= nCircles) break;
       mask = max(mask,
                  drawCircle(position, circlePos[i], radius, smoothness));
    }
    
    vec4 textureColor = texture(tex0, vec2(texcoord.s, 1.0-texcoord.t));

    fragcolor = mix(maskColor, textureColor, mask);
}

-- Uniforms

radius 0.1
smoothness .005
maskColor "0.6 0.1 0.9 1.0"
circlePos "-.1 0.0 0.0 .2"
nCircles 2
rotationAngle 0