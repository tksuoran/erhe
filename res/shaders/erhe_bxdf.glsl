#ifndef ERHE_BXDF_GLSL
#define ERHE_BXDF_GLSL

#include "erhe_math.glsl" // clamped_dot(), heaviside()
#include "erhe_ggx.glsl"
#include "erhe_fp16_paths.glsl" // brdf_float / brdf_vec3

// NOTE: Caller *must* ensure cos_theta is >= 0.0 and <= 1.0
vec3 fresnel_schlick(float cos_theta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

// Specular anti-aliasing (doc/erhe/scene_renderer.md "Specular
// anti-aliasing"): a kernel roughness added to alpha^2 of anisotropic_brdf()
// filters the specular lobe over the pixel footprint.
// https://www.shadertoy.com/view/WssyR7
// 2016 - Filtering Distributions of Normals for Shading Antialiasing
//        by A. Kaplanyan, S. Hill, A. Patney, A. Lefohn (HPG 2016)
//        project page : https://research.nvidia.com/publication/filtering-distributions-normals-shading-antialiasing
//        demo : https://blog.selfshadow.com/sandbox/specaa.html
// 2017 - Error Reduction and Simplification for Shading Anti-Aliasing
//        by Y. Tokuyoshi (technical report)
//        project page : http://www.jp.square-enix.com/tech/publications.html
//
// The mode is the ERHE_SPECULAR_ANTI_ALIASING compile-time variant axis
// (erhe::scene_renderer::Specular_anti_aliasing); only the selected mode's
// code exists in a variant:
//   OFF               - no kernel.
//   NORMAL_KERNEL     - from the screen space derivatives of the shading
//                       normal (after normal mapping), once per fragment.
//                       The Tokuyoshi-Kaplanyan kernel; Filament evaluates
//                       it on the geometric normal, this uses the shading
//                       normal so that normal map detail finer than a pixel
//                       is filtered too (the variance it measures is the
//                       one the BRDF actually sees).
//   FINITE_DIFFERENCE - from the change of the shading frame half vector
//                       toward the x and y neighbours, per light; the
//                       neighbours' shading inputs come from derivatives
//                       taken once per fragment, so no derivative is taken
//                       inside the light loops (whose bodies are non-uniform
//                       control flow).
// Shaders that do not define the axis (compute shaders: no derivatives) get
// OFF and pass a zero kernel.
#define ERHE_SPECULAR_ANTI_ALIASING_OFF               0
#define ERHE_SPECULAR_ANTI_ALIASING_NORMAL_KERNEL     1
#define ERHE_SPECULAR_ANTI_ALIASING_FINITE_DIFFERENCE 2
#if !defined(ERHE_SPECULAR_ANTI_ALIASING)
#  define ERHE_SPECULAR_ANTI_ALIASING ERHE_SPECULAR_ANTI_ALIASING_OFF
#endif

// sigma is the standard deviation of the screen space pixel filter (the
// variance is sigma^2); Kappa clamps the kernel roughness. Both forms (the
// half vector footprint and the normal derivatives) use the same pair: both
// estimate the slope variance over the pixel footprint. They are not equal
// estimates - the half vector form squares an L1 footprint (|d/dx| + |d/dy|)
// while the normal form sums the squared derivatives, so for the same
// slope change they can differ by up to a factor of 2 - and the constants
// are kept shared rather than tuned per form.
const float c_specular_aa_sigma = 0.50;
const float c_specular_aa_kappa = 0.18;

// Kernel roughness from the footprint of the half vector's xy slopes
// (|d/dx| + |d/dy|, what fwidth() gives).
vec2 specular_aa_kernel_from_footprint(vec2 footprint)
{
    vec2 variance = c_specular_aa_sigma * c_specular_aa_sigma * footprint * footprint;
    return min(vec2(c_specular_aa_kappa), 2.0 * variance);
}

// Kernel roughness from the shading normal's screen space derivatives,
// projected on the tangent (x) and bitangent (y) axes.
vec2 specular_aa_kernel_from_normal_derivatives(vec3 dNdx, vec3 dNdy, vec3 T, vec3 B)
{
    float dx_T     = dot(dNdx, T);
    float dy_T     = dot(dNdy, T);
    float dx_B     = dot(dNdx, B);
    float dy_B     = dot(dNdy, B);
    vec2  variance = c_specular_aa_sigma * c_specular_aa_sigma * vec2(
        (dx_T * dx_T) + (dy_T * dy_T),
        (dx_B * dx_B) + (dy_B * dy_B)
    );
    return min(vec2(c_specular_aa_kappa), 2.0 * variance);
}

// The half vector in the shading frame, as anisotropic_brdf() builds it.
vec3 shading_frame_half_vector(mat3 TBN_t, vec3 V, vec3 L)
{
    vec3 wo = normalize(TBN_t * V);
    vec3 wi = normalize(TBN_t * L);
    return normalize(wo + wi);
}

#if (ERHE_SPECULAR_ANTI_ALIASING == ERHE_SPECULAR_ANTI_ALIASING_FINITE_DIFFERENCE) && defined(ERHE_FRAGMENT_SHADER)
// Fine derivatives where available: a coarse derivative may give the
// second row of a quad the first row's x difference, and value plus that
// difference is then not the neighbouring pixel's value.
#  if __VERSION__ >= 450
#    define ERHE_SPECULAR_AA_DFDX dFdxFine
#    define ERHE_SPECULAR_AA_DFDY dFdyFine
#  else
#    define ERHE_SPECULAR_AA_DFDX dFdx
#    define ERHE_SPECULAR_AA_DFDY dFdy
#  endif

// Per fragment state of the finite difference kernel: the x / y
// neighbours' shading frame, view direction (in their shading frame) and
// position offset (neighbour minus this fragment).
struct Specular_aa_fragment
{
    vec3 wo;       // this fragment's view direction in its shading frame
    mat3 TBN_t_dx;
    mat3 TBN_t_dy;
    vec3 wo_dx;
    vec3 wo_dy;
    vec3 dpdx;
    vec3 dpdy;
};

// Must be called in uniform control flow. TBN_t is this fragment's shading
// frame (transposed), V its view direction. position_to_view is the vector
// from the fragment to the eye (unnormalized), position_for_derivatives any
// position whose screen space derivatives are the fragment position's (a
// view relative position keeps more precision than the world position).
Specular_aa_fragment specular_aa_begin_fragment(
    mat3 TBN_t,
    vec3 V,
    vec3 position_for_derivatives,
    vec3 position_to_view,
    vec3 T,
    vec3 B,
    vec3 N
)
{
    // The neighbours' shading inputs: the other pixel of this pixel's quad
    // pair in x (and in y) has value + derivative when this pixel is the
    // pair's first, value - derivative when it is the second, so both pixels
    // of a pair difference the same two pixels, as fwidth() of the half
    // vector would. Which one this pixel is follows from the derivative of
    // its own column (row) parity, so no quad alignment is assumed.
    float parity_x = fract(gl_FragCoord.x * 0.5);
    float parity_y = fract(gl_FragCoord.y * 0.5);
    float step_x   = ((parity_x < 0.5) == (ERHE_SPECULAR_AA_DFDX(parity_x) > 0.0)) ? 1.0 : -1.0;
    float step_y   = ((parity_y < 0.5) == (ERHE_SPECULAR_AA_DFDY(parity_y) > 0.0)) ? 1.0 : -1.0;
    vec3 T_dx = T + step_x * ERHE_SPECULAR_AA_DFDX(T);
    vec3 T_dy = T + step_y * ERHE_SPECULAR_AA_DFDY(T);
    vec3 B_dx = B + step_x * ERHE_SPECULAR_AA_DFDX(B);
    vec3 B_dy = B + step_y * ERHE_SPECULAR_AA_DFDY(B);
    vec3 N_dx = N + step_x * ERHE_SPECULAR_AA_DFDX(N);
    vec3 N_dy = N + step_y * ERHE_SPECULAR_AA_DFDY(N);
    Specular_aa_fragment result;
    result.wo       = normalize(TBN_t * V);
    result.dpdx     = step_x * ERHE_SPECULAR_AA_DFDX(position_for_derivatives);
    result.dpdy     = step_y * ERHE_SPECULAR_AA_DFDY(position_for_derivatives);
    result.TBN_t_dx = transpose(mat3(T_dx, B_dx, N_dx));
    result.TBN_t_dy = transpose(mat3(T_dy, B_dy, N_dy));
    result.wo_dx    = normalize(result.TBN_t_dx * (position_to_view - result.dpdx));
    result.wo_dy    = normalize(result.TBN_t_dy * (position_to_view - result.dpdy));
    return result;
}

// The kernel roughness for one light. point_to_light is the vector from the
// fragment to the light (the light direction for a directional light),
// positional 1.0 for a spot or point light (the vector changes with the
// fragment position), 0.0 for a directional light. No derivatives: safe in
// non-uniform control flow.
vec2 specular_aa_light_kernel(
    Specular_aa_fragment fragment,
    mat3                 TBN_t,
    vec3                 L,
    vec3                 point_to_light,
    float                positional
)
{
    vec3 wh    = normalize(fragment.wo + normalize(TBN_t * L));
    vec3 wi_dx = normalize(fragment.TBN_t_dx * (point_to_light - positional * fragment.dpdx));
    vec3 wi_dy = normalize(fragment.TBN_t_dy * (point_to_light - positional * fragment.dpdy));
    vec3 wh_dx = normalize(fragment.wo_dx + wi_dx);
    vec3 wh_dy = normalize(fragment.wo_dy + wi_dy);
    vec2 footprint = abs(wh_dx.xy - wh.xy) + abs(wh_dy.xy - wh.xy);
    return specular_aa_kernel_from_footprint(footprint);
}
#endif

// Based on https://www.shadertoy.com/view/flsyWX by Arthur Cavalier
//
// Written in the BRDF path's precision (brdf_float / brdf_vec3 of
// erhe_fp16_paths.glsl, the ERHE_FP16_PATH_BRDF bit; float16_t when the
// path is live, float otherwise, in which case this is the original fp32
// code operation for operation). The fp16 path (doc/plans/shader_fp16.md
// P2) differs in three places, all under ERHE_FP16_BRDF:
//   - the vectors L, V, N and H stay fp32 and only their dot products cross
//     into fp16: a unit vector quantized to fp16 carries an angular error of
//     2^-11 radians, which at the flank of a sharp highlight (alpha ~ 0.01)
//     is a 10 - 70% error of the distribution (measured, the all-fp16 first
//     form); the dot products themselves carry one fp16 rounding;
//   - the GGX distribution takes sin^2 of the half angle as |cross(N, H)|^2
//     (in fp32) instead of 1 - N.H^2, and its denominator normalized by
//     alpha^2 so that every fp16 term is a normal number: near the peak
//     N.H is within one fp16 ulp of 1 and 1 - N.H^2 would collapse to a
//     few bands, and sin^2 itself is below the fp16 normal range there.
//     The bound is the peak value 1 / (pi alpha^2) <= 65504, roughness
//     >= 0.064; the P1 roughness floor 0.09 keeps a margin;
//   - the visibility denominator floor is the smallest normal fp16 number
//     instead of 1e-8 (a denormal fp16 that flushes to zero on some drivers
//     and would divide by zero).
brdf_vec3 isotropic_brdf(
    brdf_vec3  base_color,
    brdf_float roughness,
    brdf_float metalness,
    brdf_float reflectance,
    vec3       L,
    vec3       V,
    vec3       N
) {
    vec3       H       = normalize(L + V);
    brdf_float N_dot_L = brdf_float(clamped_dot(N, L));
    brdf_float N_dot_V = brdf_float(clamped_dot(N, V));
    brdf_float N_dot_H = brdf_float(dot(N, H));
    brdf_float V_dot_H = brdf_float(dot(V, H)); // Note: H.L == L.H == H.V == V.H
    brdf_float alpha   = roughness * roughness;

    // Computing Normal Distribution Term
    brdf_float D;
    {
#if defined(ERHE_FP16_BRDF)
        // The denominator sin^2 + (N.H alpha)^2 normalized by alpha^2:
        // sin^2 / alpha^2 + N.H^2, at least N.H^2 ~ 1, so both terms are
        // normal fp16 numbers (unnormalized, sin^2 below 2^-14 flushed to
        // zero at the peak of a sharp highlight and raised it by up to 75%,
        // measured). The ratio is formed in fp32 (sin^2 is fp32 above).
        // k = alpha / denominator = 1 / (alpha (sin^2 / alpha^2 + N.H^2)).
        vec3       N_x_H   = cross(N, H);
        float      alpha_f = float(alpha);
        brdf_float denom_n = brdf_float(dot(N_x_H, N_x_H) / (alpha_f * alpha_f)) + N_dot_H * N_dot_H;
        brdf_float k       = brdf_float(1.0) / (alpha * denom_n);
#else
        brdf_float a     = N_dot_H * alpha;
        brdf_float denom = max(brdf_float(1.0) - N_dot_H * N_dot_H + a * a, brdf_float(1e-7));
        brdf_float k     = alpha / denom;
#endif
        D = k * k * brdf_float(m_i_pi);
    }

    // Computing Visibility Term
    brdf_float Vis;
    {
        brdf_float a2    = alpha * alpha;
        brdf_float GV    = N_dot_L * sqrt(N_dot_V * N_dot_V * (brdf_float(1.0) - a2) + a2);
        brdf_float GL    = N_dot_V * sqrt(N_dot_L * N_dot_L * (brdf_float(1.0) - a2) + a2);
#if defined(ERHE_FP16_BRDF)
        brdf_float denom = max(GV + GL, brdf_float(6.1e-5));
#else
        brdf_float denom = max(GV + GL, brdf_float(1e-8));
#endif
        brdf_float v     = brdf_float(0.5) / denom;
        Vis = max(v, brdf_float(0.0));
    }

    // Computing Fresnel Term
    // Using reflectance mapping as in Filament PBR Pipeline
    brdf_vec3  F0  = brdf_float(0.16) * reflectance * reflectance * (brdf_float(1.0) - metalness) + base_color * metalness;
    brdf_vec3  F   = F0 + (brdf_float(1.0) - F0) * pow(brdf_float(1.0) - clamp(V_dot_H, brdf_float(0.0), brdf_float(1.0)), brdf_float(5.0));

    // Lighting
    brdf_vec3 specular_microfacet = D * Vis * F;
    brdf_vec3 diffuse_lambert     = brdf_float(m_i_pi) * (brdf_float(1.0) - metalness) * base_color;
    brdf_vec3 diffuse_factor      = brdf_vec3(1.0) - F;

    // Final color
    return max(N_dot_L, brdf_float(0.0)) * (diffuse_factor * diffuse_lambert + specular_microfacet);
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
    vec3  N,
    vec2  kernel_roughness // specular anti-aliasing kernel, added to alpha^2 (zero for none)
) {
    float alpha_x = roughness_x * roughness_x;
    float alpha_y = roughness_y * roughness_y;
    alpha_x = sqrt((alpha_x * alpha_x) + kernel_roughness.x);
    alpha_y = sqrt((alpha_y * alpha_y) + kernel_roughness.y);

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
    //float G         = 1.0 / (1.0 + lambda_wo + lambda_wi);
    float G_denom   = max(1.0 + lambda_wo + lambda_wi, 1e-7);
    float G         = 1.0 / G_denom;
    vec3  F         = fresnel_schlick(wi_dot_wh, F0);
    //vec3  specular_microfacet = (D * F * G) / (4.0 * cos_theta(wi) * cos_theta(wo));
    float specular_denom      = max(4.0 * cos_theta(wi) * cos_theta(wo), 1e-2);
    vec3  specular_microfacet = (D * F * G) / specular_denom;
    vec3  diffuse_lambert     = m_i_pi * (1.0 - metallic) * base_color;
    vec3  diffuse_factor      = vec3(1.0) - F;

    return wg_dot_wi * (diffuse_factor * diffuse_lambert + specular_microfacet);
}

#endif // ERHE_BXDF_GLSL
