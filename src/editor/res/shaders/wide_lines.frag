// uniform vec4 _line_color;
// uniform vec2 _line_width;

// out vec4    out_color;

in vec2  v_start;
in vec2  v_line;
in vec4  v_color;
in float v_l2;
in float v_line_width;

//out int gl_SampleMask[];

void main(void)
{
    float t                    = dot(gl_FragCoord.xy - v_start, v_line) / v_l2;
    vec2  projection           = v_start + clamp(t, 0.0, 1.0) * v_line;
    vec2  delta                = gl_FragCoord.xy - projection;
    float d2                   = dot(delta, delta);
    float s                    = v_line_width * v_line_width * 0.25;
    float k                    = clamp(s - d2, 0.0, 1.0);
    float endWeight            = step(abs(t * 2.0 - 1.0), 1);
    float alpha                = mix(k, 1.0, endWeight);
    //gl_SampleMask[gl_SampleID] = (alpha < 0.5) ? 0 : int(0xFFFFFFFF);;
    //gl_SampleMask[gl_SampleID] = int(0x55555555);
    //out_color = vec4(_line_color.rgb, alpha);
    //out_color = vec4(v_color.rgb, alpha);
    //out_color = vec4(alpha, alpha, alpha, 1.0);
    //vec3 color = mix(vec3(1.0, 0.0, 0.0),
    //                 vec3(1.0, 1.0, 0.0), alpha);
    out_color = vec4(v_color.rgb, alpha);
    //out_color = vec4(v_color.rgb, 1.0);
}
