#version 110

// Flat, semi-transparent shadow colour. Alpha comes from the uniform so the
// darkness can be tuned without recompiling. A stencil pass ensures each pixel
// is blended only once, so overlapping projected triangles stay uniform.

uniform vec4 shadow_color;

void main()
{
    gl_FragColor = shadow_color;
}
