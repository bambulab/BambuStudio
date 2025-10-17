#version 140

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
    vec4 seg_startEnd_hasPrev_prevIndex = texelFetch(s_segment_texture, gl_InstanceID);
    int start_index = int(seg_startEnd_hasPrev_prevIndex.x);
    int end_index = int(seg_startEnd_hasPrev_prevIndex.y);
    bool hasPrev = seg_startEnd_hasPrev_prevIndex.z > 0.5;

    vec4 startPos_mid = texelFetch(s_position_texture, start_index);
    vec4 endPos_mid = texelFetch(s_position_texture, end_index);

    vec3 line = endPos_mid.xyz - startPos_mid.xyz;
    vec3 line_dir = vec3(1.0, 0.0, 0.0);
    float line_len = length(line);
    line_dir = line / max(line_len, 1e-6);

    vec3 right_dir = vec3(line_dir.y, -line_dir.x, 0.0);
    vec3 up = vec3(0.0, 0.0, 1.0);
    if (length(right_dir)  < 1e-4)
    {
        up = vec3(1.0, 0.0, 0.0);
        right_dir = cross(line_dir, up);
    }
    else
    {
        right_dir = normalize(right_dir);
        line_dir = normalize(line_dir);
        up = cross(right_dir, line_dir);
    }

    vec3 base_pos = gl_VertexID < 4 ? startPos_mid.xyz : endPos_mid.xyz;
    moveId = endPos_mid.w + 0.5;

    vec2 width_height = texelFetch(s_width_height_data_texture, int(moveId)).rg;
    float width = width_height.x;
    float height = width_height.y;

    float half_width = 0.5 * width;
    float half_height = 0.5 * height;
    vec3 d_up = half_height * up;
    vec3 d_right = half_width * right_dir;
    vec3 d_down = -half_height * up;
    vec3 d_left = -half_width * right_dir;

    vec3 position = base_pos - half_height * up;
    if (0 == gl_VertexID || 4 == gl_VertexID)
    {
        position = position + d_up;
        frag_normal = up;
    }
    else if (1 == gl_VertexID || 5 == gl_VertexID)
    {
        position = position + d_right;
        frag_normal = right_dir;
    }
    else if (2 == gl_VertexID || 6 == gl_VertexID)
    {
        position = position + d_down;
        frag_normal = -up;
    }
    else if (3 == gl_VertexID || 7 == gl_VertexID)
    {
        position = position + d_left;
        frag_normal = -right_dir;
    }

    if (gl_VertexID > 7) {
        int prev_start_index = int(seg_startEnd_hasPrev_prevIndex.w);
        vec4 prevPos_mid = texelFetch(s_position_texture, prev_start_index);
        if (hasPrev) {
            vec3 prev_dir = startPos_mid.xyz - prevPos_mid.xyz;
            prev_dir = prev_dir / max(length(prev_dir), 1e-6);
            prev_dir = normalize(prev_dir);
            vec3 prev_right_dir = vec3(prev_dir.y, -prev_dir.x, 0.0);
            prev_right_dir = normalize(prev_right_dir);
            vec3 prev_left_dir = -prev_right_dir;
            vec3 prev_up = cross(prev_right_dir, prev_dir);

            vec3 prev_second_pos = startPos_mid.xyz - half_height * prev_up;
            if (8 == gl_VertexID)
            {
                position = prev_second_pos + half_width * prev_left_dir;
                frag_normal = prev_left_dir;
            }

            else if (9 == gl_VertexID)
            {
                position = prev_second_pos + half_width * prev_right_dir;
                frag_normal = prev_right_dir;
            }
        }
    }

    vec4 final_position = view_model_matrix * vec4(position, 1.0);
    frag_pos = final_position.xyz;
    frag_normal = normalize(frag_normal);
    gl_Position = projection_matrix * final_position;
}
