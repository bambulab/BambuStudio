#version 110

uniform mat4 projection_matrix;
uniform mat4 view_matrix;
uniform mat4 volume_world_matrix;

attribute vec3 v_position;

varying float reflection_height;

void main()
{
    vec4 world_position =
        volume_world_matrix * vec4(v_position, 1.0);

    reflection_height = max(world_position.z, 0.0);

    // Mirror around world z = 0 and place the image slightly below the plate.
    world_position.z = -world_position.z - 0.10;

    gl_Position =
        projection_matrix * view_matrix * world_position;
}
