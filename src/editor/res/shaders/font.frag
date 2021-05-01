in vec2 v_texcoord;

void main(void)
{
    vec2 c = texture(font_texture, v_texcoord.xy).rg;
    float inside  = c.r;
    float outline = c.g;
    float alpha   = max(inside, outline * 0.25);
    vec3  color   = materials.color.rgb * pow(inside, 0.5);
    out_color = vec4(color, alpha);
}
