layout(location = 0) out float v_line_width;
layout(location = 1) out vec4  v_color;
layout(location = 2) out vec4  v_start_end;

void main()
{
    gl_Position  = vec4(a_position.xyz, 1.0);
    v_line_width = a_position.w;
    v_color      = a_color_0;
    v_start_end  = a_custom_0;
}
