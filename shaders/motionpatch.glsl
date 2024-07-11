-- Vertex

in vec3 vertex_position;

uniform mat4 projMat;
uniform mat4 modelviewMat;

void main(void)
{
    gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);
}


-- Fragment

out vec4 fragcolor;

void main(void)
{
  fragcolor = vec4 (1, 1, 1, 1);
}


