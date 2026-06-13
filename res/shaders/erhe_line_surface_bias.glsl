#ifndef ERHE_LINE_SURFACE_BIAS_GLSL
#define ERHE_LINE_SURFACE_BIAS_GLSL

// Depth bias (in NDC z, unsigned magnitude) for a surface-aligned debug line,
// derived from depth precision and surface slope rather than a tuned constant:
//
//   bias_ndc = margin * ulp(window_depth) * tilt * window_to_ndc_scale
//
//   * ulp(window_depth): the resolvable step of the fp32 depth buffer at this
//     fragment's window depth (23 mantissa bits). The reverse-Z distribution
//     enters through the depth value itself, so no separate reverse-Z term is
//     needed beyond the sign the caller applies.
//   * tilt: surface slope factor tan(theta) = sqrt(1-NdotV^2)/NdotV, clamped
//     to [1, SLOPE_MAX], approximating how fast the surface depth changes
//     across the wide line's screen footprint (the slope-scaled bias term).
//   * margin: unitless headroom in resolvable units.
//
// Returns 0 when no normal is supplied (length 0), so ordinary lines are
// unbiased. The caller applies the sign (clip_depth_direction) and, when it
// works in clip space, multiplies by w.
//
// Assumes a 32-bit float depth buffer (the desktop viewport format
// format_d32_sfloat_s8_uint, where surface-aligned lines are drawn). A
// fixed-point target would replace ulp() with a constant 2^-bits step.

const float ERHE_LINE_BIAS_SLOPE_MAX = 8.0;

float erhe_line_surface_bias_ndc(
    vec3  normal_world,
    vec3  world_position,
    vec3  view_position_in_world,
    float ndc_z,
    float margin,
    float window_to_ndc_scale
)
{
    float normal_length = length(normal_world);
    if (normal_length < 1e-4) {
        return 0.0;
    }
    vec3  normal = normal_world / normal_length;
    vec3  v      = normalize(view_position_in_world - world_position);
    float NdotV  = clamp(dot(normal, v), 0.0, 1.0);

    // Window depth for this depth-range convention (zero_to_one: window == ndc;
    // minus_one_to_one: window = 0.5*ndc + 0.5).
    float window_depth = (window_to_ndc_scale > 1.5) ? (0.5 * ndc_z + 0.5) : ndc_z;

    // fp32 resolvable step (1 ULP) at this window depth: 2^(exponent - 23).
    float ulp = exp2(floor(log2(max(abs(window_depth), 1e-7))) - 23.0);

    float tilt = clamp(
        sqrt(max(1.0 - (NdotV * NdotV), 0.0)) / max(NdotV, 1e-3),
        1.0,
        ERHE_LINE_BIAS_SLOPE_MAX
    );

    return margin * ulp * tilt * window_to_ndc_scale;
}

#endif // ERHE_LINE_SURFACE_BIAS_GLSL
