#version 110

uniform bool ban_light;
uniform vec4 uniform_color;
uniform float emission_factor;

// x = tainted, y = specular;
varying vec2 intensity;
//varying float drop;
varying vec4 world_pos;
varying vec4 pos_color;
void main()
{
    if (world_pos.z < 1e-6)
        discard;
    if(ban_light){
       gl_FragColor = uniform_color;
    } else{
       gl_FragColor = vec4(vec3(intensity.y) + pos_color.rgb * (intensity.x + emission_factor), pos_color.a);
    }
}
