in vec3  v_position;
in vec2  v_start;
in vec2  v_line;
in vec4  v_color;
in float v_l2;
in float v_line_width;

void main(void)
{
    float t                      = dot(gl_FragCoord.xy - v_start, v_line) / v_l2;
    vec2  projection             = v_start + clamp(t, 0.0, 1.0) * v_line;
    vec2  delta                  = gl_FragCoord.xy - projection;
    float d2                     = dot(delta, delta);
    float s                      = v_line_width * v_line_width * 0.25;
    float k                      = clamp(s - d2, 0.0, 1.0);
    float end_weight             = step(abs(t * 2.0 - 1.0), 1);
    float alpha                  = mix(k, 1.0, end_weight);

#if ERHE_LINE_SHADER_PASSTHROUGH_BASIC_LINES
    vec3  view_position_in_world = view.view_position_in_world.xyz;
    float d                      = distance(view_position_in_world, v_position);
    float clamped_d              = max(1.0, sqrt(d));
    float inv_clamped_d          = min(1.0, 8.0 * v_line_width / clamped_d);
    out_color = vec4(v_color.rgb * v_color.a * inv_clamped_d, v_color.a * inv_clamped_d);
#else
    if (alpha < 0.5) {
        discard;
    }
    out_color = vec4(v_color.rgb * v_color.a, v_color.a);
#endif
}
