#version 110

attribute vec3 v_position;

void main()
{
    gl_Position = vec4(v_position, 1.0);
}