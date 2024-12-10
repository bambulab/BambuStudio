#version 140
uniform vec4 u_base_color;

out vec4 frag_color;
void main()
{
    frag_color = u_base_color;
}