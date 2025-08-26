#version 140

in vec3 v_position;
in vec3 v_normal;

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;

uniform samplerBuffer s_position_texture;
uniform samplerBuffer s_width_height_data_texture;
uniform samplerBuffer s_segment_texture;

out float moveId;
out vec3 frag_normal;
out vec3 frag_pos;

void main()
{
    vec4 seg_startEnd_hasPrev = texelFetch(s_segment_texture, gl_InstanceID);
    int end_index = int(seg_startEnd_hasPrev.y);

    vec4 endPos_mid = texelFetch(s_position_texture, end_index);

    moveId = endPos_mid.w + 0.5;

    vec2 width_height = texelFetch(s_width_height_data_texture, int(moveId)).rg;
    float width = 1.5 * width_height.x;
    float height = width_height.y;

    mat4 model_matrix = mat4(
    width, 0.0,   0.0, 0.0,
    0.0,   width, 0.0, 0.0,
    0.0,   0.0,   1.5 * height, 0.0,
    endPos_mid.x,   endPos_mid.y,   endPos_mid.z - 0.5 * height,  1.0);
    vec4 final_position = view_model_matrix * model_matrix * vec4(v_position, 1.0);
    frag_pos = final_position.xyz;
    frag_normal = normalize(v_normal);
    gl_Position = projection_matrix * final_position;
}
