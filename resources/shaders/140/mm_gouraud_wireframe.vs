#version 140

const vec3 ZERO = vec3(0.0, 0.0, 0.0);

//attribute vec3 v_position;
//attribute vec3 v_barycentric;

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat4 volume_world_matrix;
// Clipping plane, x = min z, y = max z. Used by the FFF and SLA previews to clip with a top / bottom plane.
uniform vec2 z_range;
// Clipping plane - general orientation. Used by the SLA gizmo.
uniform vec4 clipping_plane;

in vec3 v_position;
in vec3 v_color;

out vec3 clipping_planes_dots;
out vec3 model_pos;
out vec4 world_pos;
out vec3 barycentric_coordinates;

struct SlopeDetection
{
    bool actived;
	float normal_z;
    mat3 volume_world_normal_matrix;
};
uniform SlopeDetection slope;
void main()
{
    model_pos = v_position;
    //model_pos = vec4(v_position, 1.0);
    // Point in homogenous coordinates.
    world_pos = volume_world_matrix * vec4(model_pos, 1.0);

    gl_Position = projection_matrix * view_model_matrix * vec4(v_position, 1.0);
    //gl_Position = gl_ModelViewProjectionMatrix * vec4(v_position, 1.0);
    // Fill in the scalars for fragment shader clipping. Fragments with any of these components lower than zero are discarded.
    clipping_planes_dots = vec3(dot(world_pos, clipping_plane), world_pos.z - z_range.x, z_range.y - world_pos.z);

    //compute the Barycentric Coordinates
    //int vertexMod3 = gl_VertexID % 3;
    //barycentric_coordinates = vec3(float(vertexMod3 == 0), float(vertexMod3 == 1), float(vertexMod3 == 2));
    barycentric_coordinates = v_color.xyz;//v_barycentric
}
