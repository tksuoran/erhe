// Must define one of these to 1 and others to 0
//#define ERHE_LINE_SHADER_SHOW_DEBUG_LINES
//#define ERHE_LINE_SHADER_PASSTHROUGH_BASIC_LINES
//#define ERHE_LINE_SHADER_STRIP

layout(triangles) in;

#if ERHE_LINE_SHADER_SHOW_DEBUG_LINES || ERHE_LINE_SHADER_PASSTHROUGH_BASIC_LINES
layout(line_strip, max_vertices = 30) out; // 3 * 10 = 30
#else
layout(triangle_strip, max_vertices = 18) out; // 6 * 3 = 18
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
    float fov_left         = camera.cameras[0].fov[0];
    float fov_right        = camera.cameras[0].fov[1];
    float fov_width        = fov_right - fov_left;
    float viewport_width   = camera.cameras[0].viewport[2];
    float scaled_thickness = (thickness < 0.0)
        ? -thickness
        : max(thickness / position.w, 0.01);
    return (1.0 / 1024.0) * viewport_width * scaled_thickness / fov_width;
}

bool clip_line(
    in  vec4  v0,
    in  vec4  v1,
    out vec4  clipped_v0,
    out vec4  clipped_v1,
    out float tmin,
    out float tmax
)
{
    vec4  l      = v1 - v0;
    float denom0 =  l.x + l.w; float num0 = -v0.x - v0.w; float t0 = num0 / denom0;
    float denom1 = -l.x + l.w; float num1 =  v0.x - v0.w; float t1 = num1 / denom1;
    float denom2 =  l.y + l.w; float num2 = -v0.y - v0.w; float t2 = num2 / denom2;
    float denom3 = -l.y + l.w; float num3 =  v0.y - v0.w; float t3 = num3 / denom3;
    float denom4 =  l.z + l.w; float num4 = -v0.z - v0.w; float t4 = num4 / denom4;
    float denom5 = -l.z + l.w; float num5 =  v0.z - v0.w; float t5 = num5 / denom5;
    tmin = 0.0;
    tmax = 1.0;
    if (denom0 > 0) {
        if (t0 > tmax) return false;
        if (t0 > tmin) tmin = t0;
    } else if (denom0 < 0) {
        if (t0 < tmin) return false;
        if (t0 < tmax) tmax = t0;
    } else if (num0 > 0) {
        return false;
    }
    if (denom1 > 0) {
        if (t1 > tmax) return false;
        if (t1 > tmin) tmin = t1;
    } else if (denom1 < 0) {
        if (t1 < tmin) return false;
        if (t1 < tmax) tmax = t1;
    } else if (num1 > 0) {
        return false;
    }
    if (denom2 > 0) {
        if (t2 > tmax) return false;
        if (t2 > tmin) tmin = t2;
    } else if (denom2 < 0) {
        if (t2 < tmin) return false;
        if (t2 < tmax) tmax = t2;
    } else if (num2 > 0) {
        return false;
    }
    if (denom3 > 0) {
        if (t3 > tmax) return false;
        if (t3 > tmin) tmin = t3;
    } else if (denom3 < 0) {
        if (t3 < tmin) return false;
        if (t3 < tmax) tmax = t3;
    } else if (num3 > 0) {
        return false;
    }
    if (denom4 > 0) {
        if (t4 > tmax) return false;
        if (t4 > tmin) tmin = t4;
    } else if (denom4 < 0) {
        if (t4 < tmin) return false;
        if (t4 < tmax) tmax = t4;
    } else if (num4 > 0) {
        return false;
    }
    if (denom5 > 0) {
        if (t5 > tmax) return false;
        if (t5 > tmin) tmin = t5;
    } else if (denom5 < 0) {
        if (t5 < tmin) return false;
        if (t5 < tmax) tmax = t5;
    } else if (num5 > 0) {
        return false;
    }
    clipped_v0 = mix(v0, v1, tmin);
    clipped_v1 = mix(v0, v1, tmax);
    return true;
}

void do_line(uint i0, uint i1)
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

    vec4 v0 = gl_in[i0].gl_Position;
    vec4 v1 = gl_in[i1].gl_Position;

#if ERHE_LINE_SHADER_PASSTHROUGH_BASIC_LINES
    gl_Position = v0; v_position = vs_position[0]; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = v1; v_position = vs_position[1]; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    EndPrimitive();
    return;
#endif

    vec4 v0_clipped, v1_clipped; float tmin, tmax;

    if (clip_line(v0, v1, v0_clipped, v1_clipped, tmin, tmax) == false) {
        return;
    }

    vec3  position0   = mix(vs_position  [0], vs_position  [1], tmin);
    vec3  position1   = mix(vs_position  [0], vs_position  [1], tmax);
    vec4  color0      = mix(vs_color     [0], vs_color     [1], tmin);
    vec4  color1      = mix(vs_color     [0], vs_color     [1], tmax);
    float line_width0 = mix(vs_line_width[0], vs_line_width[1], tmin);
    float line_width1 = mix(vs_line_width[0], vs_line_width[1], tmax);

    float width0      = get_line_width(v0_clipped, line_width0);
    float width1      = get_line_width(v1_clipped, line_width1);

    vec2 vp_size         = camera.cameras[0].viewport.zw;

    // Compute line axis and side vector in screen space
    vec2 v0_in_ndc       = v0_clipped.xy / v0_clipped.w;       //  clip to NDC: homogenize and drop z
    vec2 v1_in_ndc       = v1_clipped.xy / v1_clipped.w;
    vec2 line_in_ndc     = v1_in_ndc - v0_in_ndc;
    vec2 v0_in_screen    = (0.5 * v0_in_ndc + vec2(0.5)) * vp_size + camera.cameras[0].viewport.xy;
    vec2 v1_in_screen    = (0.5 * v1_in_ndc + vec2(0.5)) * vp_size + camera.cameras[0].viewport.xy;
    vec2 line_in_screen  = line_in_ndc * vp_size;       //  NDC to window (direction vector)
    vec2 axis_in_screen  = normalize(line_in_screen);
    vec2 side_in_screen  = vec2(-axis_in_screen.y, axis_in_screen.x); // rotate
    vec2 axis_in_ndc     = axis_in_screen / vp_size;                  // screen to NDC
    vec2 side_in_ndc     = side_in_screen / vp_size;
    vec4 axis0           = vec4(axis_in_ndc, 0.0, 0.0) * width0;  // NDC to clip (delta vector)
    vec4 axis1           = vec4(axis_in_ndc, 0.0, 0.0) * width1;  // NDC to clip (delta vector)
    vec4 side0           = vec4(side_in_ndc, 0.0, 0.0) * width0;
    vec4 side1           = vec4(side_in_ndc, 0.0, 0.0) * width1;

    vec4 a = (v0_clipped + (side0 - axis0) * v0_clipped.w);
    vec4 b = (v1_clipped + (side1 + axis1) * v1_clipped.w);
    vec4 c = (v1_clipped - (side1 - axis1) * v1_clipped.w);
    vec4 d = (v0_clipped - (side0 + axis0) * v0_clipped.w);

    v_start = v0_in_screen;
    v_line  = v1_in_screen - v0_in_screen;
    v_l2    = dot(v_line, v_line);

#if ERHE_LINE_SHADER_SHOW_DEBUG_LINES
    gl_Position = gl_in[i0].gl_Position; v_position = vs_position[i0]; v_color = vs_color[i0]; v_line_width = vs_line_width[i0]; EmitVertex();
    gl_Position = gl_in[i1].gl_Position; v_position = vs_position[i1]; v_color = vs_color[i1]; v_line_width = vs_line_width[i1]; EmitVertex();
    EndPrimitive();

    gl_Position = a; v_position = position0; v_color = color0; v_line_width = width0; EmitVertex();
    gl_Position = b; v_position = position1; v_color = color1; v_line_width = width1; EmitVertex();
    EndPrimitive();

    gl_Position = b; v_position = position1; v_color = color1; v_line_width = width1; EmitVertex();
    gl_Position = c; v_position = position1; v_color = color1; v_line_width = width1; EmitVertex();
    EndPrimitive();

    gl_Position = c; v_position = position1; v_color = color1; v_line_width = width1; EmitVertex();
    gl_Position = d; v_position = position0; v_color = color0; v_line_width = width0; EmitVertex();
    EndPrimitive();

    gl_Position = d; v_position = position0; v_color = color0; v_line_width = width0; EmitVertex();
    gl_Position = a; v_position = position0; v_color = color0; v_line_width = width0; EmitVertex();
    EndPrimitive();

#else

    // CCW Triangle strip indices: 0123 = adbc
    // CCW Triangles indices:      012, 213 = adb, bdc
    // a-b  0-2
    // |/|  |/|
    // d-c  1-3
#if ERHE_LINE_SHADER_STRIP
    gl_Position = a; v_position = position0; v_color = color0; v_line_width = width0; EmitVertex();
    gl_Position = d; v_position = position0; v_color = color0; v_line_width = width0; EmitVertex();
    gl_Position = b; v_position = position1; v_color = color1; v_line_width = width1; EmitVertex();
    gl_Position = c; v_position = position1; v_color = color1; v_line_width = width1; EmitVertex();
    EndPrimitive();
#else
    gl_Position = a; v_position = position0; v_color = color0; v_line_width = width0; EmitVertex();
    gl_Position = d; v_position = position0; v_color = color0; v_line_width = width0; EmitVertex();
    gl_Position = b; v_position = position1; v_color = color1; v_line_width = width1; EmitVertex();
    EndPrimitive();
    gl_Position = b; v_position = position1; v_color = color1; v_line_width = width1; EmitVertex();
    gl_Position = d; v_position = position0; v_color = color0; v_line_width = width0; EmitVertex();
    gl_Position = c; v_position = position1; v_color = color1; v_line_width = width1; EmitVertex();
    EndPrimitive();
#endif

#endif
}

void main(void)
{
    do_line(0, 1);
    do_line(1, 2);
    do_line(2, 0);
}
