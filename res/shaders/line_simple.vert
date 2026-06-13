#include "erhe_camera_view.glsl"

layout(location = 0) out vec4 v_color;

void main()
{
    vec3 world_position = a_position.xyz;
    gl_Position = view.cameras[c_view_index].clip_from_world * vec4(world_position, 1.0);

    // Surface-aligned lines (a_normal != 0) are pushed toward the viewer so
    // they win the depth test against the coplanar surface, scaled by NdotV^2
    // so silhouette edges (normal perpendicular to view) are left in place.
    // a_normal is zero for ordinary lines and for the triangle path, giving
    // no bias.
    float normal_length = length(a_normal.xyz);
    if (normal_length > 1e-4) {
        vec3  normal = a_normal.xyz / normal_length;
        vec3  v      = normalize(view.cameras[c_view_index].view_position_in_world.xyz - world_position);
        float NdotV  = clamp(dot(normal, v), 0.0, 1.0);
        gl_Position.z -= view.line_surface_bias * NdotV * NdotV * view.clip_depth_direction;
    }

    v_color = a_color_0;
}
