out      vec2  v_texcoord;
out      vec4  v_color;

#if defined(ERHE_BINDLESS_TEXTURE)
out flat uvec2 v_texture;
#endif

void main()
{
    gl_Position = projection.clip_from_window * vec4(a_position, 1.0);

#if defined(ERHE_BINDLESS_TEXTURE)
    v_texture   = projection.texture;
#endif

    v_texcoord  = a_texcoord_0;
    v_color     = a_color_0;
}

