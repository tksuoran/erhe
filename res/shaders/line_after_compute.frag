#include "erhe_camera_view.glsl"

layout(location = 0) in float v_line_width;
layout(location = 1) in vec4  v_color;
layout(location = 2) in vec4  v_start_end;

void main(void)
{
    // v_start_end is in VIEWPORT-RELATIVE pixel coordinates (the
    // compute shader writes screen_from_ndc results in [0..vp_size]).
    // Subtract the (per-eye) viewport.xy from gl_FragCoord so the
    // line-distance math is correct regardless of where the viewport
    // sits inside the framebuffer. c_view_index is 0 for single-view
    // and gl_ViewIndex for multiview, indexing the per-eye ViewCamera
    // entry that the wide-line view UBO carries.
    vec2 vp_offset = view.cameras[c_view_index].viewport.xy;
    vec2 frag_xy   = gl_FragCoord.xy - vp_offset;
    vec2  start = v_start_end.xy;
    vec2  end   = v_start_end.zw;
    vec2  line  = end - start;
    float l2    = dot(line, line);

    float t          = dot(frag_xy - start, line) / l2;
    vec2  projection = start + clamp(t, 0.0, 1.0) * line;
    vec2  delta      = frag_xy - projection;
    float h          = 0.5 * v_line_width;

    float coverage;
    if (view.binary_edge > 0.0) {
        // Anti-aliasing off: the ribbon is exactly the line width, so its
        // rasterized edge is the line edge; only the round caps are cut by
        // the distance test.
        float d2         = dot(delta, delta);
        float k          = clamp((h * h) - d2, 0.0, 1.0);
        float end_weight = step(abs(t * 2.0 - 1.0), 1.0);
        if (mix(k, 1.0, end_weight) < 0.5) {
            discard;
        }
        coverage = 1.0;
    } else {
        // Analytic coverage of a one-pixel box filter against the line edge
        // (doc/erhe/renderer.md "Line widths"): d is the distance to the
        // segment (round caps). The geometric half width hg is at least half
        // a pixel (view.fringe) so a thinner line keeps a one-pixel footprint
        // and fades by alpha (2 * h) instead: the coverage integrated across
        // the line is the full width for every width. The ribbon extends
        // hg + 0.5 from the segment, so every non-zero coverage is inside it.
        float d  = length(delta);
        float hg = max(h, view.fringe);
        coverage = clamp(hg + 0.5 - d, 0.0, 1.0) * min(1.0, 2.0 * h);
        // Two draws per pass (doc/erhe/renderer.md "Line anti-aliasing"):
        // the core draw keeps the fully covered fragments, the fringe draw
        // (ERHE_DEBUG_LINE_FRINGE) the partial ones under a stencil compare
        // that lets the first fringe fragment win a pixel and never draws
        // over a core, so overlapping fringes (polyline joints) do not
        // blend twice.
#if defined(ERHE_DEBUG_LINE_FRINGE)
        if ((coverage <= 0.0) || (coverage >= 1.0)) {
            discard;
        }
#else
        if (coverage < 1.0) {
            discard;
        }
#endif
    }
    float alpha = v_color.a * coverage;
#if defined(ERHE_DEBUG_LINE_HIDDEN)
    // Hidden (occluded) pass: dimmed by the bucket's strength (0.1, or 1.0
    // for xray). Scaling the premultiplied output keeps the coverage-scaled
    // "over" blend, which a constant blend factor could not.
    alpha *= view.hidden_dim;
#endif
    out_color = vec4(v_color.rgb * alpha, alpha);
}
