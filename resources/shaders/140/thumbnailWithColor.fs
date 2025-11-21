#version 140

uniform bool ban_light;
uniform vec4 uniform_color;
uniform float emission_factor;

// x = tainted, y = specular;
in vec2 intensity;
//varying float drop;
in vec4 world_pos;
in vec4 pos_color;
out vec4 frag_color;

void main()
{
    if (world_pos.z < 1e-6)
        discard;
    if(ban_light){
       frag_color = uniform_color;
    } else{
       frag_color = vec4(vec3(intensity.y) + pos_color.rgb * (intensity.x + emission_factor), pos_color.a);
    }
}
