#include "erhe_camera_view.glsl"
layout(location = 0) in float v_line_width;
layout(location = 1) in vec4  v_color;
layout(location = 2) in vec4  v_start_end;

void main(void)
{
    // v_start_end is in VIEWPORT-RELATIVE pixel coordinates (compute
    // shader writes them in [0..vp_size]). The fragment shader reads
    // the DRAW viewport's origin and subtracts it from gl_FragCoord
    // so the line distance test is correct regardless of where the
    // viewport sits inside the framebuffer.
    //
    // Both single-view and multiview read from the wide-line renderer's
    // per-eye view UBO (binding 3). c_view_index is 0 for single-view
    // and gl_ViewIndex for multiview. The wide-line graphics layout
    // intentionally does NOT include the program_interface camera UBO
    // (we cannot rely on it staying bound across a descriptor-set-layout
    // switch from forward_renderer), so the view UBO carries the per-eye
    // viewport.xy that this shader needs.
    vec2  vp_offset  = view.cameras[c_view_index].viewport.xy;
    vec2  frag_xy    = gl_FragCoord.xy - vp_offset;

#ifdef ERHE_CONTENT_LINE_SEED_MASK
    // ID-buffer edge-line SEED mask: reject this edge fragment unless the seed
    // buffer's frontmost-visible-face id at this pixel equals this edge half-quad's
    // own face id (encoded in v_color). This drops cap / overspray fragments that
    // land on another face or on the background BEFORE they can write the edge-id
    // color OR win the edge-id depth test, so the edge-id buffer is correct by
    // construction. Integer texelFetch (no filtering -- ids must not blend); the
    // seed buffer shares this pass's viewport so seed_px is in range, but the fetch
    // is guarded anyway (out-of-range texelFetch is UB and hangs some drivers).
    {
        ivec2 seed_px   = ivec2(frag_xy);
        ivec2 seed_size = ivec2(textureSize(s_seed_id, 0));
        if (all(greaterThanEqual(seed_px, ivec2(0))) && all(lessThan(seed_px, seed_size))) {
            vec4 seed_texel = texelFetch(s_seed_id, seed_px, 0);
            uint seed_id = (uint(seed_texel.r * 255.0 + 0.5) << 16) |
                           (uint(seed_texel.g * 255.0 + 0.5) <<  8) |
                            uint(seed_texel.b * 255.0 + 0.5);
            uint edge_id = (uint(v_color.r * 255.0 + 0.5) << 16) |
                           (uint(v_color.g * 255.0 + 0.5) <<  8) |
                            uint(v_color.b * 255.0 + 0.5);
            if (seed_id != edge_id) {
                discard;
            }
        } else {
            discard;
        }
    }
#endif

    vec2  start      = v_start_end.xy;
    vec2  end        = v_start_end.zw;
    vec2  line       = end - start;
    float l2         = dot(line, line);

    float t          = dot(frag_xy - start, line) / l2;
    vec2  projection = start + clamp(t, 0.0, 1.0) * line;
    vec2  delta      = frag_xy - projection;
    float d2         = dot(delta, delta);
    float s          = v_line_width * v_line_width * 0.25;
    float k          = clamp(s - d2, 0.0, 1.0);
    float end_weight = step(abs(t * 2.0 - 1.0), 1.0);
    float alpha      = mix(k, 1.0, end_weight);

    if (alpha < 0.5) {
        discard;
    }
    out_color = vec4(v_color.rgb * v_color.a, v_color.a);
}
