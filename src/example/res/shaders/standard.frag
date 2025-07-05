layout(location = 0) in vec2      v_texcoord;
layout(location = 1) in vec4      v_position;
layout(location = 2) in vec3      v_normal;
layout(location = 3) in flat uint v_material_index;

const float m_pi   = 3.1415926535897932384626434;
const float m_i_pi = 0.3183098861837906715377675;
const uint  max_u32 = 4294967295u;

vec4 sample_texture(uvec2 texture_handle, vec2 texcoord)
{
    if (texture_handle.x == max_u32) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    }
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return texture(s_texture, v_texcoord);
#else
    return texture(s_texture[texture_handle.x], v_texcoord);
#endif
}


vec4 sample_texture_lod_bias(uvec2 texture_handle, vec2 texcoord, float lod_bias)
{
    if (texture_handle.x == max_u32) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    }
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return texture(s_texture, texcoord, lod_bias);
#else
    return texture(s_texture[texture_handle.x], texcoord, lod_bias);
#endif
}

vec2 get_texture_size(uvec2 texture_handle)
{
    if (texture_handle.x == max_u32) {
        return vec2(1.0, 1.0);
    }
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return textureSize(s_texture, 0);
#else
    return textureSize(s_texture[texture_handle.x], 0);
#endif
}

float sample_light_visibility(vec4 position, uint light_index, float N_dot_L)
{
#if defined(ERHE_SHADOW_MAP_NOPE)

#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2DArrayShadow s_shadow_compare = sampler2DArrayShadow(light_block.shadow_texture);
#endif

    //Light light = light_block.lights[light_index];
    Light light = light_block.lights[1];
    vec4  position_in_light_texture_homogeneous = light.texture_from_world * position;

    vec4  position_in_light_texture = position_in_light_texture_homogeneous / position_in_light_texture_homogeneous.w;
    float depth_in_light_texture    = position_in_light_texture.z;
    //float sampled_depth = texture(
    //    s_shadow,
    //    vec3(
    //        position_in_light_texture.xy,
    //        float(light_index)
    //    )
    //).x;

    float bias = 0.005 * sqrt(1.0 - N_dot_L * N_dot_L) / N_dot_L; // tan(acos(N_dot_L))
    bias = clamp(bias, 0.0, 0.01);
    if (camera.cameras[0].clip_depth_direction < 0.0) {
        if (depth_in_light_texture + bias >= sampled_depth) { // reverse depth
            return 1.0;
        }
    } else {
        if (depth_in_light_texture - bias <= sampled_depth) { // forward depth
            return 1.0;
        }
    }
    return 0.0;
#else
    return 1.0;
#endif
}

float get_range_attenuation(float range, float distance)
{
    if (range <= 0.0) {
        return 1.0 / pow(distance, 2.0); // negative range means unlimited
    }
    return max(min(1.0 - pow(distance / range, 4.0), 1.0), 0.0) / pow(distance, 2.0);
}

float get_spot_attenuation(vec3 point_to_light, vec3 spot_direction, float outer_cone_coss, float inner_cone_cos)
{
    float actual_cos = dot(normalize(spot_direction), normalize(-point_to_light));
    if (actual_cos > outer_cone_coss) {
        if (actual_cos < inner_cone_cos) {
            return smoothstep(outer_cone_coss, inner_cone_cos, actual_cos);
        }
        return 1.0;
    }
    return 0.0;
}

float clamped_dot(vec3 x, vec3 y)
{
    return clamp(dot(x, y), 0.001, 1.0);
}

float heaviside(float v)
{
    if (v > 0.0) {
        return 1.0;
    } else {
        return 0.0;
    }
}

vec3 brdf(vec3 base_color, float roughness, float metalness, vec3 L, vec3 V, vec3 N)
{
    vec3  H       = normalize(L + V);
    float N_dot_L = clamped_dot(N, L);
    float N_dot_V = clamped_dot(N, V);
    float N_dot_H = clamped_dot(N, H);
    float V_dot_H = clamped_dot(V, H); // Note: H.L == L.H == H.V == V.H
    float N_dot_H_squared   = N_dot_H * N_dot_H;
    float N_dot_L_squared   = N_dot_L * N_dot_L;
    float N_dot_V_squared   = N_dot_V * N_dot_V;
    float alpha             = roughness * roughness;
    float a2                = alpha * alpha;
    float one_minus_a2      = 1.0 - a2;
    float d_denom           = N_dot_H * N_dot_H * (a2 - 1.0) + 1.0;
    float distribution      = a2 * heaviside(N_dot_H) / (m_pi * d_denom * d_denom);
    float V_denominator_l   = N_dot_L + sqrt(a2 + one_minus_a2 * N_dot_L_squared);
    float V_denominator_v   = N_dot_V + sqrt(a2 + one_minus_a2 * N_dot_V_squared);
    float visibility        = heaviside(V_dot_H) / (V_denominator_l * V_denominator_v);
    float specular_brdf     = N_dot_L * visibility * distribution;
    vec3  diffuse_brdf      = N_dot_L * base_color * m_i_pi;
    float fresnel           = pow(1.0 - abs(V_dot_H), 5.0);
    vec3  conductor_fresnel = specular_brdf * (base_color + (1.0 - base_color) * fresnel);
    float f0                = 0.04;
    float fr                = f0 + (1.0 - f0) * fresnel;
    vec3  fresnel_mix       = mix(diffuse_brdf, vec3(specular_brdf), fr);
    return mix(fresnel_mix, conductor_fresnel, metalness);
}

void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3  V       = normalize(view_position_in_world - v_position.xyz);
    vec3  N       = normalize(v_normal);
    float N_dot_V = clamped_dot(N, V);

    Material material = material.materials[v_material_index];

    uvec2 base_color_texture         = material.base_color_texture;
    uvec2 metallic_roughness_texture = material.metallic_roughness_texture;
    vec3  base_color                 = material.base_color.rgb * sample_texture(base_color_texture, v_texcoord).rgb;
    uint  directional_light_count    = light_block.directional_light_count;
    uint  spot_light_count           = light_block.spot_light_count;
    uint  point_light_count          = light_block.point_light_count;
    uint  directional_light_offset   = 0;
    uint  spot_light_offset          = directional_light_count;
    uint  point_light_offset         = spot_light_offset + spot_light_count;

    vec3 color = vec3(0);
    color += light_block.ambient_light.rgb * base_color;
    color += material.emissive.rgb;

    for (uint i = 0; i < directional_light_count; ++i) {
        uint  light_index    = directional_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
        vec3  L              = normalize(point_to_light);   // Direction from surface point to light
        float N_dot_L        = clamped_dot(N, L);
        if (N_dot_L > 0.0 || N_dot_V > 0.0) {
            vec3 intensity = light.radiance_and_range.rgb * sample_light_visibility(v_position, light_index, N_dot_L);
            color += intensity * brdf(base_color, material.roughness.x, material.metallic, L, V, N);
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
            color += intensity * brdf(base_color, material.roughness.x, material.metallic, L, V, N);
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
            color += intensity * brdf(base_color, material.roughness.x, material.metallic, L, V, N);
        }
    }

    float exposure = camera.cameras[0].exposure;
    out_color.rgb = color * exposure * material.opacity;
    out_color.a = material.opacity;
}
