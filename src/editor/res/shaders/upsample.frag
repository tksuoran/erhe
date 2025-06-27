#include "erhe_tonemap.glsl"

layout(location = 0) in vec2 v_texcoord;

float get_weight()
{
    float level_count = post_processing.level_count;
    float k = level_count - post_processing.source_lod;
    return 1.0 / k;
}

void main()
{
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_input      = sampler2D(post_processing.input_texture);
    sampler2D s_downsample = sampler2D(post_processing.downsample_texture);
    sampler2D s_upsample   = sampler2D(post_processing.upsample_texture);
#endif
    float destination_lod = post_processing.source_lod - 1.0;

    ivec2 texture_size = textureSize(s_upsample, int(post_processing.source_lod));
    vec2 texel_scale = vec2(1.0) / vec2(texture_size) * post_processing.upsample_radius;

#if defined(LAST_OUTPUT)
    vec3 curr = textureLod(s_input, v_texcoord, destination_lod).rgb;
#else
    vec3 curr = textureLod(s_downsample, v_texcoord, destination_lod).rgb;
#endif

    vec3 down = vec3(0.0, 0.0, 0.0);
    down += (1.0 / 16.0) * textureLod(s_upsample, v_texcoord + texel_scale * vec2(-1.0, -1.0), post_processing.source_lod).rgb;
    down += (2.0 / 16.0) * textureLod(s_upsample, v_texcoord + texel_scale * vec2( 0.0, -1.0), post_processing.source_lod).rgb;
    down += (1.0 / 16.0) * textureLod(s_upsample, v_texcoord + texel_scale * vec2( 1.0, -1.0), post_processing.source_lod).rgb;
    down += (2.0 / 16.0) * textureLod(s_upsample, v_texcoord + texel_scale * vec2(-1.0,  0.0), post_processing.source_lod).rgb;
    down += (4.0 / 16.0) * textureLod(s_upsample, v_texcoord + texel_scale * vec2( 0.0,  0.0), post_processing.source_lod).rgb;
    down += (2.0 / 16.0) * textureLod(s_upsample, v_texcoord + texel_scale * vec2( 1.0,  0.0), post_processing.source_lod).rgb;
    down += (1.0 / 16.0) * textureLod(s_upsample, v_texcoord + texel_scale * vec2(-1.0,  1.0), post_processing.source_lod).rgb;
    down += (2.0 / 16.0) * textureLod(s_upsample, v_texcoord + texel_scale * vec2( 0.0,  1.0), post_processing.source_lod).rgb;
    down += (1.0 / 16.0) * textureLod(s_upsample, v_texcoord + texel_scale * vec2( 1.0,  1.0), post_processing.source_lod).rgb;

#if defined(LAST_OUTPUT)
    vec3 color = mix(curr, down, post_processing.mix_weight);
    out_color.rgb = PBRNeutralToneMapping(color);
#else
    vec3 color = mix(curr, down, post_processing.mix_weight);
    out_color.rgb = color;
#endif
    out_color.a = 1.0;
}

