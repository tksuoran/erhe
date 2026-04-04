layout(location = 0) in vec2 v_texcoord;
layout(location = 1) in vec4 v_color;

#if defined(ERHE_OPENGL_BINDLESS_TEXTURE)
layout(location = 2) flat in uvec2 v_texture;
#endif

// layout(location =  3) flat in uint  v_vertex_id;
// layout(location =  4) flat in uint  v_glyph_index;
// layout(location =  5) flat in uint  v_quad_corner;
// layout(location =  6) flat in uint  v_vertex_index;
// layout(location =  7) flat in uvec4 v_data;
// layout(location =  8) flat in int   v_x;
// layout(location =  9) flat in int   v_y;
// layout(location = 10) flat in vec2  v_zw;

void main(void)
{
#if defined(ERHE_OPENGL_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
#endif

    vec2  c       = texture(s_texture, v_texcoord).rg;
    float inside  = c.r;
    float outline = c.g;
    float alpha   = max(inside, outline);
    vec3  color   = v_color.a * v_color.rgb * inside;

    out_color = vec4(color, v_color.a * alpha);
}
