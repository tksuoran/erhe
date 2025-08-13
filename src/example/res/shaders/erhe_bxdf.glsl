#ifndef ERHE_BXDF_GLSL
#define ERHE_BXDF_GLSL

#include "erhe_math.glsl" // clamped_dot(), heaviside()
#include "erhe_ggx.glsl"

// NOTE: Caller *must* ensure cos_theta is >= 0.0 and <= 1.0
vec3 fresnel_schlick(float cos_theta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

// https://www.shadertoy.com/view/WssyR7
// 2016 - Filtering Distributions of Normals for Shading Antialiasing
//        by A. Kaplanyan, S. Hill, A. Patney, A. Lefohn (HPG 2016)
//        project page : https://research.nvidia.com/publication/filtering-distributions-normals-shading-antialiasing
//        demo : https://blog.selfshadow.com/sandbox/specaa.html
// 2017 - Error Reduction and Simplification for Shading Anti-Aliasing
//        by Y. Tokuyoshi (technical report)
//        project page : http://www.jp.square-enix.com/tech/publications.html
//-----------------------------------------------------------------------------
//-- Specular AA --------------------------------------------------------------
//-- Snippet code for specular antialiasing
//-- Based on A. Kaplanyan & Y. Tokuyoshi work
//-----------------------------------------------------------------------------
void specular_anti_aliasing(in vec3 half_vector, inout float alpha_x, inout float alpha_y) {
    float sigma                  = 0.50; //- screen space variance
    float Kappa                  = 0.18; //- clamping treshold
    vec2  H                      = half_vector.xy;
    vec2  footprint_bounding_box = fwidth(H);//- abs(dfdx(slope_h)) + abs(dfdy(slope_h))
    vec2  variance               = sigma * sigma * footprint_bounding_box * footprint_bounding_box;
    vec2  kernel_roughness       = min(vec2(Kappa), 2.0 * variance); 
    alpha_x = sqrt(alpha_x * alpha_x + kernel_roughness.x);
    alpha_y = sqrt(alpha_y * alpha_y + kernel_roughness.y);
}

// Based on https://www.shadertoy.com/view/flsyWX by Arthur Cavalier
vec3 isotropic_brdf(
    vec3  base_color,
    float roughness,
    float metalness,
    float reflectance,
    vec3  L,
    vec3  V,
    vec3  N
) {
    vec3  H       = normalize(L + V);
    float N_dot_L = clamped_dot(N, L);
    float N_dot_V = clamped_dot(N, V);
    float N_dot_H = dot(N, H);
    float V_dot_H = dot(V, H); // Note: H.L == L.H == H.V == V.H
    float alpha   = roughness * roughness;

    // Computing Normal Distribution Term
    float D   = ggx_isotropic_ndf(N_dot_H, alpha);

    // Computing Visibility Term
    float Vis = ggx_isotropic_visibility(N_dot_V, N_dot_L, alpha); 

    // Computing Fresnel Term
    // Using reflectance mapping as in Filament PBR Pipeline
    vec3  F0  = 0.16 * reflectance * reflectance * (1.0 - metalness) + base_color * metalness;
    vec3  F   = fresnel_schlick(clamp(V_dot_H, 0.0, 1.0), F0);

    // Lighting
    vec3 specular_microfacet = D * Vis * F;
    vec3 diffuse_lambert     = m_i_pi * (1.0 - metalness) * base_color;
    vec3 diffuse_factor      = vec3(1.0) - F; 
            
    // Final color
    return max(N_dot_L, 0.0) * (diffuse_factor * diffuse_lambert + specular_microfacet);
}

vec3 anisotropic_brdf(
    vec3  base_color,
    float roughness_x,
    float roughness_y,
    float metalness,
    float reflectance,
    vec3  L,
    vec3  V,
    vec3  T,
    vec3  B,
    vec3  N
) {
    mat3  TBN     = mat3(T, B, N);
    mat3  TBN_t   = transpose(TBN);
    vec3  wo      = normalize(TBN_t * V);
    vec3  wg      = normalize(TBN_t * N);
    vec3  wi      = normalize(TBN_t * L);
    vec3  wh      = normalize(wo + wi);

    float alpha_x = roughness_x * roughness_x;
    float alpha_y = roughness_y * roughness_y;
    //specular_anti_aliasing(wh, alpha_x, alpha_y);

    vec3  H       = normalize(L + V);
    float N_dot_H = dot(N, H);
    float N_dot_V = clamped_dot(N, V);
    float N_dot_L = clamped_dot(N, L);
    float T_dot_V = dot(T, V);
    float B_dot_V = dot(B, V);
    float T_dot_L = dot(T, L);
    float B_dot_L = dot(B, L);
    float T_dot_H = dot(T, H);
    float B_dot_H = dot(B, H);
    float V_dot_H = clamped_dot(V, H); // Note: H.L == L.H == H.V == V.H

    // Computing Normal Distribution Term
    float D   = ggx_anisotropic_ndf(alpha_x, alpha_y, T_dot_H, B_dot_H, N_dot_H);
        
    // Computing Visibility Term
    float Vis = ggx_anisotropic_visibility(alpha_x, alpha_y, T_dot_V, B_dot_V, N_dot_V, T_dot_L, B_dot_L, N_dot_L); 
        
    // Computing Fresnel Term 
    vec3  F0  = 0.16 * reflectance * reflectance * (1.0 - metalness) + base_color * metalness;
    vec3  F   = fresnel_schlick(clamp(V_dot_H, 0.0, 1.0), F0);

    // Lighting
    vec3 specular_microfacet = D * Vis * F;
    vec3 diffuse_lambert     = m_i_pi * (1.0 - metalness) * base_color;
    vec3 diffuse_factor      = vec3(1.0) - F; 

    // Final color
    return max(N_dot_L, 0.0) * (diffuse_factor * diffuse_lambert + specular_microfacet);
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
) {
    float alpha_x   = roughness_x * roughness_x;
    float alpha_y   = roughness_y * roughness_y;
    vec3  F0        = 0.16 * reflectance * reflectance * (1.0 - metallic) + base_color * metallic;
    vec3  wo        = normalize(TBN_t * V);
    vec3  wg        = normalize(TBN_t * N);
    vec3  wi        = normalize(TBN_t * L);
    vec3  wh        = normalize(wo + wi);
    float wi_dot_wh = clamp(  dot(wi, wh), 0.0, 1.0);
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

#endif // ERHE_BXDF_GLSL
