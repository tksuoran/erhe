layout(location = 0) in vec2 v_texcoord;

void main()
{
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2D s_tex0 = sampler2D(separate_tex.texture_handle_0);
    sampler2D s_tex1 = sampler2D(separate_tex.texture_handle_1);
    sampler2D s_tex2 = sampler2D(separate_tex.texture_handle_2);
#endif

    uint count = separate_tex.texture_count;

    vec4 result = vec4(0.0);
    if (count > 0u) { result += texture(s_tex0, v_texcoord); }
    if (count > 1u) { result += texture(s_tex1, v_texcoord); }
    if (count > 2u) { result += texture(s_tex2, v_texcoord); }
    out_color = vec4(result.rgb / float(count), 1.0);
}
