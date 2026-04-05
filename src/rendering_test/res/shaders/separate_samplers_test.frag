layout(location = 0) in vec2 v_texcoord;

void main()
{
    uint count = separate_tex.texture_count;

#if defined(ERHE_OPENGL_BINDLESS_TEXTURE)
    sampler2D s_tex0 = sampler2D(separate_tex.texture_handle_0);
    sampler2D s_tex1 = sampler2D(separate_tex.texture_handle_1);
    sampler2D s_tex2 = sampler2D(separate_tex.texture_handle_2);
    vec4 result = vec4(0.0);
    if (count > 0u) { result += texture(s_tex0, v_texcoord); }
    if (count > 1u) { result += texture(s_tex1, v_texcoord); }
    if (count > 2u) { result += texture(s_tex2, v_texcoord); }
#elif defined(ERHE_VULKAN_DESCRIPTOR_INDEXING)
    vec4 result = vec4(0.0);
    if (count > 0u) { result += texture(erhe_texture_heap[separate_tex.texture_handle_0.x], v_texcoord); }
    if (count > 1u) { result += texture(erhe_texture_heap[separate_tex.texture_handle_1.x], v_texcoord); }
    if (count > 2u) { result += texture(erhe_texture_heap[separate_tex.texture_handle_2.x], v_texcoord); }
#else
    vec4 result = vec4(0.0);
    if (count > 0u) { result += texture(s_tex0, v_texcoord); }
    if (count > 1u) { result += texture(s_tex1, v_texcoord); }
    if (count > 2u) { result += texture(s_tex2, v_texcoord); }
#endif
    out_color = vec4(result.rgb / float(count), 1.0);
}
