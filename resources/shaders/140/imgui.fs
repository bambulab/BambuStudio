#version 140

uniform sampler2D s_texture;

in vec2 Frag_UV;
in vec4 color;

out vec4 frag_color;
void main()
{
    frag_color = color * texture(s_texture, Frag_UV.st);
}