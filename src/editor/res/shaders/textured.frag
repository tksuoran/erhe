in vec2 v_texcoord;

void main(void)
{
    vec4 c = texture(background_texture, v_texcoord.xy).rgba;
    out_color = materials.color * c;
}
