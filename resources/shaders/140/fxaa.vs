#version 140

in vec3 v_position;
in vec2 v_tex_coord;

out vec2 tex_coords;

void main()
{
    tex_coords = v_tex_coord;
    gl_Position = vec4(v_position, 1.0);
}