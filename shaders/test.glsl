-- Vertex

void main(void)
{
  gl_Position = ftransform();
}


-- Fragment

void main(void)
{
  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
