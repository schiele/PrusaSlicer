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

uniform vec4 uniform_color;
uniform float emission_factor;

varying vec3 eye_normal;
varying vec3 eye_position;

void main()
{
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

    gl_FragColor = vec4(vec3(specular + rim) + uniform_color.rgb * (diffuse + emission_factor), uniform_color.a);
}
