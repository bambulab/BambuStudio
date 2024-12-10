#version 140

uniform mat4 view_matrix;
uniform mat4 projection_matrix;

in vec3 v_position;

// per instance data
// in mat4 instanceMatrix;
in vec4 i_data0;
in vec4 i_data1;
in vec4 i_data2;
in vec4 i_data3;
// end per instance data
void main()
{
    mat4 model_matrix = mat4(i_data0, i_data1, i_data2, i_data3);
    gl_Position = projection_matrix * view_matrix * model_matrix* vec4(v_position, 1.0);
}
