in float v_line_width;
in vec4  v_color;
in vec4  v_start_end;

void main(void)
{
    vec2  start = v_start_end.xy;
    vec2  end   = v_start_end.zw;
    vec2  line  = end - start;
    float l2    = dot(line, line);

    float t          = dot(gl_FragCoord.xy - start, line) / l2;
    vec2  projection = start + clamp(t, 0.0, 1.0) * line;
    vec2  delta      = gl_FragCoord.xy - projection;
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
