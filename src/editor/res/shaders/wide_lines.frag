in layout(location = 0) vec2  v_start;
in layout(location = 1) vec2  v_line;
in layout(location = 2) vec4  v_color;
in layout(location = 3) float v_l2;
in layout(location = 4) float v_line_width;

void main(void)
{
    float t          = dot(gl_FragCoord.xy - v_start, v_line) / v_l2;
    vec2  projection = v_start + clamp(t, 0.0, 1.0) * v_line;
    vec2  delta      = gl_FragCoord.xy - projection;
    float d2         = dot(delta, delta);
    float s          = v_line_width * v_line_width * 0.25;
    float k          = clamp(s - d2, 0.0, 1.0);
    float end_weight = step(abs(t * 2.0 - 1.0), 1);
    float alpha      = mix(k, 1.0, end_weight);
    if (alpha < 0.5) {
        discard;
    }
    out_color = vec4(v_color.rgb * alpha, v_color.a);
}
