#include "erhe_camera_view.glsl"
#include "erhe_line_surface_bias.glsl"

layout(location = 0) out vec4  vs_color;
layout(location = 1) out float vs_line_width;

void main()
{
    vec3 world_position = a_position.xyz;
    gl_Position   = view.cameras[c_view_index].clip_from_world * vec4(world_position, 1.0);

    // See line_simple.vert: depth-precision/slope-derived bias for
    // surface-aligned lines (a_normal != 0); zero normal => no bias.
    float ndc_z    = gl_Position.z / gl_Position.w;
    float bias_ndc = erhe_line_surface_bias_ndc(
        a_normal.xyz,
        world_position,
        view.cameras[c_view_index].view_position_in_world.xyz,
        ndc_z,
        view.line_bias_margin,
        view.window_to_ndc_scale
    );
    gl_Position.z -= bias_ndc * gl_Position.w * view.clip_depth_direction;

    vs_color      = a_color_0;
    vs_line_width = a_position.w;
}
