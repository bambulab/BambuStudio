#version 140

uniform vec4 uniform_color;
uniform float reflection_strength;
uniform float reflection_fade_distance;

in float reflection_height;

out vec4 out_color;

void main()
{
    float normalized_height = clamp(
        reflection_height / max(reflection_fade_distance, 1.0),
        0.0,
        1.0);

    float fade = 1.0 - normalized_height;
    fade *= fade;

    float luminance = dot(
        uniform_color.rgb,
        vec3(0.2126, 0.7152, 0.0722));

    vec3 reflected_color = mix(
        vec3(luminance),
        uniform_color.rgb,
        0.65);

    float alpha =
        reflection_strength * fade * uniform_color.a;

    out_color = vec4(reflected_color, alpha);
}
