#version 110

// Encodes the world-space normal (Z-up) into [0, 1] so it can be stored in an
// 8-bit RGBA color texture. The SSAO pass decodes it with rgb * 2.0 - 1.0.

varying vec3 world_normal;

void main()
{
    vec3 n = normalize(world_normal);
    gl_FragColor = vec4(n * 0.5 + 0.5, 1.0);
}
