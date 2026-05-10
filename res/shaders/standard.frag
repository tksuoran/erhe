#include "erhe_bxdf.glsl"
#include "erhe_camera_view.glsl"
#include "erhe_light.glsl"
#include "erhe_texture.glsl"
#include "erhe_standard_variant.glsl"

layout(location = 0) in vec4      v_position;

#ifdef ERHE_USE_VERTEX_VARYING_TEXCOORD0
layout(location = 1) in vec2      v_texcoord;
#else
const  vec2 v_texcoord = vec2(0.5, 0.5);
#endif

#ifdef ERHE_USE_VERTEX_VARYING_COLOR
layout(location = 2) in vec4      v_color;
#else
const  vec4 v_color = vec4(1.0);
#endif

#ifdef ERHE_USE_VERTEX_VARYING_ANISO_CONTROL
layout(location = 3) in vec2      v_aniso_control;
#else
const  vec2 v_aniso_control = vec2(0.0);
#endif

#ifdef ERHE_USE_VERTEX_VARYING_TANGENT
layout(location = 4) in vec3      v_T;
#endif

#ifdef ERHE_USE_VERTEX_VARYING_BITANGENT
layout(location = 5) in vec3      v_B;
#endif

#ifdef ERHE_USE_VERTEX_VARYING_NORMAL
layout(location = 6) in vec3      v_N;
#endif

layout(location = 7) flat in uint v_material_index;

void main()
{
    Material material   = material.materials[v_material_index];
    vec3     base_color = material.base_color.rgb;

#ifdef ERHE_USE_VERTEX_VARYING_COLOR
    base_color *= v_color.rgb;
#endif

#ifdef ERHE_USE_BASE_COLOR_TEXTURE
    uvec2    base_color_texture = material.base_color_texture;
    base_color *= sample_texture(
        base_color_texture,
        v_texcoord,
        material.base_color_rotation_scale,
        material.base_color_offset
    ).rgb;
#endif

    vec3 color;
#if ERHE_BXDF_MODEL == ERHE_BXDF_MODEL_UNLIT
    color = base_color;
#else
    {
        vec3 view_position_in_world = vec3(
            camera.cameras[c_view_index].world_from_node[3][0],
            camera.cameras[c_view_index].world_from_node[3][1],
            camera.cameras[c_view_index].world_from_node[3][2]
        );

        vec3 V = normalize(view_position_in_world - v_position.xyz);

#ifdef ERHE_USE_VERTEX_VARYING_TANGENT
        vec3 T = normalize(v_T);
#endif
#ifdef ERHE_USE_VERTEX_VARYING_BITANGENT
        vec3 B = normalize(v_B);
#endif

#ifdef ERHE_USE_VERTEX_VARYING_NORMAL
        vec3 N = normalize(v_N);
#else
        vec3 N = vec3(0.0, 1.0, 0.0);
#endif

#if defined(ERHE_USE_CIRCULAR_BRUSHED_METAL) && defined(ERHE_USE_VERTEX_VARYING_TANGENT) && defined(ERHE_USE_VERTEX_VARYING_BITANGENT) && defined(ERHE_USE_VERTEX_VARYING_NORMAL)
        {
            float circular_anisotropy_magnitude = pow(length(v_texcoord) * 8.0, 0.25);
            vec2  T_circular                    = (circular_anisotropy_magnitude > 0.0) ? normalize(v_texcoord) : vec2(0.0, 0.0);
            // X is used to modulate anisotropy level:
            //   0.0 -- Anisotropic
            //   1.0 -- Isotropic when approaching texcoord (0, 0)
            // Y is used for tangent space selection/control:
            //   0.0 -- Use geometry T and B (from vertex attribute
            //   1.0 -- Use T and B derived from texcoord
            // GLSL mix(a, b, w) = a * (1 - w) + y * w   w == 0 -> a, w == 1 -> b
            float anisotropy_strength = mix(
                1.0,
                min(1.0, circular_anisotropy_magnitude),
                v_aniso_control.y
            ) * v_aniso_control.x;
            // Mix tangent space geometric .. texcoord generated
            vec3 T0 = T;
            vec3 B0 = B;
            T = circular_anisotropy_magnitude > 0.0 ? mix(T0, T_circular.x * T0 + T_circular.y * B0, v_aniso_control.y) : T0;
            B = circular_anisotropy_magnitude > 0.0 ? mix(B0, T_circular.y * T0 - T_circular.x * B0, v_aniso_control.y) : B0;
        }
#endif

#ifdef ERHE_USE_METALLIC_ROUGHNESS_TEXTURE
        vec4 metallic_roughness = sample_texture(
            material.metallic_roughness_texture,
            v_texcoord,
            material.metallic_roughness_rotation_scale,
            material.metallic_roughness_offset
        );
        float metallic    = material.metallic * metallic_roughness.b;
        float roughness_x = max(material.roughness.x * metallic_roughness.g, 1e-4);
        float roughness_y = max(material.roughness.y * metallic_roughness.g, 1e-4);
#else
        float metallic    = material.metallic;
        float roughness_x = material.roughness.x;
        float roughness_y = material.roughness.y;
#endif

#ifdef ERHE_USE_NORMAL_TEXTURE
        {
            vec3 ntex = sample_texture(
                material.normal_texture,
                v_texcoord,
                material.normal_rotation_scale,
                material.normal_offset
            ).xyz * 2.0 - vec3(1.0);
            ntex.xy = ntex.xy * material.normal_texture_scale;
            ntex    = normalize(ntex);
#   if defined(ERHE_USE_VERTEX_VARYING_NORMAL) && defined(ERHE_USE_VERTEX_VARYING_BITANGENT)
            N       = normalize(mat3(T, B, N) * ntex);
#   else
            N       = ntex;
#   endif
        }
#endif 

        vec3 emissive = material.emissive.rgb;
#ifdef ERHE_USE_EMISSION_TEXTURE
        emissive *= sample_texture(
            material.emissive_texture,
            v_texcoord,
            material.emissive_rotation_scale,
            material.emissive_offset
        ).rgb;
#endif

        float N_dot_V = clamped_dot(N, V);

        // Compile-time BxDF dispatch. The Anisotropic branch needs T and
        // B; if the mesh / variant did not enable the tangent/bitangent
        // varyings, fall back to the isotropic call (using roughness_x)
        // so the shader still links and renders something reasonable.
#if (ERHE_BXDF_MODEL == ERHE_BXDF_MODEL_ANISOTROPIC_BRDF) && defined(ERHE_USE_VERTEX_VARYING_TANGENT) && defined(ERHE_USE_VERTEX_VARYING_BITANGENT)
#  define BXDF_CALL(L_arg) anisotropic_brdf(base_color, roughness_x, roughness_y, metallic, material.reflectance, L_arg, V, T, B, N)
#else
#  define BXDF_CALL(L_arg) isotropic_brdf(base_color, roughness_x, metallic, material.reflectance, L_arg, V, N)
#endif

        // Per-type light loop bounds. These are compile-time integer
        // literals emitted by Standard_shader_variants from the
        // Standard_variant_key counts, so each loop unrolls or disappears.
        uint directional_light_offset = 0;
        uint spot_light_offset        = uint(ERHE_LIGHT_DIRECTIONAL_COUNT);
        uint point_light_offset       = spot_light_offset + uint(ERHE_LIGHT_SPOT_COUNT);

        //color += (0.5 + 0.5 * N.y) * light_block.ambient_light.rgb * base_color;
        color  = light_block.ambient_light.rgb * base_color;
#ifdef ERHE_USE_OCCLUSION_TEXTURE
        {
            float occlusion = sample_texture(
                material.occlusion_texture,
                v_texcoord,
                material.occlusion_rotation_scale,
                material.occlusion_offset
            ).r;
            color *= occlusion;
        }
#endif
        color += emissive;

        // Directional - shadow-mapped prefix
        for (uint i = 0u; i < uint(ERHE_LIGHT_DIRECTIONAL_SHADOWMAPPED_COUNT); ++i) {
            uint  light_index    = directional_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                vec3 intensity = light.radiance_and_range.rgb * sample_light_visibility(v_position, light_index, N_dot_L);
                color += intensity * BXDF_CALL(L);
            }
        }

        // Directional - non-shadow suffix
        for (uint i = uint(ERHE_LIGHT_DIRECTIONAL_SHADOWMAPPED_COUNT); i < uint(ERHE_LIGHT_DIRECTIONAL_COUNT); ++i) {
            uint  light_index    = directional_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                vec3 intensity = light.radiance_and_range.rgb;
                color += intensity * BXDF_CALL(L);
            }
        }

        // Spot - shadow-mapped prefix
        for (uint i = 0u; i < uint(ERHE_LIGHT_SPOT_SHADOWMAPPED_COUNT); ++i) {
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
                color += intensity * BXDF_CALL(L);
            }
        }

        // Spot - non-shadow suffix
        for (uint i = uint(ERHE_LIGHT_SPOT_SHADOWMAPPED_COUNT); i < uint(ERHE_LIGHT_SPOT_COUNT); ++i) {
            uint  light_index    = spot_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                float spot_attenuation  = get_spot_attenuation(-point_to_light, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
                vec3  intensity         = range_attenuation * spot_attenuation * light.radiance_and_range.rgb;
                color += intensity * BXDF_CALL(L);
            }
        }

        // Point - shadow-mapped prefix
        // Point shadows are not yet sampled (TODO in old code), so this matches the suffix branch.
        for (uint i = 0u; i < uint(ERHE_LIGHT_POINT_SHADOWMAPPED_COUNT); ++i) {
            uint  light_index    = point_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                vec3  intensity         = range_attenuation * light.radiance_and_range.rgb;
                color += intensity * BXDF_CALL(L);
            }
        }

        // Point - non-shadow suffix
        for (uint i = uint(ERHE_LIGHT_POINT_SHADOWMAPPED_COUNT); i < uint(ERHE_LIGHT_POINT_COUNT); ++i) {
            uint  light_index    = point_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                vec3  intensity         = range_attenuation * light.radiance_and_range.rgb;
                color += intensity * BXDF_CALL(L);
            }
        }
#undef BXDF_CALL
    }
#endif

    float exposure = camera.cameras[c_view_index].exposure;
    out_color.rgb = color * exposure * material.opacity;
    out_color.a = material.opacity;
}
