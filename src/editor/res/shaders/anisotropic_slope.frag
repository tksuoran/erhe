in vec2      v_texcoord;
in vec4      v_position;
in vec4      v_color;
in mat3      v_TBN;
in flat uint v_material_index;

const uint  max_u32 = 4294967295u;

vec4 sample_texture(uvec2 texture_handle, vec2 texcoord)
{
    if (texture_handle.x == max_u32) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    }
#if defined(ERHE_BINDLESS_TEXTURE)
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
#if defined(ERHE_BINDLESS_TEXTURE)
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
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return textureSize(s_texture, 0);
#else
    return textureSize(s_texture[texture_handle.x], 0);
#endif
}

float sample_light_visibility(vec4  position, uint  light_index, float N_dot_L)
{
#if defined(ERHE_SHADOW_MAPS)
    if (light_block.shadow_texture.x == max_u32) {
        return 1.0;
    }

#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2DArray s_shadow = sampler2DArray(light_block.shadow_texture);
#endif

    Light light = light_block.lights[light_index];
    vec4  position_in_light_texture_homogeneous = light.texture_from_world * position;

    vec4  position_in_light_texture = position_in_light_texture_homogeneous / position_in_light_texture_homogeneous.w;
    float depth_in_light_texture    = position_in_light_texture.z;
    float sampled_depth = texture(
        s_shadow,
        vec3(
            position_in_light_texture.xy,
            float(light_index)
        )
    ).x;

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

const float m_pi   = 3.1415926535897932384626434;
const float m_i_pi = 0.3183098861837906715377675;

// https://www.shadertoy.com/view/NtlyWX

float cos_theta  (vec3 w) {return w.z; }
float cos_2_theta(vec3 w) {return w.z * w.z; }
float sin_2_theta(vec3 w) {return max(0.0, 1.0 - cos_2_theta(w)); }
float sin_theta  (vec3 w) {return sqrt(sin_2_theta(w)); }
float tan_theta  (vec3 w) {return sin_theta(w) / cos_theta(w); }
float cos_phi    (vec3 w) {return (sin_theta(w) == 0.0) ? 1.0 : clamp(w.x / sin_theta(w), -1.0, 1.0); }
float sin_phi    (vec3 w) {return (sin_theta(w) == 0.0) ? 0.0 : clamp(w.y / sin_theta(w), -1.0, 1.0); }
float cos_2_phi  (vec3 w) {return cos_phi(w) * cos_phi(w); }
float sin_2_phi  (vec3 w) {return sin_phi(w) * sin_phi(w); }

float p22_ggx_isotropic(float x, float y, float alpha)
{
    float x_sqr     = x * x;
    float y_sqr     = y * y;
    float alpha_sqr = alpha * alpha;
    float denom     = 1.0 + (x_sqr / alpha_sqr) + (y_sqr / alpha_sqr);
    return 1.0 / ((m_pi * alpha_sqr) * (denom * denom));
}

float p22_ggx_anisotropic(float x, float y, float alpha_x, float alpha_y)
{
    float x_sqr       = x * x;
    float y_sqr       = y * y;
    float alpha_x_sqr = alpha_x * alpha_x;
    float alpha_y_sqr = alpha_y * alpha_y;
    float denom       = 1.0 + (x_sqr / alpha_x_sqr) + (y_sqr / alpha_y_sqr);
    float denom_sqr   = denom * denom;
    return 1.0 / ((m_pi * alpha_x * alpha_y) * denom_sqr);
}

float slope_ndf_ggx_isotropic(vec3 omega_h, float alpha)
{
    float slope_x     = -(omega_h.x / omega_h.z);
    float slope_y     = -(omega_h.y / omega_h.z);
    float cos_theta   = cos_theta(omega_h);
    float cos_2_theta = cos_theta * cos_theta;
    float cos_4_theta = cos_2_theta * cos_2_theta;
    float ggx_p22     = p22_ggx_isotropic(slope_x, slope_y, alpha);
    return ggx_p22 / cos_4_theta;
}

float slope_ndf_ggx_anisotropic(vec3 omega_h, float alpha_x, float alpha_y)
{
    float slope_x     = -(omega_h.x / omega_h.z);
    float slope_y     = -(omega_h.y / omega_h.z);
    float cos_theta   = cos_theta(omega_h);
    float cos_2_theta = cos_theta * cos_theta;
    float cos_4_theta = cos_2_theta * cos_2_theta;
    float ggx_p22     = p22_ggx_anisotropic(slope_x, slope_y, alpha_x, alpha_y);
    return ggx_p22 / cos_4_theta;
}

float lambda_ggx_isotropic(vec3 omega, float alpha)
{
    float a = 1.0 / (alpha * tan_theta(omega));
    return ( (-1.0 + sqrt(1.0 + 1.0 / (a * a))) * 0.5);
    //return 0.5 * (-1.0 + sqrt(1.0 + 1.0 / (a * a)));
}

float lambda_ggx_anisotropic(vec3 omega, float alpha_x, float alpha_y)
{
    float cos_phi = cos_phi(omega);
    float sin_phi = sin_phi(omega);
    float alpha_o = sqrt(cos_phi * cos_phi * alpha_x * alpha_x + sin_phi * sin_phi * alpha_y * alpha_y);
    float a       = 1.0 / (alpha_o * tan_theta(omega));
    return 0.5 * (-1.0 + sqrt(1.0 + 1.0 / (a * a)));
}

float ggx_isotropic_ndf(float N_dot_H, float alpha)
{
    float a = N_dot_H * alpha;
    float k = alpha / (1.0 - N_dot_H * N_dot_H + a * a);
    return k * k * m_i_pi;
}

float ggx_anisotropic_ndf(
    float alpha_t,
    float alpha_b,
    float T_dot_H,
    float B_dot_H,
    float N_dot_H
)
{
    vec3 v = vec3(
        alpha_b * T_dot_H,
        alpha_t * B_dot_H,
        alpha_t * alpha_b * N_dot_H
    );
    float v2 = dot(v, v);
    float a2 = alpha_t * alpha_b;
    float w2 = a2 / v2;
    return a2 * w2 * w2 * m_i_pi;
}

float ggx_isotropic_visibility(float N_dot_V, float N_dot_L, float alpha)
{
    float a2 = alpha * alpha;
    float GV = N_dot_L * sqrt(N_dot_V * N_dot_V * (1.0 - a2) + a2);
    float GL = N_dot_V * sqrt(N_dot_L * N_dot_L * (1.0 - a2) + a2);
    return 0.5 / (GV + GL);
}

float ggx_anisotropic_visibility(
    float alpha_t,
    float alpha_b,
	float T_dot_V,
    float B_dot_V,
    float N_dot_V,
    float T_dot_L,
    float B_dot_L,
    float N_dot_L
)
{
    float lambda_V = N_dot_L * length(vec3(alpha_t * T_dot_V, alpha_b * B_dot_V, N_dot_V));
    float lambda_L = N_dot_V * length(vec3(alpha_t * T_dot_L, alpha_b * B_dot_L, N_dot_L));
    float v = 0.5 / (lambda_V + lambda_L);
    return clamp(v, 0.0, 1.0);
}

vec3 fresnel_schlick(float cos_theta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

float clamped_dot(vec3 x, vec3 y) {
    return clamp(dot(x, y), 0.001, 1.0);
}

// This shader is adapted from https://www.shadertoy.com/view/3tyXRt - Arthur Cavalier
vec3 slope_brdf(
    vec3  base_color,
    float roughness_x,
    float roughness_y,
    float metallic,
    float reflectance,
    mat3  TBN_t,
    vec3  L,
    vec3  V,
    vec3  T,
    vec3  B,
    vec3  N
)
{
    float alpha_x   = roughness_x * roughness_x;
    float alpha_y   = roughness_y * roughness_y;
    vec3  F0        = 0.16 * reflectance * reflectance * (1.0 - metallic) + base_color * metallic;
    vec3  wo        = normalize(TBN_t * V);
    vec3  wg        = normalize(TBN_t * N);
    vec3  wi        = normalize(TBN_t * L);
    vec3  wh        = normalize(wo + wi);
    float wi_dot_wh = clamp(dot(wi, wh), 0.0, 1.0);
    float wg_dot_wi = clamp(cos_theta(wi), 0.0, 1.0);
    float lambda_wo = lambda_ggx_anisotropic(wo, alpha_x, alpha_y);
    float lambda_wi = lambda_ggx_anisotropic(wi, alpha_x, alpha_y);
    float D         = slope_ndf_ggx_anisotropic(wh, alpha_x, alpha_y);
    //float lambda_wo = lambda_ggx_isotropic(wo, alpha_x);
    //float lambda_wi = lambda_ggx_isotropic(wi, alpha_x);
    //float D         = slope_ndf_ggx_isotropic(wh, alpha_x);
    float G         = 1.0 / (1.0 + lambda_wo + lambda_wi);
    vec3  F         = fresnel_schlick(wi_dot_wh, F0);
    vec3  specular_microfacet = (D * F * G) / (4.0 * cos_theta(wi) * cos_theta(wo));
    vec3  diffuse_lambert     = m_i_pi * (1.0 - metallic) * base_color;
    vec3  diffuse_factor      = vec3(1.0) - F;

    return wg_dot_wi * (diffuse_factor * diffuse_lambert + specular_microfacet);
}

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
    mat3  TBN     = mat3(T, B, N);
    mat3  TBN_t   = transpose(TBN);
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
            color += intensity * slope_brdf(
                base_color,
                material.roughness.x,
                material.roughness.y,
                material.metallic,
                material.reflectance,
                TBN_t,
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
            float light_visibility  = 1.0; // TODO sample_light_visibility(v_position, light_index, N_dot_L);
            vec3  intensity         = range_attenuation * spot_attenuation * light.radiance_and_range.rgb * light_visibility;
            color += intensity * slope_brdf(
                base_color,
                material.roughness.x,
                material.roughness.y,
                material.metallic,
                material.reflectance,
                TBN_t,
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
            color += intensity * slope_brdf(
                base_color,
                material.roughness.x,
                material.roughness.y,
                material.metallic,
                material.reflectance,
                TBN_t,
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
