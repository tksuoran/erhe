in vec2 v_texcoord;

float srgb_to_linear(float x)
{
    if (x <= 0.04045)
    {
        return x / 12.92;
    }
    else
    {
        return pow((x + 0.055) / 1.055, 2.4);
    }
}

vec4 srgb_to_linear(vec4 v)
{
    return vec4(
        srgb_to_linear(v.r),
        srgb_to_linear(v.g),
        srgb_to_linear(v.b),
        v.a
    );
}

void main()
{
    //out_color = srgb_to_linear(texture(s_gui_texture, v_texcoord));
    out_color = texture(s_gui_texture, v_texcoord);
}
