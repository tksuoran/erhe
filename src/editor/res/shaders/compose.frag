in vec2 v_texcoord;

void main()
{
    sampler2D s_source = sampler2D(post_processing.source_texture[0]);

    vec3  center = texture(s_source, v_texcoord).rgb;
    out_color    = vec4(center, 1.0);
}
