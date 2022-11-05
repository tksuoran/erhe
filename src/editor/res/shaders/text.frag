in vec2       v_texcoord;
in vec4       v_color;

#if defined(ERHE_BINDLESS_TEXTURE)
in flat uvec2 v_texture;
#endif

void main(void)
{
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
#endif

    vec2  c       = texture(s_texture, v_texcoord).rg;
    float inside  = c.r;
    float outline = c.g;
    float alpha   = max(inside, outline);
    vec3  color   = v_color.a * v_color.rgb * inside;

    out_color = vec4(color, v_color.a * alpha);
}
