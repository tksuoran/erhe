#ifndef ERHE_GGX_GLSL
#define ERHE_GGX_GLSL

#include "erhe_math.glsl" // m_pi

float ggx_isotropic_ndf(float N_dot_H, float alpha) {
    float a = N_dot_H * alpha;
    float k = alpha / (1.0 - N_dot_H * N_dot_H + a * a);
    return k * k * m_i_pi;
}

float ggx_anisotropic_ndf(float alpha_t, float alpha_b, float T_dot_H, float B_dot_H, float N_dot_H) {
    vec3 v = vec3(
        alpha_b * T_dot_H,
        alpha_t * B_dot_H,
        alpha_t * alpha_b * N_dot_H
    );
    float v2 = dot(v, v);
    float a2 = alpha_t * alpha_b;
    float w2 = 0.0;
    //if (v2 != 0.0)
    {
        w2 = a2 / v2;
    }
    return a2 * w2 * w2 * m_i_pi;
}

float ggx_isotropic_visibility(float N_dot_V, float N_dot_L, float alpha) {
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
) {
    float lambda_V = N_dot_L * length(vec3(alpha_t * T_dot_V, alpha_b * B_dot_V, N_dot_V));
    float lambda_L = N_dot_V * length(vec3(alpha_t * T_dot_L, alpha_b * B_dot_L, N_dot_L));
    float v = 0.5 / (lambda_V + lambda_L);
    return clamp(v, 0.0, 1.0);
}


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

float p22_ggx_isotropic(float x, float y, float alpha) {
    float x_sqr     = x * x;
    float y_sqr     = y * y;
    float alpha_sqr = alpha * alpha;
    float denom     = 1.0 + (x_sqr / alpha_sqr) + (y_sqr / alpha_sqr);
    return 1.0 / ((m_pi * alpha_sqr) * (denom * denom));
}

float p22_ggx_anisotropic(float x, float y, float alpha_x, float alpha_y) {
    float x_sqr       = x * x;
    float y_sqr       = y * y;
    float alpha_x_sqr = alpha_x * alpha_x;
    float alpha_y_sqr = alpha_y * alpha_y;
    float denom       = 1.0 + (x_sqr / alpha_x_sqr) + (y_sqr / alpha_y_sqr);
    float denom_sqr   = denom * denom;
    return 1.0 / ((m_pi * alpha_x * alpha_y) * denom_sqr);
}

float slope_ndf_ggx_isotropic(vec3 omega_h, float alpha) {
    float slope_x     = -(omega_h.x / omega_h.z);
    float slope_y     = -(omega_h.y / omega_h.z);
    float cos_theta   = cos_theta(omega_h);
    float cos_2_theta = cos_theta * cos_theta;
    float cos_4_theta = cos_2_theta * cos_2_theta;
    float ggx_p22     = p22_ggx_isotropic(slope_x, slope_y, alpha);
    return ggx_p22 / cos_4_theta;
}

float slope_ndf_ggx_anisotropic(vec3 omega_h, float alpha_x, float alpha_y) {
    float slope_x     = -(omega_h.x / omega_h.z);
    float slope_y     = -(omega_h.y / omega_h.z);
    float cos_theta   = cos_theta(omega_h);
    float cos_2_theta = cos_theta * cos_theta;
    float cos_4_theta = cos_2_theta * cos_2_theta;
    float ggx_p22     = p22_ggx_anisotropic(slope_x, slope_y, alpha_x, alpha_y);
    if (cos_4_theta == 0.0) {
        return 1.0;
    }
    return ggx_p22 / cos_4_theta;
}

float lambda_ggx_isotropic(vec3 omega, float alpha) {
    float a = 1.0 / (alpha * tan_theta(omega));
    return ( (-1.0 + sqrt(1.0 + 1.0 / (a * a))) * 0.5);
    //return 0.5 * (-1.0 + sqrt(1.0 + 1.0 / (a * a)));
}

float lambda_ggx_anisotropic(vec3 omega, float alpha_x, float alpha_y) {
    float cos_phi = cos_phi(omega);
    float sin_phi = sin_phi(omega);
    float alpha_o = sqrt(cos_phi * cos_phi * alpha_x * alpha_x + sin_phi * sin_phi * alpha_y * alpha_y);
    float a       = 1.0 / (alpha_o * tan_theta(omega));
    return 0.5 * (-1.0 + sqrt(1.0 + 1.0 / (a * a)));
}

#endif // ERHE_GGX_GLSL
