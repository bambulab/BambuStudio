#version 140

// Planar projected shadow: the per-volume world matrix lifts the mesh into
// world space, and shadow_matrix = projection * view * planar-projection then
// flattens it onto the plate plane (world z = 0) along the light direction and
// takes it to clip space.

uniform mat4 shadow_matrix;
uniform mat4 volume_world_matrix;

in vec3 v_position;

void main()
{
    gl_Position = shadow_matrix * volume_world_matrix * vec4(v_position, 1.0);
}
