#version 110

const vec3 ZERO = vec3(0.0, 0.0, 0.0);
//BBS: add grey and orange
//const vec3 GREY = vec3(0.9, 0.9, 0.9);
const vec3 ORANGE = vec3(0.8, 0.4, 0.0);
const vec3 LightRed = vec3(0.78, 0.0, 0.0);
const vec3 LightBlue = vec3(0.73, 1.0, 1.0);
const float EPSILON = 0.0001;
const float ONE_OVER_EPSILON = 1e4;

struct PrintVolumeDetection
{
	// 0 = rectangle, 1 = circle, 2 = custom, 3 = invalid
	int type;
    // type = 0 (rectangle):
    // x = min.x, y = min.y, z = max.x, w = max.y
    // type = 1 (circle):
    // x = center.x, y = center.y, z = radius
	vec4 xy_data;
    // x = min z, y = max z
	vec2 z_data;
};

struct SlopeDetection
{
    bool actived;
	float normal_z;
    mat3 volume_world_normal_matrix;
};

uniform vec4 uniform_color;
uniform SlopeDetection slope;

//BBS: add outline_color
uniform bool is_outline;

uniform bool offset_depth_buffer;

#ifdef ENABLE_ENVIRONMENT_MAP
    uniform sampler2D environment_tex;
    uniform bool use_environment_tex;
#endif // ENABLE_ENVIRONMENT_MAP

varying vec3 clipping_planes_dots;

// x = diffuse, y = specular;
varying vec2 intensity;

uniform PrintVolumeDetection print_volume;
uniform vec3 extruder_printable_heights;
varying vec4 world_pos;
varying float world_normal_z;
varying vec3 eye_normal;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;
    vec3  color = uniform_color.rgb;
    float alpha = uniform_color.a;

    if (slope.actived) {
         if(world_pos.z<0.1&&world_pos.z>-0.1)
         {
                color = LightBlue;
                alpha = 0.8;
         }
         else if( world_normal_z < slope.normal_z - EPSILON)
         {
                color = color * 0.5 + LightRed * 0.5;
                alpha = 0.8;
         }
    }
	// if the fragment is outside the print volume -> use darker color
    vec3 pv_check_min =  (world_pos.xyz - vec3(print_volume.xy_data.x, print_volume.xy_data.y, print_volume.z_data.x)) * ONE_OVER_EPSILON;
    vec3 pv_check_max = (world_pos.xyz - vec3(print_volume.xy_data.z, print_volume.xy_data.w, print_volume.z_data.y)) * ONE_OVER_EPSILON;
    bool is_out_print_limit =(any(lessThan(pv_check_min, vec3(1.0))) || any(greaterThan(pv_check_max, vec3(1.0))));
    if (print_volume.type == 0) {// rectangle
        color = is_out_print_limit ? mix(color, ZERO, 0.3333) : color;
    }
    else if (print_volume.type == 1) {
        // circle
        float delta_radius = print_volume.xy_data.z - distance(world_pos.xy, print_volume.xy_data.xy);
        pv_check_min = vec3(delta_radius, 0.0, world_pos.z - print_volume.z_data.x) * ONE_OVER_EPSILON;
        pv_check_max = vec3(0.0, 0.0, world_pos.z - print_volume.z_data.y) * ONE_OVER_EPSILON;
        color = (any(lessThan(pv_check_min, vec3(1.0))) || any(greaterThan(pv_check_max, vec3(1.0)))) ? mix(color, ZERO, 0.3333) : color;
    }
    if(extruder_printable_heights.x >= 1.0 ){
        pv_check_min = (world_pos.xyz - vec3(print_volume.xy_data.x, print_volume.xy_data.y, extruder_printable_heights.y)) * ONE_OVER_EPSILON;
        pv_check_max = (world_pos.xyz - vec3(print_volume.xy_data.z, print_volume.xy_data.w, extruder_printable_heights.z)) * ONE_OVER_EPSILON;
        bool is_out_printable_height = (all(greaterThan(pv_check_min, vec3(1.0))) && all(lessThan(pv_check_max, vec3(1.0)))) ;
        color = is_out_printable_height ? mix(color, ZERO, 0.7) : color;
    }
	//BBS: add outline_color
	if (is_outline)
		gl_FragColor = uniform_color;
#ifdef ENABLE_ENVIRONMENT_MAP
    else if (use_environment_tex)
        gl_FragColor = vec4(0.45 * texture2D(environment_tex, normalize(eye_normal).xy * 0.5 + 0.5).xyz + 0.8 * color * intensity.x, alpha);
#endif
	else
        gl_FragColor = vec4(vec3(intensity.y) + color * intensity.x, alpha);

    // In the support painting gizmo and the seam painting gizmo are painted triangles rendered over the already
    // rendered object. To resolved z-fighting between previously rendered object and painted triangles, values
    // inside the depth buffer are offset by small epsilon for painted triangles inside those gizmos.
    gl_FragDepth = gl_FragCoord.z - (offset_depth_buffer ? EPSILON : 0.0);
}