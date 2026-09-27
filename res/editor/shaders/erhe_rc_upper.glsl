#ifndef ERHE_RC_UPPER_GLSL
#define ERHE_RC_UPPER_GLSL

// The upper cascade stencil of the radiance cascades merge
// (doc/editor/radiance_cascades.md "Layout" and "Merge"), shared by the
// merge (rc_merge.comp) and the per-neighbour connecting trace
// (rc_trace.comp, ERHE_RC_TRACE_NEIGHBOURS), so both use the same 8 upper
// probes and weights, in the order n = i + 2 j + 4 k of
// get_upper_probes() (src/editor/renderers/radiance_cascades_layout.cpp).

// One axis of the trilinear interpolation to the upper cascade, centred on
// the lower one (get_upper_probe_axis()): with an even lower count lower
// probe k sits at upper grid coordinate k / 2 - 1 / 4 (weights 0.25 /
// 0.75), with an odd one at k / 2 (weights 1 / 0 for even k, 0.5 / 0.5 for
// odd k). Indices are clamped to the upper grid (clamp to edge).
void rc_upper_axis(int lower_index, int lower_count, int upper_count, out ivec2 index, out vec2 weight)
{
    int last = max(0, upper_count - 1);
    if ((lower_count % 2) != 0) {
        int m  = lower_index / 2;
        index  = ivec2(clamp(m, 0, last), clamp(m + 1, 0, last));
        weight = ((lower_index % 2) == 0) ? vec2(1.0, 0.0) : vec2(0.5, 0.5);
    } else if ((lower_index % 2) == 0) {
        int m  = lower_index / 2;
        index  = ivec2(clamp(m - 1, 0, last), clamp(m, 0, last));
        weight = vec2(0.25, 0.75);
    } else {
        int m  = (lower_index - 1) / 2;
        index  = ivec2(clamp(m, 0, last), clamp(m + 1, 0, last));
        weight = vec2(0.75, 0.25);
    }
}

// The per_neighbour_trace connecting segments of one lower texel are stored
// in a 4 x 2 texel block of the neighbour atlas: segment n (upper probe n)
// of the lower atlas texel t sits at (4 t.x + (n & 3), 2 t.y + (n >> 2)).
ivec2 rc_neighbour_texel(ivec2 atlas_texel, int n)
{
    return ivec2((4 * atlas_texel.x) + (n & 3), (2 * atlas_texel.y) + (n >> 2));
}

#endif // ERHE_RC_UPPER_GLSL
