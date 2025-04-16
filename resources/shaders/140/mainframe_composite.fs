#version 140

uniform sampler2D u_sampler;

in vec2 tex_coords;

out vec4 frag_color;

void main()
{
    frag_color = texture(u_sampler, tex_coords);
}