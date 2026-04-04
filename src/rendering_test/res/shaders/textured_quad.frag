layout(location = 0) in vec2 v_texcoord;

void main()
{
#if defined(ERHE_OPENGL_BINDLESS_TEXTURE)
    sampler2D s = sampler2D(quad.texture_handle);
    out_color = texture(s, v_texcoord);
#else
    out_color = texture(s_textures[0], v_texcoord);
#endif
}
