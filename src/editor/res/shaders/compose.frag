in vec2 v_texcoord;

vec3 tonemap_reinhard(vec3 color)
{
    return color / (color + vec3(1.0));
}

void main()
{
    vec3 sum = vec3(0.0, 0.0, 0.0);
    for (uint i = 1; i < post_processing.texture_count; ++i)
    {
        sum += texture(sampler2D(post_processing.source_texture[i]), v_texcoord).rgb;
    }
    float count = float(post_processing.texture_count) * 10.0;

    vec3 color = sum / count;
    color.rgb += texture(sampler2D(post_processing.source_texture[0]), v_texcoord).rgb;

    out_color.rgb = tonemap_reinhard(color);

    out_color.a    = 1.0;

    //out_color = texture(sampler2D(post_processing.source_texture[2]), v_texcoord);
}
