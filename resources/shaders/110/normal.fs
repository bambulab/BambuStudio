#version 110

// Encodes the world-space normal into [0, 1] for the SSAO pass.

varying vec3 world_normal;
varying vec3 clipping_planes_dots;

void main()
{
    if (any(lessThan(
            clipping_planes_dots,
            vec3(0.0)))) {
        discard;
    }

    vec3 normal = normalize(world_normal);

    gl_FragColor =
        vec4(normal * 0.5 + 0.5, 1.0);
}
