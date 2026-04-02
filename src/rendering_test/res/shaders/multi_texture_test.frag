layout(location = 0) in vec2 v_texcoord;

void main()
{
    uint count = multi_tex.texture_count;

#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    vec4 result = vec4(0.0);
    if (count > 0u) { sampler2D s0 = sampler2D(multi_tex.texture_handle_0); result += texture(s0, v_texcoord); }
    if (count > 1u) { sampler2D s1 = sampler2D(multi_tex.texture_handle_1); result += texture(s1, v_texcoord); }
    if (count > 2u) { sampler2D s2 = sampler2D(multi_tex.texture_handle_2); result += texture(s2, v_texcoord); }
    out_color = vec4(result.rgb / float(count), 1.0);
#else
    vec4 result = vec4(0.0);
    if (count > 0u) { result += texture(s_textures[0], v_texcoord); }
    if (count > 1u) { result += texture(s_textures[1], v_texcoord); }
    if (count > 2u) { result += texture(s_textures[2], v_texcoord); }
    out_color = vec4(result.rgb / float(count), 1.0);
#endif
}
