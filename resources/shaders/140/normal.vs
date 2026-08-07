#version 140

// Writes world-space normals into a G-buffer consumed by the SSAO pass.

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat4 volume_world_matrix;
uniform mat3 volume_world_normal_matrix;

uniform vec2 z_range;
uniform vec4 clipping_plane;

in vec3 v_position;
in vec3 v_normal;

out vec3 world_normal;
out vec3 clipping_planes_dots;

void main()
{
    vec4 world_position =
        volume_world_matrix * vec4(v_position, 1.0);

    world_normal =
        volume_world_normal_matrix * v_normal;

    clipping_planes_dots = vec3(
        dot(world_position, clipping_plane),
        world_position.z - z_range.x,
        z_range.y - world_position.z);

    gl_Position =
        projection_matrix *
        view_model_matrix *
        vec4(v_position, 1.0);
}
