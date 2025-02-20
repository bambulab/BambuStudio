#version 110

uniform mat4 view_matrix;
uniform mat4 projection_matrix;

attribute vec3 v_position;
attribute vec2 v_undefine;
attribute mat4 instanceMatrix;
void main()
{
    gl_Position = projection_matrix * view_matrix * instanceMatrix  * vec4(v_position, 1.0);
}
