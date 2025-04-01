#version 110

attribute vec3 v_position;
attribute vec2 v_tex_coord;

varying vec2 tex_coords;

void main()
{
    tex_coords = v_tex_coord;
    gl_Position = vec4(v_position, 1.0);
}