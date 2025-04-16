#version 140
uniform vec3 u_base_color;

out vec4 frag_color;
void main()
{
    frag_color = vec4(u_base_color, 1.0);
}