layout(location = 0) in vec2       v_texcoord;
layout(location = 1) in vec4       v_color;

#if defined(ERHE_OPENGL_BINDLESS_TEXTURE)
layout(location = 2) flat in uvec2 v_texture;
#elif defined(ERHE_VULKAN_DESCRIPTOR_INDEXING)
layout(location = 2) flat in uint v_texture_index;
#endif

void main(void)
{
#if defined(ERHE_OPENGL_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
    vec4 t_color = texture(s_texture, v_texcoord);
#elif defined(ERHE_VULKAN_DESCRIPTOR_INDEXING)
    vec4 t_color = texture(erhe_texture_heap[v_texture_index], v_texcoord);
#else
    vec4 t_color = texture(s_texture, v_texcoord);
#endif

    out_color = v_color * t_color;
}
