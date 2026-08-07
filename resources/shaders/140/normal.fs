#version 140

// Encodes the world-space normal into [0, 1] for the SSAO pass.

in vec3 world_normal;
in vec3 clipping_planes_dots;

out vec4 out_color;

void main()
{
    if (any(lessThan(
            clipping_planes_dots,
            vec3(0.0)))) {
        discard;
    }

    vec3 normal = normalize(world_normal);

    out_color =
        vec4(normal * 0.5 + 0.5, 1.0);
}
