#include "erhe_bxdf.glsl"
#include "erhe_light.glsl"
#include "erhe_texture.glsl"

in vec2      v_texcoord;
in vec4      v_position;
in vec4      v_color;
in mat3      v_TBN;
in flat uint v_material_index;

void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3  V       = normalize(view_position_in_world - v_position.xyz);
    vec3  T       = normalize(v_TBN[0]);
    vec3  B       = normalize(v_TBN[1]);
    vec3  N       = normalize(v_TBN[2]);
    float N_dot_V = clamped_dot(N, V);

    Material material = material.materials[v_material_index];
    uvec2 base_color_texture         = material.base_color_texture;
    uvec2 metallic_roughness_texture = material.metallic_roughness_texture;
    vec3  base_color                 = v_color.rgb * material.base_color.rgb * sample_texture(base_color_texture, v_texcoord).rgb;
    uint  directional_light_count    = light_block.directional_light_count;
    uint  spot_light_count           = light_block.spot_light_count;
    uint  point_light_count          = light_block.point_light_count;
    uint  directional_light_offset   = 0;
    uint  spot_light_offset          = directional_light_count;
    uint  point_light_offset         = spot_light_offset + spot_light_count;
    vec3 color = vec3(0);
    color += (0.5 + 0.5 * N.y) * light_block.ambient_light.rgb * base_color;
    color += material.emissive.rgb;
    for (uint i = 0; i < directional_light_count; ++i) {
        uint  light_index    = directional_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
        vec3  L              = normalize(point_to_light);
        float N_dot_L        = clamped_dot(N, L);
        if (N_dot_L > 0.0 || N_dot_V > 0.0) {
            vec3 intensity = light.radiance_and_range.rgb * sample_light_visibility(v_position, light_index, N_dot_L);
            color += intensity * anisotropic_brdf(
                base_color,
                material.roughness.x,
                material.roughness.y,
                material.metallic,
                material.reflectance,
                L,
                V,
                T,
                B,
                N
            );
        }
    }

    for (uint i = 0; i < spot_light_count; ++i) {
        uint  light_index    = spot_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
        vec3  L              = normalize(point_to_light);
        float N_dot_L        = clamped_dot(N, L);
        if (N_dot_L > 0.0 || N_dot_V > 0.0) {
            float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
            float spot_attenuation  = get_spot_attenuation(-point_to_light, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
            float light_visibility  = sample_light_visibility(v_position, light_index, N_dot_L);
            vec3  intensity         = range_attenuation * spot_attenuation * light.radiance_and_range.rgb * light_visibility;
            color += intensity * anisotropic_brdf(
                base_color,
                material.roughness.x,
                material.roughness.y,
                material.metallic,
                material.reflectance,
                L,
                V,
                T,
                B,
                N
            );
        }
    }

    for (uint i = 0; i < point_light_count; ++i) {
        uint  light_index    = point_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
        vec3  L              = normalize(point_to_light);
        float N_dot_L        = clamped_dot(N, L);
        if (N_dot_L > 0.0 || N_dot_V > 0.0) {
            float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
            float light_visibility  = 1.0; // TODO sample_light_visibility(v_position, light_index, N_dot_L);
            vec3  intensity         = range_attenuation * light.radiance_and_range.rgb * light_visibility;
            color += intensity * anisotropic_brdf(
                base_color,
                material.roughness.x,
                material.roughness.y,
                material.metallic,
                material.reflectance,
                L,
                V,
                T,
                B,
                N
            );
        }
    }

    float exposure = camera.cameras[0].exposure;
    out_color.rgb = color * exposure * material.opacity;
    out_color.a = material.opacity;
}
