layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec4 v_color;
#if defined(ERHE_OPENGL_BINDLESS_TEXTURE)
layout(location = 2) flat out uvec2 v_texture;
#endif

void main()
{
    //  3---2
    //  |  /|
    //  | / |
    //  |/  |
    //  0---1

    const uint indices[6] = uint[6]( 2u, 1u, 0u,  2u, 0u, 3u );
    uint  glyph_index  = gl_VertexID / 6;
    uint  quad_corner  = indices[gl_VertexID % 6];
    uint  vertex_index = glyph_index * 4 + quad_corner;

#if defined(ERHE_VERTEX_DATA_TEXTURE_BUFFER)
    uvec4 packed_data  = texelFetch(s_vertex_data, int(vertex_index) + int(projection.vertex_data_offset));
#else
    uvec4 packed_data  = vertex_ssbo.data[vertex_index];
#endif

    int x = int( packed_data[0]        & 0xffffu);
    int y = int((packed_data[0] >> 16) & 0xffffu);
    if (x >= 0x8000) x -= 0x10000;
    if (y >= 0x8000) y -= 0x10000;
    vec2 zw = unpackSnorm2x16(packed_data[1]);
    vec4 a_position = vec4(float(x), float(y), zw);
    v_color     = unpackUnorm4x8 (packed_data[2]);
    v_texcoord  = unpackUnorm2x16(packed_data[3]);
    gl_Position = projection.clip_from_window * vec4(a_position);

#if defined(ERHE_OPENGL_BINDLESS_TEXTURE)
    v_texture  = projection.texture;
#endif

}

