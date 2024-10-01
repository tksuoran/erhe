in vec2 v_texcoord;

float luminance(vec3 c) // BT.709
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Tone Mapping for HDR Image using Optimization – A New Closed Form Solution
// http://www.cs.nott.ac.uk/~pszqiu/webpages/Papers/icpr2006-hdri-camera.pdf
vec3 tonemap_log(vec3 x)
{
    float small_value      = 1.0 /    32.0; // 0.03125
    float very_small_value = 0.5 / 65536.0; // 0.00000762939
    float D_max            = 1.0; // maximum display luminance
    float D_min            = 0.0; // minimum display luminance
    float I_max            = post_processing.tonemap_luminance_max;
    float I_min            = 0.0; // minimum scene luminance
    float alpha            = post_processing.tonemap_alpha;
    float tau              = alpha * (I_max - I_min);
    float I_in             = luminance(x);
    float numerator        = log(I_in  + tau) - log(I_min + tau);
    float denominator      = log(I_max + tau) - log(I_min + tau);
    float I_out            = (D_max - D_min) * (numerator / denominator) + D_min;
    float scale            = I_out / max(I_in, very_small_value);
    return x * scale + I_in * small_value;
}

// https://www.shadertoy.com/view/WdjSW3
vec3 tonemap_aces(vec3 x) {
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

// Input color is non-negative and resides in the Linear Rec. 709 color space.
// Output color is also Linear Rec. 709, but in the [0, 1] range.

vec3 PBRNeutralToneMapping( vec3 color ) {
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;

  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;

  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) return color;

  const float d = 1. - startCompression;
  float newPeak = 1. - d * d / (peak + d - startCompression);
  color *= newPeak / peak;

  float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
  return mix(color, newPeak * vec3(1, 1, 1), g);
}

float get_weight()
{
    float level_count = post_processing.level_count;
    float k = level_count - post_processing.source_lod;
    return 1.0 / k;
}

void main()
{
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_downsample = sampler2D(post_processing.downsample_texture);
    sampler2D s_upsample   = sampler2D(post_processing.upsample_texture);
#endif
    float destination_lod = post_processing.source_lod - 1.0;

    ivec2 texture_size = textureSize(s_upsample, int(post_processing.source_lod));
    vec2 texel_scale = vec2(1.0) / vec2(texture_size) * post_processing.upsample_radius;

    vec3 curr = textureLod(s_downsample, v_texcoord, destination_lod).rgb;
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

    if (destination_lod == 0.0) {
        vec3 color = mix(curr, down, post_processing.mix_weight);
        //out_color.rgb = PBRNeutralToneMapping(color);
        //out_color.rgb = tonemap_aces(color);
        //if (v_texcoord.x < 1.0 / 3.0) {
            out_color.rgb = PBRNeutralToneMapping(color);
        //} 
        //else
        //if (v_texcoord.x < 2.0 / 3.0) {
        //    out_color.rgb = color;
        //}
        //else
        //{
        //    out_color.rgb = tonemap_aces(color);
        //}
        //out_color.rgb = (v_texcoord.x > 1.0/3.05) ? tonemap_aces(color) : tonemap_log(color);
    } else {
        vec3 color = mix(curr, down, post_processing.mix_weight);
        out_color.rgb = color;
    }
    out_color.a = 1.0;
}

