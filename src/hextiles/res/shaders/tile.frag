layout(location = 0) in vec2       v_texcoord;
layout(location = 1) in vec4       v_color;

#if defined(ERHE_BINDLESS_TEXTURE)
layout(location = 2) in flat uvec2 v_texture;
#endif

void main(void)
{
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
#endif

    vec4      t_color   = texture(s_texture, v_texcoord);

    out_color = v_color * t_color;
    //out_color = vec4(v_texcoord.xy, 0.0, 1.0);
}
