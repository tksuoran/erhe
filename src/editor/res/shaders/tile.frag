layout(location = 0) in vec2       v_texcoord;
layout(location = 1) in vec4       v_color;
layout(location = 2) in flat uvec2 v_texture;

void main(void)
{
    sampler2D s_texture = sampler2D(v_texture);
    vec4      t_color   = texture(s_texture, v_texcoord);

    out_color = v_color * t_color;
}
