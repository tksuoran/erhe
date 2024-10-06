#include "erhe_srgb.glsl"

in vec3      v_position;
in vec3      v_normal;
in flat uint v_material_index;

void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3  v     = normalize(view_position_in_world - v_position);
    vec3  n     = normalize(v_normal);
    float NdotV = max(dot(n, v), 0.0);
    out_color.rgb = srgb_to_linear(material.materials[v_material_index].base_color.rgb * (0.5 + pow(NdotV, 5.0) * 2.5));

#if 0
    const vec3 palette[24] = vec3[24](
        vec3(0.0, 0.0, 0.0), // 0
        vec3(1.0, 0.0, 0.0), // 1
        vec3(0.0, 1.0, 0.0), // 2
        vec3(0.0, 0.0, 1.0), // 3
        vec3(1.0, 1.0, 0.0), // 4
        vec3(0.0, 1.0, 1.0), // 5
        vec3(1.0, 0.0, 1.0), // 6
        vec3(1.0, 1.0, 1.0), // 7
        vec3(0.5, 0.0, 0.0), // 8
        vec3(0.0, 0.5, 0.0), // 9
        vec3(0.0, 0.0, 0.5), // 10
        vec3(0.5, 0.5, 0.0), // 11
        vec3(0.0, 0.5, 0.5), // 12
        vec3(0.5, 0.0, 0.5), // 13
        vec3(0.5, 0.5, 0.5), // 14
        vec3(1.0, 0.5, 0.0), // 15
        vec3(1.0, 0.0, 0.5), // 16
        vec3(0.5, 1.0, 0.0), // 17
        vec3(0.0, 1.0, 0.5), // 18
        vec3(0.5, 0.0, 1.0), // 19
        vec3(0.0, 0.5, 1.0), // 20
        vec3(1.0, 1.0, 0.5), // 21
        vec3(0.5, 1.0, 1.0), // 22
        vec3(1.0, 0.5, 1.0)  // 23
    );

    out_color.rgb = srgb_to_linear(palette[v_material_index % 24]);
#endif

    out_color.a = 1.0;
}
