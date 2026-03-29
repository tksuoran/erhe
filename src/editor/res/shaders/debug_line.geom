layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;

layout(location = 0) in vec4  vs_color[];
layout(location = 1) in float vs_line_width[];

layout(location = 0) out float v_line_width;
layout(location = 1) out vec4  v_color;
layout(location = 2) out vec4  v_start_end;

float get_line_width(vec4 position, float thickness)
{
    float fov_left         = view.fov[0];
    float fov_right        = view.fov[1];
    float fov_width        = fov_right - fov_left;
    float viewport_width   = view.viewport[2];
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
    // Near/far clipping only -- X/Y disabled so wide lines outside viewport are not culled
    float denom4 =  l.z + l.w; float num4 = -v0.z - v0.w; float t4 = num4 / denom4;
    float denom5 = -l.z + l.w; float num5 =  v0.z - v0.w; float t5 = num5 / denom5;
    tmin = 0.0;
    tmax = 1.0;
    if (denom4 > 0.0) {
        if (t4 > tmax) return false;
        if (t4 > tmin) tmin = t4;
    } else if (denom4 < 0.0) {
        if (t4 < tmin) return false;
        if (t4 < tmax) tmax = t4;
    } else if (num4 > 0.0) {
        return false;
    }
    if (denom5 > 0.0) {
        if (t5 > tmax) return false;
        if (t5 > tmin) tmin = t5;
    } else if (denom5 < 0.0) {
        if (t5 < tmin) return false;
        if (t5 < tmax) tmax = t5;
    } else if (num5 > 0.0) {
        return false;
    }
    clipped_v0 = mix(v0, v1, tmin);
    clipped_v1 = mix(v0, v1, tmax);
    return true;
}

vec2 screen_from_ndc(vec2 ndc, vec2 vp_size)
{
    return (ndc * 0.5 + 0.5) * vp_size;
}

vec2 ndc_from_screen_direction(vec2 screen, vec2 vp_size)
{
    return screen / (0.5 * vp_size);
}

void main(void)
{
    vec4 v0 = gl_in[0].gl_Position;
    vec4 v1 = gl_in[1].gl_Position;

    vec4  v0_clipped;
    vec4  v1_clipped;
    float tmin;
    float tmax;
    if (!clip_line(v0, v1, v0_clipped, v1_clipped, tmin, tmax)) {
        return;
    }

    float thickness0 = mix(vs_line_width[0], vs_line_width[1], tmin);
    float thickness1 = mix(vs_line_width[0], vs_line_width[1], tmax);
    vec4  color0     = mix(vs_color[0],      vs_color[1],      tmin);
    vec4  color1     = mix(vs_color[0],      vs_color[1],      tmax);
    float width0     = get_line_width(v0_clipped, thickness0);
    float width1     = get_line_width(v1_clipped, thickness1);

    vec2 vp_size      = view.viewport.zw;
    vec3 v0_ndc       = v0_clipped.xyz / v0_clipped.w;
    vec3 v1_ndc       = v1_clipped.xyz / v1_clipped.w;
    vec2 v0_screen    = screen_from_ndc(v0_ndc.xy, vp_size);
    vec2 v1_screen    = screen_from_ndc(v1_ndc.xy, vp_size);
    vec2 line_screen  = v1_screen - v0_screen;
    vec2 axis_screen  = normalize(line_screen);
    vec2 side_screen  = vec2(-axis_screen.y, axis_screen.x);
    vec2 axis_ndc     = ndc_from_screen_direction(axis_screen, vp_size);
    vec2 side_ndc     = ndc_from_screen_direction(side_screen, vp_size);

    vec2 offset_a = ( side_ndc - axis_ndc) * width0;
    vec2 offset_b = ( side_ndc + axis_ndc) * width1;
    vec2 offset_c = (-side_ndc + axis_ndc) * width1;
    vec2 offset_d = (-side_ndc - axis_ndc) * width0;

    vec4 screen_endpoints = vec4(v0_screen, v1_screen);

    //  a - - - - b
    //  |  v0->v1 |
    //  d - - - - c

    // Triangle strip: a, d, b, c
    gl_Position = vec4(v0_ndc.xy + offset_a, v0_ndc.z, 1.0);
    v_line_width = width0; v_color = color0; v_start_end = screen_endpoints;
    EmitVertex();

    gl_Position = vec4(v0_ndc.xy + offset_d, v0_ndc.z, 1.0);
    v_line_width = width0; v_color = color0; v_start_end = screen_endpoints;
    EmitVertex();

    gl_Position = vec4(v1_ndc.xy + offset_b, v1_ndc.z, 1.0);
    v_line_width = width1; v_color = color1; v_start_end = screen_endpoints;
    EmitVertex();

    gl_Position = vec4(v1_ndc.xy + offset_c, v1_ndc.z, 1.0);
    v_line_width = width1; v_color = color1; v_start_end = screen_endpoints;
    EmitVertex();

    EndPrimitive();
}
