#include "erhe_bxdf.glsl"
#include "erhe_camera_view.glsl"
#include "erhe_light.glsl"
#include "erhe_texture.glsl"

layout(location = 0) in vec4      v_position;

#ifdef ERHE_ATTRIBUTE_a_texcoord_0
layout(location = 1) in vec2      v_texcoord;
#else
const  vec2 v_texcoord = vec2(0.5, 0.5);
#endif

#ifdef ERHE_ATTRIBUTE_a_color_0
layout(location = 2) in vec4      v_color;
#else
const  vec4 v_color = vec4(1.0);
#endif

#ifdef ERHE_ATTRIBUTE_a_custom_1
layout(location = 3) in vec2      v_aniso_control;
#else
const  vec2 v_aniso_control = vec2(0.0);
#endif

#if defined(ERHE_ATTRIBUTE_a_tangent)
layout(location = 4) in vec3      v_T;
#endif

#if defined(ERHE_ATTRIBUTE_a_normal) && defined(ERHE_ATTRIBUTE_a_tangent)
layout(location = 5) in vec3      v_B;
#endif

#ifdef ERHE_ATTRIBUTE_a_normal
layout(location = 6) in vec3      v_N;
#endif

layout(location = 7) flat in uint v_material_index;

void main()
{
    Material material           = material.materials[v_material_index];
    uvec2    base_color_texture = material.base_color_texture;
    vec3     base_color         = v_color.rgb * material.base_color.rgb * sample_texture(
        base_color_texture,
        v_texcoord,
        material.base_color_rotation_scale,
        material.base_color_offset
    ).rgb;
    vec3 color;
    if (material.unlit == 1) {
        color = base_color;
    } else {
        vec3 view_position_in_world = vec3(
            camera.cameras[c_view_index].world_from_node[3][0],
            camera.cameras[c_view_index].world_from_node[3][1],
            camera.cameras[c_view_index].world_from_node[3][2]
        );

        vec3 V = normalize(view_position_in_world - v_position.xyz);

#ifdef ERHE_ATTRIBUTE_a_tangent
        vec3 T = normalize(v_T);
#endif
#if defined(ERHE_ATTRIBUTE_a_normal) && defined(ERHE_ATTRIBUTE_a_tangent)
        vec3 B = normalize(v_B);
#endif
#ifdef ERHE_ATTRIBUTE_a_normal
        vec3 N = normalize(v_N);
#else
        vec3 N = vec3(0.0, 1.0, 0.0);
#endif

        uvec2 metallic_roughness_texture = material.metallic_roughness_texture;
        uvec2 normal_texture             = material.normal_texture;
        uvec2 occlusion_texture          = material.occlusion_texture;
        uvec2 emissive_texture           = material.emissive_texture;

        vec4 metallic_roughness = sample_texture(
            metallic_roughness_texture,
            v_texcoord,
            material.metallic_roughness_rotation_scale,
            material.metallic_roughness_offset
        );
        float metallic  = material.metallic * metallic_roughness.b;
        float roughness = max(material.roughness.x * metallic_roughness.g, 1e-4);

        if (normal_texture.x != max_u32) {
            vec3 ntex = sample_texture(
                normal_texture,
                v_texcoord,
                material.normal_rotation_scale,
                material.normal_offset
            ).xyz * 2.0 - vec3(1.0);
            ntex.xy = ntex.xy * material.normal_texture_scale;
            ntex    = normalize(ntex);
#if defined(ERHE_ATTRIBUTE_a_normal) && defined(ERHE_ATTRIBUTE_a_tangent)
            N       = normalize(mat3(T, B, N) * ntex);
#else
            N       = ntex;
#endif
        }

        vec3 emissive = material.emissive.rgb * material.emissive.rgb * sample_texture(
            emissive_texture,
            v_texcoord,
            material.emissive_rotation_scale,
            material.emissive_offset
        ).rgb;

        float occlusion = sample_texture(
            occlusion_texture,
            v_texcoord,
            material.occlusion_rotation_scale,
            material.occlusion_offset
        ).r;

        float N_dot_V = clamped_dot(N, V);

        uint directional_light_count    = light_block.directional_light_count;
        uint spot_light_count           = light_block.spot_light_count;
        uint point_light_count          = light_block.point_light_count;
        uint directional_shadow_count   = light_block.directional_shadow_count;
        uint spot_shadow_count          = light_block.spot_shadow_count;
        uint point_shadow_count         = light_block.point_shadow_count;
        uint directional_light_offset   = 0;
        uint spot_light_offset          = directional_light_count;
        uint point_light_offset         = spot_light_offset + spot_light_count;

        //color += (0.5 + 0.5 * N.y) * light_block.ambient_light.rgb * base_color;
        color  = light_block.ambient_light.rgb * occlusion * base_color;
        color += emissive;

        // Directional - shadow-mapped prefix
        for (uint i = 0u; i < directional_shadow_count; ++i) {
            uint  light_index    = directional_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                vec3 intensity = light.radiance_and_range.rgb * sample_light_visibility(v_position, light_index, N_dot_L);
                color += intensity * isotropic_brdf(
                    base_color,
                    roughness,
                    metallic,
                    material.reflectance,
                    L,
                    V,
                    N
                );
            }
        }

        // Directional - non-shadow suffix
        for (uint i = directional_shadow_count; i < directional_light_count; ++i) {
            uint  light_index    = directional_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                vec3 intensity = light.radiance_and_range.rgb;
                color += intensity * isotropic_brdf(
                    base_color,
                    roughness,
                    metallic,
                    material.reflectance,
                    L,
                    V,
                    N
                );
            }
        }

        // Spot - shadow-mapped prefix
        for (uint i = 0u; i < spot_shadow_count; ++i) {
            uint  light_index    = spot_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                float spot_attenuation  = get_spot_attenuation(-point_to_light, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
                float light_visibility  = sample_light_visibility(v_position, light_index, N_dot_L);
                vec3  intensity         = range_attenuation * spot_attenuation * light.radiance_and_range.rgb * light_visibility;
                color += intensity * isotropic_brdf(
                    base_color,
                    roughness,
                    metallic,
                    material.reflectance,
                    L,
                    V,
                    N
                );
            }
        }

        // Spot - non-shadow suffix
        for (uint i = spot_shadow_count; i < spot_light_count; ++i) {
            uint  light_index    = spot_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                float spot_attenuation  = get_spot_attenuation(-point_to_light, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
                vec3  intensity         = range_attenuation * spot_attenuation * light.radiance_and_range.rgb;
                color += intensity * isotropic_brdf(
                    base_color,
                    roughness,
                    metallic,
                    material.reflectance,
                    L,
                    V,
                    N
                );
            }
        }

        // Point - shadow-mapped prefix
        // Point shadows are not yet sampled (TODO in old code), so this matches the suffix branch.
        for (uint i = 0u; i < point_shadow_count; ++i) {
            uint  light_index    = point_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                vec3  intensity         = range_attenuation * light.radiance_and_range.rgb;
                color += intensity * isotropic_brdf(
                    base_color,
                    roughness,
                    metallic,
                    material.reflectance,
                    L,
                    V,
                    N
                );
            }
        }

        // Point - non-shadow suffix
        for (uint i = point_shadow_count; i < point_light_count; ++i) {
            uint  light_index    = point_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                vec3  intensity         = range_attenuation * light.radiance_and_range.rgb;
                color += intensity * isotropic_brdf(
                    base_color,
                    roughness,
                    metallic,
                    material.reflectance,
                    L,
                    V,
                    N
                );
            }
        }
    }

    float exposure = camera.cameras[c_view_index].exposure;
    out_color.rgb = color * exposure * material.opacity;
    out_color.a = material.opacity;
}
