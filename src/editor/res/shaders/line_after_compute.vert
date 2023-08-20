out float v_line_width;
out vec4  v_color;
out vec4  v_start_end;

void main()
{
    gl_Position  = vec4(a_position0.xyz, 1.0);
    v_line_width = a_position0.w;
    v_color      = a_color;
    v_start_end  = a_line_start_end;
}
