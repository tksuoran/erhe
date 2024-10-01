in vec2 v_texcoord;

float luminance(vec3 c) // BT.709
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 karis_average(vec3 c1, vec3 c2, vec3 c3, vec3 c4)
{
    float w1 = 1.0 / (luminance(c1.rgb) + 1.0);
    float w2 = 1.0 / (luminance(c2.rgb) + 1.0);
    float w3 = 1.0 / (luminance(c3.rgb) + 1.0);
    float w4 = 1.0 / (luminance(c4.rgb) + 1.0);

    return (c1 * w1 + c2 * w2 + c3 * w3 + c4 * w4) / (w1 + w2 + w3 + w4);
}

void main()
{
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_downsample = sampler2D(post_processing.downsample_texture);
#endif

    ivec2 texture_size = textureSize(s_downsample, int(post_processing.source_lod));
    vec2 texel_scale = vec2(1.0) / vec2(texture_size);

    vec3 sample0  = textureLod(s_downsample, v_texcoord + texel_scale * vec2(-2.0, -2.0), post_processing.source_lod).rgb;
    vec3 sample1  = textureLod(s_downsample, v_texcoord + texel_scale * vec2( 0.0, -2.0), post_processing.source_lod).rgb;
    vec3 sample2  = textureLod(s_downsample, v_texcoord + texel_scale * vec2( 2.0, -2.0), post_processing.source_lod).rgb;
    vec3 sample3  = textureLod(s_downsample, v_texcoord + texel_scale * vec2(-1.0, -1.0), post_processing.source_lod).rgb;
    vec3 sample4  = textureLod(s_downsample, v_texcoord + texel_scale * vec2( 1.0, -1.0), post_processing.source_lod).rgb;
    vec3 sample5  = textureLod(s_downsample, v_texcoord + texel_scale * vec2(-2.0,  0.0), post_processing.source_lod).rgb;
    vec3 sample6  = textureLod(s_downsample, v_texcoord + texel_scale * vec2( 0.0,  0.0), post_processing.source_lod).rgb;
    vec3 sample7  = textureLod(s_downsample, v_texcoord + texel_scale * vec2( 2.0,  0.0), post_processing.source_lod).rgb;
    vec3 sample8  = textureLod(s_downsample, v_texcoord + texel_scale * vec2(-1.0,  1.0), post_processing.source_lod).rgb;
    vec3 sample9  = textureLod(s_downsample, v_texcoord + texel_scale * vec2( 1.0,  1.0), post_processing.source_lod).rgb;
    vec3 sample10 = textureLod(s_downsample, v_texcoord + texel_scale * vec2(-2.0,  2.0), post_processing.source_lod).rgb;
    vec3 sample11 = textureLod(s_downsample, v_texcoord + texel_scale * vec2( 0.0,  2.0), post_processing.source_lod).rgb;
    vec3 sample12 = textureLod(s_downsample, v_texcoord + texel_scale * vec2( 2.0,  2.0), post_processing.source_lod).rgb;

    out_color     = vec4(0.0, 0.0, 0.0, 1.0);
    //out_color.rgb = sample6;
    out_color.rgb += karis_average(sample3, sample4, sample8,  sample9 ) * 0.0100;
    out_color.rgb += karis_average(sample0, sample1, sample5,  sample6 ) * 0.125;
    out_color.rgb += karis_average(sample1, sample2, sample6,  sample7 ) * 0.125;
    out_color.rgb += karis_average(sample5, sample6, sample10, sample11) * 0.125;
    out_color.rgb += karis_average(sample0, sample1, sample5,  sample6 ) * 0.125;

    //out_color.rgb += karis_average(
    //                      karis_average(sample1, sample2, sample6,  sample7 ),
    //                      karis_average(sample5, sample6, sample10, sample11),
    //                      karis_average(sample6, sample7, sample11, sample12),
    //                      karis_average(sample6, sample7, sample11, sample12)
    //                 ) * 0.125;
}
