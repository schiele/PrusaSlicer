#version 140

#define INTENSITY_CORRECTION 0.6

const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)

const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)

#define INTENSITY_AMBIENT    0.25
#define SPECULAR_INTENSITY   0.35
#define BLINN_SHININESS      32.0
#define RIM_POWER            3.0
#define RIM_INTENSITY        0.18

const vec3 SH_L00  = vec3( 0.38,  0.36,  0.34);
const vec3 SH_L1m1 = vec3( 0.02,  0.01, -0.01);
const vec3 SH_L10  = vec3( 0.12,  0.12,  0.14);
const vec3 SH_L11  = vec3(-0.04, -0.03, -0.02);
const vec3 SH_L2m2 = vec3(-0.01, -0.01, -0.01);
const vec3 SH_L2m1 = vec3( 0.03,  0.03,  0.04);
const vec3 SH_L20  = vec3( 0.06,  0.06,  0.08);
const vec3 SH_L21  = vec3(-0.01, -0.01, -0.01);
const vec3 SH_L22  = vec3(-0.02, -0.02, -0.02);
#define REFLECTION_STRENGTH  0.25

const vec3  ZERO    = vec3(0.0, 0.0, 0.0);
const float EPSILON = 0.0001;

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

    vec3 refl = reflect(-view_dir, eye_normal);
    vec3 sh_color = SH_L00
        + SH_L1m1 * refl.y + SH_L10 * refl.z + SH_L11 * refl.x
        + SH_L2m2 * (refl.x * refl.y) + SH_L2m1 * (refl.y * refl.z)
        + SH_L20 * (3.0 * refl.z * refl.z - 1.0) + SH_L21 * (refl.x * refl.z)
        + SH_L22 * (refl.x * refl.x - refl.y * refl.y);
    vec3 reflection = max(sh_color, vec3(0.0)) * REFLECTION_STRENGTH;

    out_color = vec4(min(vec3(specular + rim) + reflection + color * diffuse, vec3(1.0)), alpha);
}
