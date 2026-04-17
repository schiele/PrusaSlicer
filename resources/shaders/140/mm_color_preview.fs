#version 140

// Color mixing preview shader - same lighting model as mm_gouraud but with
// higher ambient and lower diffuse contrast to preserve color fidelity.
// Shadows are softened so painted colors remain readable from all angles.

#define INTENSITY_CORRECTION 0.6

// Same light directions as mm_gouraud for consistent shape perception
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.5 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.05 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0

const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.2 * INTENSITY_CORRECTION)

// Higher ambient than mm_gouraud (0.3) so shadows don't crush colors
#define INTENSITY_AMBIENT    0.55

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

    float NdotL = max(dot(eye_normal, LIGHT_TOP_DIR), 0.0);

    vec2 intensity = vec2(0.0);
    intensity.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
    vec3 position = (view_model_matrix * model_pos).xyz;
    intensity.y = LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(position), reflect(-LIGHT_TOP_DIR, eye_normal)), 0.0), LIGHT_TOP_SHININESS);

    NdotL = max(dot(eye_normal, LIGHT_FRONT_DIR), 0.0);
    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;

    out_color = vec4(vec3(intensity.y) + color * intensity.x, alpha);
}
