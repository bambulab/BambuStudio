#version 110

uniform sampler2D u_sampler;

varying vec2 tex_coords;

void main()
{
    gl_FragColor = texture2D(u_sampler, tex_coords);
}