layout(location = 0) out vec4  vs_color;
layout(location = 1) out float vs_line_width;

void main()
{
    gl_Position   = view.clip_from_world * vec4(a_position.xyz, 1.0);
    vs_color      = a_color_0;
    vs_line_width = a_position.w;
}
