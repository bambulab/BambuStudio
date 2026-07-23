#version 110

// Writes per-fragment world-space normals into a G-buffer that the SSAO pass
// samples. Derive the world normal from volume_world_matrix, a top-level
// uniform the volume render path always sets (the SlopeDetection struct member
// did not reach this shader and left the normal at zero).

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat4 volume_world_matrix;

attribute vec3 v_position;
attribute vec3 v_normal;

varying vec3 world_normal;

void main()
{
    world_normal = (volume_world_matrix * vec4(v_normal, 0.0)).xyz;
    gl_Position = projection_matrix * view_model_matrix * vec4(v_position, 1.0);
}
