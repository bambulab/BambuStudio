#version 140

#define INTENSITY_CORRECTION 0.6

// normalized values for (-0.6/1.31, 0.6/1.31, 1./1.31)
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0

// normalized values for (1./1.43, 0.2/1.43, 1./1.43)
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)

#define INTENSITY_AMBIENT    0.3

const mat3 KTravel_Colors = mat3(0.505, 0.064, 0.028,
                                0.219, 0.282, 0.609,
                                0.112, 0.422, 0.103);

uniform mat3 normal_matrix;
uniform float emission_factor;
uniform vec4 u_base_color;

in vec3 frag_normal;
in vec3 frag_pos;

out vec4 frag_color;

void main()
{
    // x = tainted, y = specular;
    vec2 intensity = vec2(0.0, 0.0);
    float t_emission_factor = emission_factor;
    vec3 norm = normal_matrix * normalize(frag_normal);
    norm = normalize(norm);
    // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
    // Since these two are normalized the cosine is the dot product. We also need to clamp the result to the [0,1] range.
    float NdotL = max(dot(norm, LIGHT_TOP_DIR), 0.0);

    intensity.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;

    intensity.y = LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(frag_pos.xyz), reflect(-LIGHT_TOP_DIR, norm)), 0.0), LIGHT_TOP_SHININESS);

    // Perform the same lighting calculation for the 2nd light source (no specular applied).
    NdotL = max(dot(norm, LIGHT_FRONT_DIR), 0.0);
    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;

    frag_color = vec4(vec3(intensity.y) + u_base_color.rgb * (intensity.x + t_emission_factor), u_base_color.a);
}
