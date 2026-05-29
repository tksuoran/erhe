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
