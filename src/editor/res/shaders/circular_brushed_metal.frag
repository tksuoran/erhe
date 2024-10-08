#include "erhe_bxdf.glsl"
#include "erhe_light.glsl"
#include "erhe_texture.glsl"

in vec2      v_texcoord;
in vec4      v_position;
in vec4      v_color;
in vec2      v_aniso_control;
in mat3      v_TBN;
in flat uint v_material_index;

void main() {
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3  V  = normalize(view_position_in_world - v_position.xyz);
    vec3  T0 = normalize(v_TBN[0]); // Geometry tangent from vertex attribute
    vec3  B0 = normalize(v_TBN[1]); // Geometry bitangent from vertex attribute
    vec3  N  = normalize(v_TBN[2]);
    float N_dot_V = clamped_dot(N, V);

    Material material = material.materials[v_material_index];
    uvec2 base_color_texture         = material.base_color_texture;
    uvec2 metallic_roughness_texture = material.metallic_roughness_texture;
    vec3  base_color                 = v_color.rgb * material.base_color.rgb * sample_texture(base_color_texture, v_texcoord).rgb;

    // Generating circular anisotropy direction from texcoord
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
    vec3  T                   = circular_anisotropy_magnitude > 0.0 ? mix(T0, T_circular.x * T0 + T_circular.y * B0, v_aniso_control.y) : T0;
    vec3  B                   = circular_anisotropy_magnitude > 0.0 ? mix(B0, T_circular.y * T0 - T_circular.x * B0, v_aniso_control.y) : B0;
    float isotropic_roughness = 0.5 * material.roughness.x + 0.5 * material.roughness.y;
    // Mix roughness based on anisotropy_strength
    float roughness_x         = mix(isotropic_roughness, material.roughness.x, anisotropy_strength);
    float roughness_y         = mix(isotropic_roughness, material.roughness.y, anisotropy_strength);

    uint directional_light_count  = light_block.directional_light_count;
    uint spot_light_count         = light_block.spot_light_count;
    uint point_light_count        = light_block.point_light_count;
    uint directional_light_offset = 0;
    uint spot_light_offset        = directional_light_count;
    uint point_light_offset       = spot_light_offset + spot_light_count;
    vec3 color = vec3(0);
    color += (0.5 + 0.5 * N.y) * light_block.ambient_light.rgb * base_color;
    color += material.emissive.rgb;

    for (uint i = 0; i < directional_light_count; ++i) {
        uint  light_index    = directional_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
        vec3  L              = normalize(point_to_light);   // Direction from surface point to light
        float N_dot_L        = clamped_dot(N, L);
        if (N_dot_L > 0.0 || N_dot_V > 0.0) {
            vec3 intensity = light.radiance_and_range.rgb * sample_light_visibility(v_position, light_index, N_dot_L);
            color += intensity * anisotropic_brdf(
                base_color,
                roughness_x,
                roughness_y,
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
                roughness_x,
                roughness_y,
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
            float light_visibility  = sample_light_visibility(v_position, light_index, N_dot_L);
            vec3  intensity         = range_attenuation * light.radiance_and_range.rgb * light_visibility;
            color += intensity * anisotropic_brdf(
                base_color,
                roughness_x,
                roughness_y,
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
