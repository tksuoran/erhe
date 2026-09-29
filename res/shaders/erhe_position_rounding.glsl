#ifndef ERHE_POSITION_ROUNDING_GLSL
#define ERHE_POSITION_ROUNDING_GLSL

// Bounds on the fp32 rounding of view-relative positions (doc/erhe/shadows.md
// "View-relative positions" and "Minimum bias"). Shared by the vertex stage
// (standard.vert, which bounds each vertex it computes and passes the bound
// on as v_position_rounding) and the shadow receivers (erhe_light.glsl).

// Unit roundoff of fp32 (round to nearest): a rounded operation returns
// x * (1 + d) with |d| <= 2^-24. The receiver's minimum shadow bias (D1,
// doc/erhe/shadows.md "Minimum bias") is built from bounds in these units.
const float erhe_fp32_unit_roundoff = 1.0 / 16777216.0;

// Bound on the fp32 rounding of a vertex position computed as
// linear * node_position + view_relative_translation (standard.vert: the
// upper 3x3 of world_from_node, the node-space position and the node's
// translation less the pass's view origin), as a vector length. Each
// component is a four-term fp32 sum, off by at most gamma_4 = 4u times the
// magnitudes it adds, |linear_ij| |x_j|, plus the one rounding of the
// translation difference (u |t_i|), which passes through the sum: the
// products round with the vertex's distance from its node origin, not with
// its distance from the view origin, so a vertex far from its node origin
// carries a large bound however near the camera it is.
float get_vertex_position_rounding(mat3 linear, vec3 node_position, vec3 view_relative_translation) {
    mat3 abs_linear = mat3(abs(linear[0]), abs(linear[1]), abs(linear[2]));
    vec3 magnitudes = abs_linear * abs(node_position);
    vec3 rounding   = ((4.0 * erhe_fp32_unit_roundoff) * magnitudes) + ((5.0 * erhe_fp32_unit_roundoff) * abs(view_relative_translation));
    return length(rounding);
}

// Bound on the rounding of an interpolated view-relative position (a
// fragment's receiver point, a caster fragment's point): its triangle's
// vertices (vertex_rounding: the interpolated get_vertex_position_rounding()
// of its vertices, since the barycentric combination of the vertex errors is
// bounded by the same combination of their bounds; the interpolation rounds
// that non-negative bound by at most a relative 4u, which the first factor
// restores) plus the barycentric interpolation of the position itself, a
// four-term sum per component (4u times the magnitudes, for which the
// position's own distance from the view origin stands in): sqrt(3) 4u |p|.
float get_position_rounding(vec3 view_relative_position, float vertex_rounding) {
    return ((1.0 + (4.0 * erhe_fp32_unit_roundoff)) * vertex_rounding) + ((sqrt(3.0) * 4.0 * erhe_fp32_unit_roundoff) * length(view_relative_position));
}

#endif // ERHE_POSITION_ROUNDING_GLSL
