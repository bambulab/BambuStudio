#version 110

attribute vec3 v_position;
attribute vec2 v_tex_coord;

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 u_uvTransformMatrix;

varying vec2 v_texcoord;

void main()
{
    v_texcoord = (u_uvTransformMatrix * vec3(v_tex_coord, 1.0)).xy;
    gl_Position = projection_matrix * view_model_matrix * vec4(v_position, 1.0);
}