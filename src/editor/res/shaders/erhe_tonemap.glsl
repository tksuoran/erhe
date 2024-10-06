#ifndef ERHE_TONEMAP_GLSL
#define ERHE_TONEMAP_GLSL

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

#endif // ERHE_TONEMAP_GLSL
