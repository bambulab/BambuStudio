#version 140

uniform mat4 u_model_matrix;
uniform mat4 u_view_projection_matrix;

in vec3 v_position;

void main()
{
    gl_Position = u_view_projection_matrix * u_model_matrix * vec4(v_position, 1.0);
}
