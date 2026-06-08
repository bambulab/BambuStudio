#version 140
uniform vec4 uniform_color;

out vec4 frag_color;
void main()
{
    frag_color = uniform_color;
}