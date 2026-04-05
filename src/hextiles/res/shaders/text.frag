layout(location = 0) in vec2 v_texcoord;
layout(location = 1) in vec4 v_color;

#if defined(ERHE_OPENGL_BINDLESS_TEXTURE)
layout(location = 2) flat in uvec2 v_texture;
#elif defined(ERHE_VULKAN_DESCRIPTOR_INDEXING)
layout(location = 2) flat in uint v_texture_index;
#endif

void main(void)
{
#if defined(ERHE_OPENGL_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
    vec2  c       = texture(s_texture, v_texcoord).rg;
#elif defined(ERHE_VULKAN_DESCRIPTOR_INDEXING)
    vec2  c       = texture(erhe_texture_heap[v_texture_index], v_texcoord).rg;
#else
    vec2  c       = texture(s_texture, v_texcoord).rg;
#endif
    float inside  = c.r;
    float outline = c.g;
    float alpha   = max(inside, outline);
    vec3  color   = v_color.a * v_color.rgb * inside;

    out_color = vec4(color, v_color.a * alpha);
}
