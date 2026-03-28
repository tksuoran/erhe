layout(location = 0) in vec4 v_color;

void main()
{
    out_color = vec4(v_color.rgb * v_color.a, v_color.a);
}
