#define a_line_start_end a_custom

out float v_line_width;
out vec4  v_color;
out vec4  v_start_end;

void main()
{
    gl_Position  = vec4(a_position.xyz, 1.0);
    v_line_width = a_position.w;
    v_color      = a_color_0;
    v_start_end  = a_line_start_end;
}
