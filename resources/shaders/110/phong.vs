#version 110

const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
const vec3 ZERO = vec3(0.0, 0.0, 0.0);

struct SlopeDetection
{
    bool actived;
    float normal_z;
    mat3 volume_world_normal_matrix;
    vec3 color;
};

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 view_normal_matrix;
uniform mat4 volume_world_matrix;
uniform SlopeDetection slope;

uniform vec2 z_range;
uniform vec4 clipping_plane;
uniform vec4 color_clip_plane;

attribute vec3 v_position;
attribute vec3 v_normal;

varying vec3 eye_normal;
varying vec3 eye_position;
varying vec3 clipping_planes_dots;
varying float color_clip_plane_dot;
varying vec4 world_pos;
varying float world_normal_z;

void main()
{
    eye_normal = normalize(view_normal_matrix * v_normal);

    vec4 position = view_model_matrix * vec4(v_position, 1.0);
    eye_position = position.xyz;

    world_pos = volume_world_matrix * vec4(v_position, 1.0);

    world_normal_z = slope.actived ? (normalize(slope.volume_world_normal_matrix * v_normal)).z : 0.0;

    gl_Position = projection_matrix * position;
    clipping_planes_dots = vec3(dot(world_pos, clipping_plane), world_pos.z - z_range.x, z_range.y - world_pos.z);
    color_clip_plane_dot = dot(world_pos, color_clip_plane);
}
