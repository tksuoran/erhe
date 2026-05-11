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
    // Single-view: read from program_interface's camera UBO (bound by
    // the surrounding forward_renderer flow). Multiview: read from
    // the wide-line renderer's per-eye view UBO instead - the
    // multiview bind_group_layout intentionally does NOT include the
    // program_interface camera UBO so we cannot rely on it being
    // bound at draw time.
#ifdef ERHE_MULTIVIEW
    vec2  vp_offset  = view.cameras[c_view_index].viewport.xy;
#else
    vec2  vp_offset  = camera.cameras[c_view_index].viewport.xy;
#endif
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
