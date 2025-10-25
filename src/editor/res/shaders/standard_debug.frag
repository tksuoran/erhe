#include "erhe_bxdf.glsl"
#include "erhe_light.glsl"
#include "erhe_srgb.glsl"
#include "erhe_texture.glsl"

layout(location =  0) in vec4       v_position;
layout(location =  1) in vec2       v_texcoord;
layout(location =  2) in vec4       v_color;
layout(location =  3) in vec2       v_aniso_control;
layout(location =  4) in vec3       v_T;
layout(location =  5) in vec3       v_B;
layout(location =  6) in vec3       v_N;
layout(location =  7) in flat uint  v_material_index;
layout(location =  8) in float      v_tangent_scale;
layout(location =  9) in float      v_line_width;
layout(location = 10) in vec4       v_bone_color;
layout(location = 11) in flat uvec2 v_valency_edge_count;

const vec3 palette[24] = vec3[24](
    vec3(0.0, 0.0, 0.0), //  0
    vec3(1.0, 0.0, 0.0), //  1
    vec3(0.0, 1.0, 0.0), //  2
    vec3(0.0, 0.0, 1.0), //  3
    vec3(1.0, 1.0, 0.0), //  4
    vec3(0.0, 1.0, 1.0), //  5
    vec3(1.0, 0.0, 1.0), //  6
    vec3(1.0, 1.0, 1.0), //  7
    vec3(0.5, 0.0, 0.0), //  8
    vec3(0.0, 0.5, 0.0), //  9
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
    //// if (material.unlit == 1) {
    ////     color = base_color;
    //// } else {
        vec3 view_position_in_world = vec3(
            camera.cameras[0].world_from_node[3][0],
            camera.cameras[0].world_from_node[3][1],
            camera.cameras[0].world_from_node[3][2]
        ); 

        vec3 V = normalize(view_position_in_world - v_position.xyz);
        vec3 T = normalize(v_T);
        vec3 B = normalize(v_B);
        vec3 N = normalize(v_N);

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
            N       = normalize(mat3(T, B, N) * ntex);
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

        uint directional_light_count  = light_block.directional_light_count;
        uint spot_light_count         = light_block.spot_light_count;
        uint point_light_count        = light_block.point_light_count;
        uint directional_light_offset = 0;
        uint spot_light_offset        = directional_light_count;
        uint point_light_offset       = spot_light_offset + spot_light_count;

        //color += (0.5 + 0.5 * N.y) * light_block.ambient_light.rgb * base_color;
        color  = light_block.ambient_light.rgb * occlusion * base_color;
        color += emissive;

        for (uint i = 0; i < directional_light_count; ++i) {
            uint  light_index    = directional_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
            vec3  L              = normalize(point_to_light);   // Direction from surface point to light
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) { // || N_dot_V > 0.0
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

        for (uint i = 0; i < spot_light_count; ++i) {
            uint  light_index    = spot_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) { // || N_dot_V > 0.0
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

        for (uint i = 0; i < point_light_count; ++i) {
            uint  light_index    = point_light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) { // || N_dot_V > 0.0
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                float light_visibility  = 1.0; // TODO sample_light_visibility(v_position, light_index, N_dot_L);
                vec3  intensity         = range_attenuation * light.radiance_and_range.rgb * light_visibility;
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
    ////}
    if (material.unlit == 1) {
        color = base_color;
    }

    float exposure = camera.cameras[0].exposure;
    out_color.rgb = color * exposure * material.opacity;
    out_color.a = material.opacity;

    vec4 empty = vec4(1.0);
    {
        float frequency = 0.02;
        float gray = 0.9;

        vec2 v1 = step(0.5, fract(frequency * gl_FragCoord.xy));
        vec2 v2 = step(0.5, vec2(1.0) - fract(frequency * gl_FragCoord.xy));
        empty *= gray + v1.x * v1.y + v2.x * v2.y;
    }
    uint  light_index       = 0;
    Light light             = light_block.lights[light_index];
    vec3  point_to_light    = light.position_and_inner_spot_cos.xyz - v_position.xyz;
    vec3  L                 = normalize(point_to_light);
    float N_dot_L           = dot(N, L);
    float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
    float spot_attenuation  = get_spot_attenuation(-point_to_light, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
    float light_visibility  = sample_light_visibility(v_position, light_index, N_dot_L);
    vec3  intensity         = range_attenuation * spot_attenuation * light.radiance_and_range.rgb * light_visibility;
    mat3 TBN   = mat3(T, B, N);
    mat3 TBN_t = transpose(TBN);
    vec3 wo    = normalize(TBN_t * V);
    vec3 wi    = normalize(TBN_t * L);
    vec3 wg    = normalize(TBN_t * N);


#if defined(ERHE_DEBUG_VERTEX_NORMAL)
    out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * normalize(v_N));
#endif
#if defined(ERHE_DEBUG_FRAGMENT_NORMAL)
    out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * N);
#endif
#if defined(ERHE_DEBUG_NORMAL_TEXTURE)
    if (normal_texture.x != max_u32) {
        out_color.rgb = srgb_to_linear(sample_texture(normal_texture, v_texcoord).rgb);
    }
#endif
#if defined(ERHE_DEBUG_TANGENT)
    out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * T);
#endif
#if defined(ERHE_DEBUG_BITANGENT)
    {
        //vec3 b = normalize(cross(v_N, v_T)) * v_tangent_scale;
        //vec3 b = normalize(cross(v_N, v_T)) * v_tangent_scale;
        //vec3 b = v_TBN[1];
        //float len = length(v_B);
        //if (len < 0.9) {
        //    out_color.rgb = vec3(1.0, 0.0, 0.0);
        //} else { 
        //    out_color.rgb = vec3(0.0, 1.0, 0.0);
        //}
        out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * B);
        //out_color.rgb = vec3(0.5) + 0.5 * v_B);
        //out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * b);
    }
#endif
#if defined(ERHE_DEBUG_TANGENT_W)
    out_color.rgb = vec3(0.5 + 0.5 * v_tangent_scale);
#endif
#if defined(ERHE_DEBUG_VDOTN)
    float V_dot_N = max(dot(V, N), 0.0);
    out_color.rgb = vec3(V_dot_N);
#endif
#if defined(ERHE_DEBUG_LDOTN)
    float L_dot_N = max(dot(L, N), 0.0);
    out_color.rgb = vec3(L_dot_N);
#endif
#if defined(ERHE_DEBUG_HDOTV)
    vec3  H       = normalize(L + V);
    float H_dot_N = max(dot(H, N), 0.0);
    out_color.rgb = vec3(H_dot_N);
#endif
#if defined(ERHE_DEBUG_JOINT_INDICES)
    out_color.rgb = vec3(1.0);
#endif
#if defined(ERHE_DEBUG_JOINT_WEIGHTS)
    out_color.rgb = srgb_to_linear(v_bone_color.rgb);
#endif
#if defined(ERHE_DEBUG_OMEGA_O)
    out_color.rgb = vec3(0.5) + 0.5 * wo;
    out_color.r = 1.0;
#endif
#if defined(ERHE_DEBUG_OMEGA_I)
    out_color.rgb = vec3(0.5) + 0.5 * wi;
    out_color.g = 1.0;
#endif
#if defined(ERHE_DEBUG_OMEGA_G)
    out_color.rgb = vec3(0.5) + 0.5 * wg;
    out_color.b = 1.0;
#endif
#if defined(ERHE_DEBUG_TEXCOORD)
    out_color.rgb = srgb_to_linear(vec3(v_texcoord, 0.0));
#endif
#if defined(ERHE_DEBUG_BASE_COLOR_TEXTURE)
    if (base_color_texture.x != max_u32) {
        out_color.rgb = srgb_to_linear(base_color);
    }
#endif
#if defined(ERHE_DEBUG_VERTEX_COLOR_RGB)
    out_color.rgb = srgb_to_linear(v_color.rgb);
#endif
#if defined(ERHE_DEBUG_VERTEX_COLOR_ALPHA)
    out_color.rgb = vec3(v_color.a);
#endif
#if defined(ERHE_DEBUG_ANISO_STRENGTH)
    out_color.rgb = vec3(v_aniso_control.x);
#endif
#if defined(ERHE_DEBUG_ANISO_TEXCOORD)
    out_color.rgb = vec3(v_aniso_control.y);
#endif
#if defined(ERHE_DEBUG_VERTEX_VALENCY)
    out_color.rgb = palette[v_valency_edge_count.x % 24];
#endif
#if defined(ERHE_DEBUG_POLYGON_EDGE_COUNT)
    out_color.rgb = palette[v_valency_edge_count.y % 24];
#endif
#if defined(ERHE_DEBUG_METALLIC)
    out_color.rgb = vec3(metallic);
#endif
#if defined(ERHE_DEBUG_ROUGHNESS)
    out_color.rgb = vec3(roughness);
#endif
#if defined(ERHE_DEBUG_OCCLUSION)
    out_color.rgb = vec3(occlusion);
#endif
#if defined(ERHE_DEBUG_EMISSIVE)
    out_color.rgb = emissive;
#endif
#if defined(ERHE_DEBUG_SHADOWMAP_TEXELS)
    {
        vec4  position    = v_position;
        uint  light_index = 0;

        Light light                                 = light_block.lights[light_index];
        float array_layer                           = float(light_index);
        vec4  position_in_light_texture_homogeneous = light.texture_from_world * position;
        vec3  position_in_light_texture             = position_in_light_texture_homogeneous.xyz / position_in_light_texture_homogeneous.w;

        vec2  shadowmap_resolution = get_texture_size(light_block.shadow_texture_compare);

        vec2  uv = fract(shadowmap_resolution * position_in_light_texture.xy);
        vec2  v1 = step(vec2(0.5), uv);
        vec2  v2 = step(vec2(0.5), vec2(1.0) - uv);
        float v  = v1.x * v1.y + v2.x * v2.y;
        bool inside = 
            (position_in_light_texture.x >= 0.0) &&
            (position_in_light_texture.y >= 0.0) &&
            (position_in_light_texture.x <= 1.0) &&
            (position_in_light_texture.y <= 1.0);
        if (inside) {
            out_color.r += v;
        } else {
            out_color.b += v;
        }
    }
#endif
#if defined(ERHE_DEBUG_MISC)
    // Show Draw ID

    // Show Directional light L . N
    out_color.rgb = palette[v_material_index % 24];

    float N_dot_T = dot(N, T);
    float N_dot_B = dot(N, B);
    float T_dot_B = dot(T, B);

    out_color.rgb = vec3(N_dot_T, N_dot_B, T_dot_B);

    // Show material
    //Material material = material.materials[v_material_index];
    //out_color.rgb = srgb_to_linear(material.base_color.rgb);

#endif
    out_color.a = 1.0;
}
