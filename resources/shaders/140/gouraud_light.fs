#version 140

uniform vec4 uniform_color;
uniform float emission_factor;

// x = tainted, y = specular;
in vec2 intensity;

out vec4 frag_color;

void main()
{
    frag_color = vec4(vec3(intensity.y) + uniform_color.rgb * (intensity.x + emission_factor), uniform_color.a);
}
