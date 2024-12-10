#version 110

attribute vec3 v_position;
attribute vec3 v_normal;

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 normal_matrix;

varying vec3 eye_normal;

void main()
{
    gl_Position = projection_matrix * view_model_matrix * vec4(v_position, 1.0);
    eye_normal = normal_matrix * v_normal;
}
