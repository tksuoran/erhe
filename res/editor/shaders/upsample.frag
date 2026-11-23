#include "erhe_tonemap.glsl"

layout(location = 0) in vec2 v_texcoord;

void main()
{
    vec2 texel_scale = post_processing.texel_scale * post_processing.upsample_radius;

#if defined(LAST_PASS)
    // Last upsample pass (writes pyramid level 0). curr is the original
    // input viewport texture (pyramid level 0), bound to s_input.
    vec3 curr = textureLod(s_input, v_texcoord, 0.0).rgb;
#else
    // curr is the destination level's downsample content, bound to a
    // dedicated single-level texture at s_downsample_dst.
    vec3 curr = textureLod(s_downsample_dst, v_texcoord, 0.0).rgb;
#endif

    // SOURCE is wired to s_downsample (first upsample pass) or s_upsample
    // (all other upsample passes). Each bound texture is single-level.
    vec3 down = vec3(0.0, 0.0, 0.0);
    down += (1.0 / 16.0) * textureLod(SOURCE, v_texcoord + texel_scale * vec2(-1.0, -1.0), 0.0).rgb;
    down += (2.0 / 16.0) * textureLod(SOURCE, v_texcoord + texel_scale * vec2( 0.0, -1.0), 0.0).rgb;
    down += (1.0 / 16.0) * textureLod(SOURCE, v_texcoord + texel_scale * vec2( 1.0, -1.0), 0.0).rgb;
    down += (2.0 / 16.0) * textureLod(SOURCE, v_texcoord + texel_scale * vec2(-1.0,  0.0), 0.0).rgb;
    down += (4.0 / 16.0) * textureLod(SOURCE, v_texcoord + texel_scale * vec2( 0.0,  0.0), 0.0).rgb;
    down += (2.0 / 16.0) * textureLod(SOURCE, v_texcoord + texel_scale * vec2( 1.0,  0.0), 0.0).rgb;
    down += (1.0 / 16.0) * textureLod(SOURCE, v_texcoord + texel_scale * vec2(-1.0,  1.0), 0.0).rgb;
    down += (2.0 / 16.0) * textureLod(SOURCE, v_texcoord + texel_scale * vec2( 0.0,  1.0), 0.0).rgb;
    down += (1.0 / 16.0) * textureLod(SOURCE, v_texcoord + texel_scale * vec2( 1.0,  1.0), 0.0).rgb;

#if defined(LAST_PASS)
    vec3 color = mix(curr, down, post_processing.mix_weight);
    out_color.rgb = PBRNeutralToneMapping(color);
    // The final output keeps the input's alpha (transparent background where the sky is off).
    out_color.a = textureLod(s_input, v_texcoord, 0.0).a;
#else
    vec3 color = mix(curr, down, post_processing.mix_weight);
    out_color.rgb = color;
    out_color.a = 1.0;
#endif
}
