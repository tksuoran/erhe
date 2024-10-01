in vec2 v_texcoord;

void main()
{
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_downsample = sampler2D(post_processing.downsample_texture);
#endif

    ivec2 texture_size = textureSize(s_downsample, int(post_processing.source_lod));
    vec2 texel_scale = vec2(1.0) / vec2(texture_size);

    const float weight0  = 1.0 / 32.0;
    const float weight1  = 2.0 / 32.0;
    const float weight2  = 1.0 / 32.0;
    const float weight3  = 4.0 / 32.0;
    const float weight4  = 4.0 / 32.0;
    const float weight5  = 2.0 / 32.0;
    const float weight6  = 4.0 / 32.0;
    const float weight7  = 2.0 / 32.0;
    const float weight8  = 4.0 / 32.0;
    const float weight9  = 4.0 / 32.0;
    const float weight10 = 1.0 / 32.0;
    const float weight11 = 2.0 / 32.0;
    const float weight12 = 1.0 / 32.0;

    vec3 sample0  = weight0  * textureLod(s_downsample, v_texcoord + texel_scale * vec2(-2.0, -2.0), post_processing.source_lod).rgb;
    vec3 sample1  = weight1  * textureLod(s_downsample, v_texcoord + texel_scale * vec2( 0.0, -2.0), post_processing.source_lod).rgb;
    vec3 sample2  = weight2  * textureLod(s_downsample, v_texcoord + texel_scale * vec2( 2.0, -2.0), post_processing.source_lod).rgb;
    vec3 sample3  = weight3  * textureLod(s_downsample, v_texcoord + texel_scale * vec2(-1.0, -1.0), post_processing.source_lod).rgb;
    vec3 sample4  = weight4  * textureLod(s_downsample, v_texcoord + texel_scale * vec2( 1.0, -1.0), post_processing.source_lod).rgb;
    vec3 sample5  = weight5  * textureLod(s_downsample, v_texcoord + texel_scale * vec2(-2.0,  0.0), post_processing.source_lod).rgb;
    vec3 sample6  = weight6  * textureLod(s_downsample, v_texcoord + texel_scale * vec2( 0.0,  0.0), post_processing.source_lod).rgb;
    vec3 sample7  = weight7  * textureLod(s_downsample, v_texcoord + texel_scale * vec2( 2.0,  0.0), post_processing.source_lod).rgb;
    vec3 sample8  = weight8  * textureLod(s_downsample, v_texcoord + texel_scale * vec2(-1.0,  1.0), post_processing.source_lod).rgb;
    vec3 sample9  = weight9  * textureLod(s_downsample, v_texcoord + texel_scale * vec2( 1.0,  1.0), post_processing.source_lod).rgb;
    vec3 sample10 = weight10 * textureLod(s_downsample, v_texcoord + texel_scale * vec2(-2.0,  2.0), post_processing.source_lod).rgb;
    vec3 sample11 = weight11 * textureLod(s_downsample, v_texcoord + texel_scale * vec2( 0.0,  2.0), post_processing.source_lod).rgb;
    vec3 sample12 = weight12 * textureLod(s_downsample, v_texcoord + texel_scale * vec2( 2.0,  2.0), post_processing.source_lod).rgb;

    out_color      = vec4(0.0, 0.0, 0.0, 1.0);
    out_color.rgb += sample0;
    out_color.rgb += sample1;
    out_color.rgb += sample2;
    out_color.rgb += sample3;
    out_color.rgb += sample4;
    out_color.rgb += sample5;
    out_color.rgb += sample6;
    out_color.rgb += sample7;
    out_color.rgb += sample8;
    out_color.rgb += sample9;
    out_color.rgb += sample10;
    out_color.rgb += sample11;
    out_color.rgb += sample12;
}
