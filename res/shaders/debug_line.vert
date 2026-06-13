#include "erhe_camera_view.glsl"

layout(location = 0) out vec4  vs_color;
layout(location = 1) out float vs_line_width;

void main()
{
    vec3 world_position = a_position.xyz;
    gl_Position   = view.cameras[c_view_index].clip_from_world * vec4(world_position, 1.0);

    // See line_simple.vert: push surface-aligned lines (a_normal != 0) toward
    // the viewer by an NdotV^2-scaled bias. Zero normal => no bias.
    float normal_length = length(a_normal.xyz);
    if (normal_length > 1e-4) {
        vec3  normal = a_normal.xyz / normal_length;
        vec3  v      = normalize(view.cameras[c_view_index].view_position_in_world.xyz - world_position);
        float NdotV  = clamp(dot(normal, v), 0.0, 1.0);
        gl_Position.z -= 0.0005 * NdotV * NdotV * view.clip_depth_direction;
    }

    vs_color      = a_color_0;
    vs_line_width = a_position.w;
}
