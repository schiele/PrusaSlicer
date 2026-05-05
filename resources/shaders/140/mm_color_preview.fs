#version 140

// Color mixing preview shader - same lighting model as mm_gouraud but with
// higher ambient and lower diffuse contrast to preserve color fidelity.
// Shadows are softened so painted colors remain readable from all angles.

#define INTENSITY_CORRECTION 0.6

const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.5 * INTENSITY_CORRECTION)

const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.2 * INTENSITY_CORRECTION)

// Higher ambient to preserve painted color readability
#define INTENSITY_AMBIENT    0.50
#define SPECULAR_INTENSITY   0.20
#define BLINN_SHININESS      32.0
#define RIM_POWER            3.0
#define RIM_INTENSITY        0.12

const vec3  ZERO    = vec3(0.0, 0.0, 0.0);

uniform vec4 uniform_color;

uniform bool volume_mirrored;

uniform mat4 view_model_matrix;
uniform mat3 view_normal_matrix;

in vec3 clipping_planes_dots;
in vec4 model_pos;

out vec4 out_color;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;
    vec3  color = uniform_color.rgb;
    float alpha = uniform_color.a;

    vec3 triangle_normal = normalize(cross(dFdx(model_pos.xyz), dFdy(model_pos.xyz)));
#ifdef FLIP_TRIANGLE_NORMALS
    triangle_normal = -triangle_normal;
#endif

    if (volume_mirrored)
        triangle_normal = -triangle_normal;

    vec3 eye_normal = normalize(view_normal_matrix * triangle_normal);
    vec3 eye_position = (view_model_matrix * model_pos).xyz;
    vec3 view_dir = normalize(-eye_position);

    float NdotL_top = max(dot(eye_normal, LIGHT_TOP_DIR), 0.0);
    float diffuse = INTENSITY_AMBIENT + NdotL_top * LIGHT_TOP_DIFFUSE;

    vec3 half_top = normalize(LIGHT_TOP_DIR + view_dir);
    float NdotH_top = max(dot(eye_normal, half_top), 0.0);
    float specular = SPECULAR_INTENSITY * pow(NdotH_top, BLINN_SHININESS);

    float NdotL_front = max(dot(eye_normal, LIGHT_FRONT_DIR), 0.0);
    diffuse += NdotL_front * LIGHT_FRONT_DIFFUSE;

    float rim = pow(max(0.0, 1.0 - dot(view_dir, eye_normal)), RIM_POWER) * RIM_INTENSITY;

    out_color = vec4(min(vec3(specular + rim) + color * diffuse, vec3(1.0)), alpha);
}
