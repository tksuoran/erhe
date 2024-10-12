#include "erhe_srgb.glsl"

layout(location = 0) in vec4      v_position;
layout(location = 1) in vec2      v_texcoord;
layout(location = 2) in vec4      v_color;
layout(location = 3) in mat3      v_TBN;
layout(location = 6) in flat uint v_material_index;
layout(location = 7) in float     v_tangent_scale;
layout(location = 8) in float     v_line_width;

void main() {
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3  V = normalize(view_position_in_world - v_position.xyz);
    vec3  N = normalize(v_TBN[2]);

    float N_dot_V = dot(N, V);

    Material material = material.materials[v_material_index];
    out_color.rgb = srgb_to_linear(0.5 * vec3(N_dot_V) * material.base_color.rgb * material.opacity);
    out_color.a = 0.5 * material.opacity;
}
