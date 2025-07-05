layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec4 v_color;
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
layout(location = 2) out flat uvec2 v_texture;
#endif
// layout(location =  3) out flat uint  v_vertex_id;
// layout(location =  4) out flat uint  v_glyph_index;
// layout(location =  5) out flat uint  v_quad_corner;
// layout(location =  6) out flat uint  v_vertex_index;
// layout(location =  7) out flat uvec4 v_data;
// layout(location =  8) out flat int   v_x;
// layout(location =  9) out flat int   v_y;
// layout(location = 10) out flat vec2  v_zw;

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
    uvec4 packed_data  = vertex_ssbo.data[vertex_index];
    int x = int( packed_data[0]        & 0xffffu);
    int y = int((packed_data[0] >> 16) & 0xffffu);
    if (x >= 0x8000) x -= 0x10000;
    if (y >= 0x8000) y -= 0x10000;
    vec2 zw = unpackSnorm2x16(packed_data[1]);
    vec4 a_position = vec4(float(x), float(y), zw);
    v_color     = unpackUnorm4x8 (packed_data[2]);
    v_texcoord  = unpackUnorm2x16(packed_data[3]);
    gl_Position = projection.clip_from_window * vec4(a_position);

#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    v_texture  = projection.texture;
#endif

    // v_vertex_id     = gl_VertexID;
    // v_glyph_index   = glyph_index;
    // v_quad_corner   = quad_corner;
    // v_vertex_index  = vertex_index;
    // v_data = packed;
    // v_x  = x;
    // v_y  = y;
    // v_zw = zw;

}

