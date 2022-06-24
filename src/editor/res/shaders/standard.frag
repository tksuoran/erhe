in vec2      v_texcoord;
in vec4      v_position;
in vec4      v_color;
in mat3      v_TBN;
in flat uint v_material_index;
in float     v_tangent_scale;
in float     v_line_width;

const float m_pi   = 3.1415926535897932384626434;
const float m_i_pi = 0.3183098861837906715377675;

float cos_theta  (vec3 w) {return w.z; }
float cos_2_theta(vec3 w) {return w.z * w.z; }
float sin_2_theta(vec3 w) {return max(0.0, 1.0 - cos_2_theta(w)); }
float sin_theta  (vec3 w) {return sqrt(sin_2_theta(w)); }
float tan_theta  (vec3 w) {return sin_theta(w) / cos_theta(w); }
float cos_phi    (vec3 w) {return (sin_theta(w) == 0.0) ? 1.0 : clamp(w.x / sin_theta(w), -1.0, 1.0); }
float sin_phi    (vec3 w) {return (sin_theta(w) == 0.0) ? 0.0 : clamp(w.y / sin_theta(w), -1.0, 1.0); }
float cos_2_phi  (vec3 w) {return cos_phi(w) * cos_phi(w); }
float sin_2_phi  (vec3 w) {return sin_phi(w) * sin_phi(w); }

float p22_ggx_anisotropic(float x, float y, float alpha_x, float alpha_y)
{
    float x_sqr       = x * x;
    float y_sqr       = y * y;
    float alpha_x_sqr = alpha_x * alpha_x;
    float alpha_y_sqr = alpha_y * alpha_y;
    float denom       = (1.0 + (x_sqr / alpha_x_sqr) + (y_sqr / alpha_y_sqr));
    float denom_sqr   = denom * denom;
    return
        1.0
        /
        (
            (
                m_pi * alpha_x * alpha_y
            ) *
            denom_sqr
        );
}

float ndf_ggx_anisotropic(vec3 omega_h, float alpha_x, float alpha_y)
{
    float slope_x     = -(omega_h.x / omega_h.z);
    float slope_y     = -(omega_h.y / omega_h.z);
    float cos_theta   = cos_theta(omega_h);
    float cos_2_theta = cos_theta * cos_theta;
    float cos_4_theta = cos_2_theta * cos_2_theta;
    float ggx_p22     = p22_ggx_anisotropic(slope_x, slope_y, alpha_x, alpha_y);
    return ggx_p22 / cos_4_theta;
}

float lambda_ggx_anisotropic(vec3 omega, float alpha_x, float alpha_y)
{
    float cos_phi = cos_phi(omega);
    float sin_phi = sin_phi(omega);
    float alpha_o = sqrt(cos_phi * cos_phi * alpha_x * alpha_x + sin_phi * sin_phi * alpha_y * alpha_y);
    float a       = 1.0 / (alpha_o * tan_theta(omega));
    return 0.5 * (-1.0 + sqrt(1.0 + 1.0 / (a * a)));
}

vec3 fresnel_schlick(float wo_dot_wh, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - wo_dot_wh, 5.0);
}

float sample_light_visibility(
    vec4  position,
    uint  light_index,
    float N_dot_L
)
{
#if defined(ERHE_SHADOW_MAPS)

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
    if (camera.cameras[0].clip_depth_direction < 0.0)
    {
        if (depth_in_light_texture + bias >= sampled_depth) // reverse depth
        {
            return 1.0;
        }
    }
    else
    {
        if (depth_in_light_texture - bias <= sampled_depth) // forward depth
        {
            return 1.0;
        }
    }
    return 0.0;
#else
    return 1.0;
#endif
}

float srgb_to_linear(float x)
{
    if (x <= 0.04045)
    {
        return x / 12.92;
    }
    else
    {
        return pow((x + 0.055) / 1.055, 2.4);
    }
}

vec3 srgb_to_linear(vec3 V)
{
    return vec3(
        srgb_to_linear(V.r),
        srgb_to_linear(V.g),
        srgb_to_linear(V.b)
    );
}

vec2 srgb_to_linear(vec2 V)
{
    return vec2(
        srgb_to_linear(V.r),
        srgb_to_linear(V.g)
    );
}

const float M_PI = 3.141592653589793;

float clamped_dot(vec3 x, vec3 y)
{
    return clamp(dot(x, y), 0.0, 1.0);
}

float max3(vec3 V)
{
    return max(max(V.x, V.y), V.z);
}

vec3 F_Schlick(vec3 f0, vec3 f90, float V_dot_H)
{
    return f0 + (f90 - f0) * pow(clamp(1.0 - V_dot_H, 0.0, 1.0), 5.0);
}

float V_GGX(
    float N_dot_L,
    float N_dot_V,
    float alpha_roughness
)
{
    float alpha_roughness_sq = alpha_roughness * alpha_roughness;
    float GGX_V              = N_dot_L * sqrt(N_dot_V * N_dot_V * (1.0 - alpha_roughness_sq) + alpha_roughness_sq);
    float GGX_L              = N_dot_V * sqrt(N_dot_L * N_dot_L * (1.0 - alpha_roughness_sq) + alpha_roughness_sq);
    float GGX                = GGX_V + GGX_L;
    if (GGX > 0.0)
    {
        return 0.5 / GGX;
    }
    return 0.0;
}

float D_GGX(float N_dot_H, float alpha_roughness)
{
    float alpha_roughness_sq = alpha_roughness * alpha_roughness;
    float f                  = (N_dot_H * N_dot_H) * (alpha_roughness_sq - 1.0) + 1.0;
    return alpha_roughness_sq / (M_PI * f * f);
}

vec3 BRDF_specularGGX(vec3 f0, vec3 f90, float alphaRoughness, float VdotH, float NdotL, float NdotV, float NdotH)
{
    vec3   F   = F_Schlick(f0, f90, VdotH);
    float  Vis = V_GGX(NdotL, NdotV, alphaRoughness);
    float  D   = D_GGX(NdotH, alphaRoughness);
    return F * Vis * D;
}

vec3 BRDF_lambertian(vec3 f0, vec3 f90, vec3 diffuseColor, float V_dot_H)
{
    return (1.0 - F_Schlick(f0, f90, V_dot_H)) * (diffuseColor / M_PI);
}

float get_range_attenuation(float range, float distance)
{
    if (range <= 0.0)
    {
        return 1.0 / pow(distance, 2.0); // negative range means unlimited
    }
    return max(min(1.0 - pow(distance / range, 4.0), 1.0), 0.0) / pow(distance, 2.0);
}

float get_spot_attenuation(vec3 point_to_light, vec3 spot_direction, float outer_cone_coss, float inner_cone_cos)
{
    float actual_cos = dot(normalize(spot_direction), normalize(-point_to_light));
    if (actual_cos > outer_cone_coss)
    {
        if (actual_cos < inner_cone_cos)
        {
            return smoothstep(outer_cone_coss, inner_cone_cos, actual_cos);
        }
        return 1.0;
    }
    return 0.0;
}

void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3  V     = normalize(view_position_in_world - v_position.xyz);
    vec3  N     = normalize(v_TBN[2]);
    vec3  T0    = normalize(v_TBN[0]);
    vec3  B0    = normalize(v_TBN[1]);

    // Anisitropy direction
    vec2  T_    = normalize(v_texcoord);

    // Anisotropy strength:
    //  - 0.0 where r is 0.0
    //  - relative to texture coordinate magnitude, clamped from 0 to 1
    float anisotropy_texcoord = v_color.g;
    float texcoord_factor     = length(v_texcoord) * 8.0;
    float anisotropy_strength = mix(1.0, min(1.0, pow(texcoord_factor, 0.25)), anisotropy_texcoord) * v_color.r;

    // Anisotropic tangent and bitangent, mixed with non-aniso T and B based on vertex alpha aniso control
    vec3  T     = mix(T0, T_.x * T0 + T_.y * B0, anisotropy_texcoord);
    vec3  B     = mix(B0, T_.y * T0 - T_.x * B0, anisotropy_texcoord);

    mat3  TBN   = mat3(T, B, N);
    mat3  TBN_t = transpose(TBN);
    vec3  wo    = normalize(TBN_t * V);
    vec3  wg    = normalize(TBN_t * N); // ( should be (0,0,1)^T )

    float N_dot_V = clamped_dot(N, V);
    float f0_ior  = 0.04; // The default index of refraction of 1.5 yields a dielectric normal incidence reflectance of 0.04.
    vec3  f0_met  = vec3(f0_ior); // Achromatic f0 based on IOR.

    Material material = material.materials[v_material_index];

    //float roughness_x_ = material.roughness.x;
    //float roughness_y_ = material.roughness.y;
    float roughness    = 0.5 * material.roughness.x + 0.5 * material.roughness.y;
    float roughness_x = mix(roughness, material.roughness.x, anisotropy_strength);
    float roughness_y = mix(roughness, material.roughness.y, anisotropy_strength);
    float alpha_x     = roughness_x * roughness_x;
    float alpha_y     = roughness_y * roughness_y;

    vec3  m_base_color   = material.base_color.rgb;
    float m_metallic     = material.metallic;
    vec3  m_albedo_color = mix(m_base_color.rgb * (vec3(1.0) - f0_met), vec3(0), m_metallic);
    vec3  m_f0           = mix(f0_met, m_base_color.rgb, m_metallic);

    float reflectance    = max(max(m_f0.r, m_f0.g), m_f0.b);
    vec3  m_f90          = vec3(reflectance);

    uint  directional_light_count  = light_block.directional_light_count;
    uint  spot_light_count         = light_block.spot_light_count;
    uint  directional_light_offset = 0;
    uint  spot_light_offset        = directional_light_count;

    vec3 color = vec3(0);
    color += (0.5 + 0.5 * N.y) * light_block.ambient_light.rgb * m_base_color;
    color += material.emissive.rgb;

    for (uint i = 0; i < directional_light_count; ++i)
    {
        uint  light_index    = directional_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
        vec3  L              = normalize(point_to_light);   // Direction from surface point to light
        float N_dot_L        = clamped_dot(N, L);
        float N_dot_V        = clamped_dot(N, V);
        if (N_dot_L > 0.0 || N_dot_V > 0.0)
        {
            vec3  intensity = light.radiance_and_range.rgb * sample_light_visibility(v_position, light_index, N_dot_L);

#if defined(ERHE_SIMPLER_SHADERS)
            vec3  H= normalize(L + V);
            float N_dot_H   = clamped_dot(L, H);
            float L_dot_H   = clamped_dot(L, H);
            float V_dot_H   = clamped_dot(V, H);
            color += intensity * N_dot_L * BRDF_lambertian(m_f0, m_f90, m_albedo_color, V_dot_H);
            color += intensity * N_dot_L * BRDF_specularGGX(
                m_f0, m_f90, roughness,
                V_dot_H, N_dot_L, N_dot_V, N_dot_H
            );
#else

            vec3  wi        = normalize(TBN_t * L);
            vec3  wh        = normalize(wo + wi);   // ( could check it not zero)
            float wi_dot_wh = clamp(dot(wi, wh), 0.0, 1.0);     // saturate(dot(L,H))
            float wg_dot_wi = clamp(cos_theta(wi), 0.0, 1.0);   // saturate(dot(N,L))
            float lambda_wo = lambda_ggx_anisotropic(wo, alpha_x, alpha_y);
            float lambda_wi = lambda_ggx_anisotropic(wi, alpha_x, alpha_y);
            float D         = ndf_ggx_anisotropic(wh, alpha_x, alpha_y);
            float G         = 1.0 / (1.0 + lambda_wo + lambda_wi);
            vec3  F         = F_Schlick(m_f0, m_f90, wi_dot_wh);

            color += intensity * wg_dot_wi * BRDF_lambertian(m_f0, m_f90, m_albedo_color, wi_dot_wh);
            color += max(
                wg_dot_wi * intensity * (D * F * G) / (4.0 * cos_theta(wi) * cos_theta(wo)),
                vec3(0.0, 0.0, 0.0)
            );
#endif
        }
    }

    for (uint i = 0; i < spot_light_count; ++i)
    {
        uint  light_index    = spot_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
        vec3  L              = normalize(point_to_light);
        float N_dot_L        = clamped_dot(N, L);
        float N_dot_V        = clamped_dot(N, V);
        if (N_dot_L > 0.0 || N_dot_V > 0.0)
        {
            float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));

            float spot_attenuation  = get_spot_attenuation(-point_to_light, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
            float light_visibility  = sample_light_visibility(v_position, light_index, N_dot_L);
            vec3  intensity         = range_attenuation * spot_attenuation * light.radiance_and_range.rgb * light_visibility;

#if defined(ERHE_SIMPLER_SHADERS)
            vec3  H       = normalize(L + V);
            float N_dot_H = clamped_dot(N, H);
            float L_dot_H = clamped_dot(L, H);
            float V_dot_H = clamped_dot(V, H);
            color += intensity * N_dot_L * BRDF_lambertian(m_f0, m_f90, m_albedo_color, V_dot_H);
            color += intensity * N_dot_L * BRDF_specularGGX(
                m_f0, m_f90, roughness,
                V_dot_H, N_dot_L, N_dot_V, N_dot_H
            );
#else
            vec3  wi        = normalize(TBN_t * L);
            vec3  wh        = normalize(wo + wi);
            float wi_dot_wh = clamp(dot(wi, wh), 0.0, 1.0);
            float wg_dot_wi = clamp(cos_theta(wi), 0.0, 1.0);
            float lambda_wo = lambda_ggx_anisotropic(wo, alpha_x, alpha_y);
            float lambda_wi = lambda_ggx_anisotropic(wi, alpha_x, alpha_y);
            float D         = ndf_ggx_anisotropic(wh, alpha_x, alpha_y);
            float G         = 1.0 / (1.0 + lambda_wo + lambda_wi);
            vec3  F         = F_Schlick(m_f0, m_f90, wi_dot_wh);

            color += intensity * N_dot_L * BRDF_lambertian(m_f0, m_f90, m_albedo_color, wi_dot_wh);
            color += max(
                wg_dot_wi * intensity * (D * F * G) / (4.0 * cos_theta(wi) * cos_theta(wo)),
                vec3(0.0, 0.0, 0.0)
            );
#endif
       }
    }

    float exposure = camera.cameras[0].exposure;
    out_color.rgb = color * exposure;

    out_color.a = 1.0;
}
