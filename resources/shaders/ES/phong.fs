#version 100

precision highp float;

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

const vec3 ZERO = vec3(0.0, 0.0, 0.0);
const float EPSILON = 0.0001;

struct PrintVolumeDetection
{
    int type;
    vec4 xy_data;
    vec2 z_data;
};

struct SlopeDetection
{
    bool actived;
    float normal_z;
    mat3 volume_world_normal_matrix;
    vec3 color;
};

uniform vec4 uniform_color;
uniform bool use_color_clip_plane;
uniform vec4 uniform_color_clip_plane_1;
uniform vec4 uniform_color_clip_plane_2;
uniform SlopeDetection slope;

#ifdef ENABLE_ENVIRONMENT_MAP
    uniform sampler2D environment_tex;
    uniform bool use_environment_tex;
#endif // ENABLE_ENVIRONMENT_MAP

uniform PrintVolumeDetection print_volume;

varying vec3 eye_normal;
varying vec3 eye_position;
varying vec3 clipping_planes_dots;
varying float color_clip_plane_dot;
varying vec4 world_pos;
varying float world_normal_z;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;

    vec4 color;
    if (use_color_clip_plane) {
        color.rgb = (color_clip_plane_dot < 0.0) ? uniform_color_clip_plane_1.rgb : uniform_color_clip_plane_2.rgb;
        color.a = uniform_color.a;
    }
    else
        color = uniform_color;

    if (slope.actived && world_normal_z < slope.normal_z - EPSILON) {
        color.rgb = slope.color;
        color.a = 1.0;
    }

    vec3 pv_check_min = ZERO;
    vec3 pv_check_max = ZERO;
    if (print_volume.type == 0) {
        pv_check_min = world_pos.xyz - vec3(print_volume.xy_data.x, print_volume.xy_data.y, print_volume.z_data.x);
        pv_check_max = world_pos.xyz - vec3(print_volume.xy_data.z, print_volume.xy_data.w, print_volume.z_data.y);
    }
    else if (print_volume.type == 1) {
        float delta_radius = print_volume.xy_data.z - distance(world_pos.xy, print_volume.xy_data.xy);
        pv_check_min = vec3(delta_radius, 0.0, world_pos.z - print_volume.z_data.x);
        pv_check_max = vec3(0.0, 0.0, world_pos.z - print_volume.z_data.y);
    }
    color.rgb = (any(lessThan(pv_check_min, ZERO)) || any(greaterThan(pv_check_max, ZERO))) ? mix(color.rgb, ZERO, 0.3333) : color.rgb;

    vec3 normal = normalize(eye_normal);
    vec3 view_dir = normalize(-eye_position);

    float NdotL_top = max(dot(normal, LIGHT_TOP_DIR), 0.0);
    float diffuse = INTENSITY_AMBIENT + NdotL_top * LIGHT_TOP_DIFFUSE;

    vec3 half_top = normalize(LIGHT_TOP_DIR + view_dir);
    float NdotH_top = max(dot(normal, half_top), 0.0);
    float specular = SPECULAR_INTENSITY * pow(NdotH_top, BLINN_SHININESS);

    float NdotL_front = max(dot(normal, LIGHT_FRONT_DIR), 0.0);
    diffuse += NdotL_front * LIGHT_FRONT_DIFFUSE;

    float rim = pow(max(0.0, 1.0 - dot(view_dir, normal)), RIM_POWER) * RIM_INTENSITY;

    vec3 refl = reflect(-view_dir, normal);
    vec3 sh_color = SH_L00
        + SH_L1m1 * refl.y + SH_L10 * refl.z + SH_L11 * refl.x
        + SH_L2m2 * (refl.x * refl.y) + SH_L2m1 * (refl.y * refl.z)
        + SH_L20 * (3.0 * refl.z * refl.z - 1.0) + SH_L21 * (refl.x * refl.z)
        + SH_L22 * (refl.x * refl.x - refl.y * refl.y);
    vec3 reflection = max(sh_color, vec3(0.0)) * REFLECTION_STRENGTH;

#ifdef ENABLE_ENVIRONMENT_MAP
    if (use_environment_tex)
        gl_FragColor = vec4(0.45 * texture(environment_tex, normalize(eye_normal).xy * 0.5 + 0.5).xyz + 0.8 * color.rgb * diffuse, color.a);
    else
#endif
        gl_FragColor = vec4(min(vec3(specular + rim) + reflection + color.rgb * diffuse, vec3(1.0)), color.a);
}
