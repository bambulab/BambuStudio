#version 140

uniform sampler2D Texture;

in vec2 Frag_UV;
in vec4 color;

out vec4 frag_color;
void main()
{
    frag_color = color * texture(Texture, Frag_UV.st);
}