#version 110
uniform vec3 u_base_color;
void main()
{
    gl_FragColor = vec4(u_base_color, 1.0);
}