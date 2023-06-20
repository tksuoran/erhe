// uniform vec4 _viewport;
// uniform vec2 _line_width;

// Must define one of these to 1 and others to 0
//#define ERHE_LINE_SHADER_SHOW_DEBUG_LINES
//#define ERHE_LINE_SHADER_PASSTHROUGH_BASIC_LINES
//#define ERHE_LINE_SHADER_STRIP

layout(lines) in;

#if ERHE_LINE_SHADER_SHOW_DEBUG_LINES || ERHE_LINE_SHADER_PASSTHROUGH_BASIC_LINES
layout(line_strip, max_vertices = 10) out;
#else
layout(triangle_strip, max_vertices = 6) out;
#endif

in vec3  vs_position  [];
in vec4  vs_color     [];
in float vs_line_width[];

out vec3  v_position;
out vec2  v_start;
out vec2  v_line;
out vec4  v_color;
out float v_l2;
out float v_line_width;

float get_line_width(vec4 position, float thickness)
{
    float fov_left        = view.fov[0];
    float fov_right       = view.fov[1];
    float fov_up          = view.fov[2];
    float fov_down        = view.fov[3];
    float fov_width       = fov_right - fov_left;
    float fov_height      = fov_up    - fov_down;
    float viewport_width  = view.viewport[2];
    float viewport_height = view.viewport[2];

    float scaled_thickness = (thickness < 0.0)
        ? -thickness
        : max(thickness / position.w, 0.01);
    return (1.0 / 1024.0) * viewport_width * scaled_thickness / fov_width;
}

void main(void)
{
    //  a - - - - - - - - - - - - - - - - b
    //  |      |                   |      |
    //  |      |                   |      |
    //  |      |                   |      |
    //  | - - -start - - - - - - end- - - |
    //  |      |                   |      |
    //  |      |                   |      |
    //  |      |                   |      |
    //  d - - - - - - - - - - - - - - - - c

    vec4 start = gl_in[0].gl_Position;
    vec4 end   = gl_in[1].gl_Position;

#if ERHE_LINE_SHADER_PASSTHROUGH_BASIC_LINES
    gl_Position = start; v_position = vs_position[0]; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = end;   v_position = vs_position[1]; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    EndPrimitive();
    return;
#endif

    // Line clipping. Adapted from tinygl.
    vec4  l      = gl_in[1].gl_Position - gl_in[0].gl_Position;
    float denom0 =  l.x + l.w; float num0 = -gl_in[0].gl_Position.x - gl_in[0].gl_Position.w; float t0 = num0 / denom0;
    float denom1 = -l.x + l.w; float num1 =  gl_in[0].gl_Position.x - gl_in[0].gl_Position.w; float t1 = num1 / denom1;
    float denom2 =  l.y + l.w; float num2 = -gl_in[0].gl_Position.y - gl_in[0].gl_Position.w; float t2 = num2 / denom2;
    float denom3 = -l.y + l.w; float num3 =  gl_in[0].gl_Position.y - gl_in[0].gl_Position.w; float t3 = num3 / denom3;
    float denom4 =  l.z + l.w; float num4 = -gl_in[0].gl_Position.z - gl_in[0].gl_Position.w; float t4 = num4 / denom4;
    float denom5 = -l.z + l.w; float num5 =  gl_in[0].gl_Position.z - gl_in[0].gl_Position.w; float t5 = num5 / denom5;
    float tmin   = 0.0;
    float tmax   = 1.0;
    if (denom0 > 0) {
        if (t0 > tmax) return;
        if (t0 > tmin) tmin = t0;
    } else if (denom0 < 0) {
        if (t0 < tmin) return;
        if (t0 < tmax) tmax = t0;
    } else if (num0 > 0) {
        return;
    }
    if (denom1 > 0) {
        if (t1 > tmax) return;
        if (t1 > tmin) tmin = t1;
    } else if (denom1 < 0) {
        if (t1 < tmin) return;
        if (t1 < tmax) tmax = t1;
    } else if (num1 > 0) {
        return;
    }
    if (denom2 > 0) {
        if (t2 > tmax) return;
        if (t2 > tmin) tmin = t2;
    } else if (denom2 < 0) {
        if (t2 < tmin) return;
        if (t2 < tmax) tmax = t2;
    } else if (num2 > 0) {
        return;
    }
    if (denom3 > 0) {
        if (t3 > tmax) return;
        if (t3 > tmin) tmin = t3;
    } else if (denom3 < 0) {
        if (t3 < tmin) return;
        if (t3 < tmax) tmax = t3;
    } else if (num3 > 0) {
        return;
    }
    if (denom4 > 0) {
        if (t4 > tmax) return;
        if (t4 > tmin) tmin = t4;
    } else if (denom4 < 0) {
        if (t4 < tmin) return;
        if (t4 < tmax) tmax = t4;
    } else if (num4 > 0) {
        return;
    }
    if (denom5 > 0) {
        if (t5 > tmax) return;
        if (t5 > tmin) tmin = t5;
    } else if (denom5 < 0) {
        if (t5 < tmin) return;
        if (t5 < tmax) tmax = t5;
    } else if (num5 > 0) {
        return;
    }
    start = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, tmin);
    end   = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, tmax);
    vec4  start_color  = mix(vs_color[0], vs_color[1], tmin);
    vec4  end_color    = mix(vs_color[0], vs_color[1], tmax);
    float start_width0 = mix(vs_line_width[0], vs_line_width[1], tmin);
    float end_width0   = mix(vs_line_width[0], vs_line_width[1], tmax);

    float start_width = get_line_width(start, start_width0);
    float end_width   = get_line_width(end,   end_width0);

    vec2 vp_size         = view.viewport.zw;

    // Compute line axis and side vector in screen space
    vec2 start_in_ndc    = start.xy / start.w;       //  clip to NDC: homogenize and drop z
    vec2 end_in_ndc      = end.xy   / end.w;
    vec2 line_in_ndc     = end_in_ndc - start_in_ndc;
    vec2 start_in_screen = (0.5 * start_in_ndc + vec2(0.5)) * vp_size + view.viewport.xy;
    vec2 end_in_screen   = (0.5 * end_in_ndc   + vec2(0.5)) * vp_size + view.viewport.xy;
    vec2 line_in_screen  = line_in_ndc * vp_size;       //  NDC to window (direction vector)
    vec2 axis_in_screen  = normalize(line_in_screen);
    vec2 side_in_screen  = vec2(-axis_in_screen.y, axis_in_screen.x);    // rotate
    vec2 axis_in_ndc     = axis_in_screen / vp_size;                   // screen to NDC
    vec2 side_in_ndc     = side_in_screen / vp_size;
    vec4 axis_start      = vec4(axis_in_ndc, 0.0, 0.0) * start_width;  // NDC to clip (delta vector)
    vec4 axis_end        = vec4(axis_in_ndc, 0.0, 0.0) * end_width;    // NDC to clip (delta vector)
    vec4 side_start      = vec4(side_in_ndc, 0.0, 0.0) * start_width;
    vec4 side_end        = vec4(side_in_ndc, 0.0, 0.0) * end_width;

    vec4 a = (start + (side_start - axis_start) * start.w);
    vec4 b = (end   + (side_end   + axis_end  ) * end.w);
    vec4 c = (end   - (side_end   - axis_end  ) * end.w);
    vec4 d = (start - (side_start + axis_start) * start.w);

    v_start = start_in_screen;
    v_line  = end_in_screen - start_in_screen;
    v_l2    = dot(v_line, v_line);

#if ERHE_LINE_SHADER_SHOW_DEBUG_LINES
    gl_Position = gl_in[0].gl_Position; v_position = vs_position[0]; v_color = vs_color[0]; v_line_width = start_width; EmitVertex();
    gl_Position = gl_in[1].gl_Position; v_position = vs_position[1]; v_color = vs_color[1]; v_line_width = end_width;   EmitVertex();
    EndPrimitive();

    gl_Position = a; v_position = vs_position[0]; v_color = start_color; v_line_width = start_width; EmitVertex();
    gl_Position = b; v_position = vs_position[1]; v_color = end_color;   v_line_width = end_width;   EmitVertex();
    EndPrimitive();

    gl_Position = b; v_position = vs_position[1]; v_color = end_color;   v_line_width = end_width;   EmitVertex();
    gl_Position = c; v_position = vs_position[1]; v_color = end_color;   v_line_width = end_width;   EmitVertex();
    EndPrimitive();

    gl_Position = c; v_position = vs_position[1]; v_color = end_color;   v_line_width = end_width;   EmitVertex();
    gl_Position = d; v_position = vs_position[0]; v_color = start_color; v_line_width = start_width; EmitVertex();
    EndPrimitive();

    gl_Position = d; v_position = vs_position[0]; v_color = start_color; v_line_width = start_width; EmitVertex();
    gl_Position = a; v_position = vs_position[0]; v_color = start_color; v_line_width = start_width; EmitVertex();
    EndPrimitive();

#else

#if ERHE_LINE_SHADER_STRIP
#if 1
    gl_Position = d; v_position = vs_position[0]; v_color = start_color; v_line_width = start_width; EmitVertex();
    gl_Position = a; v_position = vs_position[0]; v_color = start_color; v_line_width = start_width; EmitVertex();
    gl_Position = c; v_position = vs_position[1]; v_color = end_color;   v_line_width = end_width;   EmitVertex();
    gl_Position = b; v_position = vs_position[1]; v_color = end_color;   v_line_width = end_width;   EmitVertex();
#else
    gl_Position = a; v_position = vs_position[0]; v_color = start_color; v_line_width = start_width; EmitVertex();
    gl_Position = d; v_position = vs_position[0]; v_color = start_color; v_line_width = start_width; EmitVertex();
    gl_Position = b; v_position = vs_position[1]; v_color = end_color;   v_line_width = end_width;   EmitVertex();
    gl_Position = c; v_position = vs_position[1]; v_color = end_color;   v_line_width = end_width;   EmitVertex();
#endif
    EndPrimitive();
#else
    gl_Position = d; v_position = vs_position[0]; v_color = start_color; v_line_width = start_width; EmitVertex();
    gl_Position = a; v_position = vs_position[0]; v_color = start_color; v_line_width = start_width; EmitVertex();
    gl_Position = c; v_position = vs_position[1]; v_color = end_color;   v_line_width = end_width;   EmitVertex();
    EndPrimitive();
    gl_Position = c; v_position = vs_position[1]; v_color = end_color;   v_line_width = end_width;   EmitVertex();
    gl_Position = a; v_position = vs_position[0]; v_color = start_color; v_line_width = start_width; EmitVertex();
    gl_Position = b; v_position = vs_position[1]; v_color = end_color;   v_line_width = end_width;   EmitVertex();
    EndPrimitive();
#endif

#endif
}
