in vec2 v_texcoord;

void main(void)
{
    vec2 c = texture(font_texture, v_texcoord.xy).rg;
    float inside  = c.r;
    float outline = c.g;
    float alpha   = max(inside, outline * 0.25);
    out_color = vec4(0.0, 0.0, 0.0, alpha);
}
