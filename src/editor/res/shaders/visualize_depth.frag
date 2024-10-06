#include "erhe_srgb.glsl"

in vec2 v_texcoord;

const vec3 temperature_colors[] = vec3[](
    vec3(  4.,  35., 51.) / 255.,
    vec3( 23.,  51.,122.) / 255.,
    vec3( 85.,  59.,157.) / 255.,
    vec3(129.,  79.,143.) / 255.,
    vec3(175.,  95.,130.) / 255.,
    vec3(222., 112.,101.) / 255.,
    vec3(249., 146., 66.) / 255.,
    vec3(249., 196., 65.) / 255.,
    vec3(232., 250., 91.) / 255.);

//vec3(176.,29.,30.) / 255.,
//vec3(241.,104.,38.) / 255.,
//vec3(234.,211.,95.) / 255.,
//vec3(0.,187.,173.) / 255.,
//vec3(0.,107.,228.) / 255.,
//vec3(126.,99.,180.) / 255.);


vec3 temperature_old(float t) {
    t *= float(temperature_colors.length() - 1);
    return srgb_to_linear(mix(temperature_colors[int(t)], temperature_colors[int(t) + 1], smoothstep(0.0, 1.0, fract(t))));
}

// http://c0de517e.blogspot.com/2017/11/coder-color-palettes-for-data.html
vec3 temperature(float x)
{
    x = clamp (x, 0.0, 1.0);
    float r = -0.121 + 0.893 * x + 0.276 * sin (1.94 - 5.69 * x);
    float g = 0.07 + 0.947 * x;
    float b = 0.107 + (1.5 - 1.22 * x) * x;
    return srgb_to_linear(vec3(r, g, b));
}

// https://www.shadertoy.com/view/4dsSzr

// By Morgan McGuire @morgan3d, http://graphicscodex.com
// Reuse permitted under the BSD license.

//float square(float s) { return s * s; }
//vec3 square(vec3 s) { return s * s; }
//
//vec3 hueGradient(float t) {
//    vec3 p = abs(fract(t + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
//  return (clamp(p - 1.0, 0.0, 1.0));
//}
//
//
//vec3 techGradient(float t) {
//  return pow(vec3(t + 0.01), vec3(120.0, 10.0, 180.0));
//}
//
//
//vec3 fireGradient(float t) {
//  return max(pow(vec3(min(t * 1.02, 1.0)), vec3(1.7, 25.0, 100.0)),
//         vec3(0.06 * pow(max(1.0 - abs(t - 0.35), 0.0), 5.0)));
//}
//
//
//vec3 desertGradient(float t) {
//  float s = sqrt(clamp(1.0 - (t - 0.4) / 0.6, 0.0, 1.0));
//  vec3 sky = sqrt(mix(vec3(1, 1, 1), vec3(0, 0.8, 1.0), smoothstep(0.4, 0.9, t)) * vec3(s, s, 1.0));
//  vec3 land = mix(vec3(0.7, 0.3, 0.0), vec3(0.85, 0.75 + max(0.8 - t * 20.0, 0.0), 0.5), square(t / 0.4));
//  return clamp((t > 0.4) ? sky : land, 0.0, 1.0) * clamp(1.5 * (1.0 - abs(t - 0.4)), 0.0, 1.0);
//}
//
//
//vec3 electricGradient(float t) {
//  return clamp( vec3(t * 8.0 - 6.3, square(smoothstep(0.6, 0.9, t)), pow(t, 3.0) * 1.7), 0.0, 1.0);
//}
//
//
//vec3 neonGradient(float t) {
//  return clamp(vec3(t * 1.3 + 0.1, square(abs(0.43 - t) * 1.7), (1.0 - t) * 1.7), 0.0, 1.0);
//}
//
//
//vec3 heatmapGradient(float t) {
//  return clamp((pow(t, 1.5) * 0.8 + 0.2) * vec3(smoothstep(0.0, 0.35, t) + t * 0.5, smoothstep(0.5, 1.0, t), max(1.0 - t * 1.7, t * 7.0 - 6.0)), 0.0, 1.0);
//}
//
//
//vec3 rainbowGradient(float t) {
//  vec3 c = 1.0 - pow(abs(vec3(t) - vec3(0.65, 0.5, 0.2)) * vec3(3.0, 3.0, 5.0), vec3(1.5, 1.3, 1.7));
//  c.r = max((0.15 - square(abs(t - 0.04) * 5.0)), c.r);
//  c.g = (t < 0.5) ? smoothstep(0.04, 0.45, t) : c.g;
//  return clamp(c, 0.0, 1.0);
//}
//
//
//vec3 brightnessGradient(float t) {
//  return vec3(t * t);
//}
//
//
//vec3 grayscaleGradient(float t) {
//  return vec3(t);
//}
//
//
//vec3 stripeGradient(float t) {
//  return vec3(mod(floor(t * 32.0), 2.0) * 0.2 + 0.8);
//}
//
//
//vec3 ansiGradient(float t) {
//  return mod(floor(t * vec3(8.0, 4.0, 2.0)), 2.0);
//}



void main(void)
{
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2DArray s_shadow = sampler2DArray(light_block.shadow_texture);
#endif

    float sampled_depth = texture(
        s_shadow,
        vec3(
            v_texcoord,
            float(light_control_block.light_index)
        )
    ).x;

    out_color = vec4(temperature(sampled_depth), 1.0);
    //out_color = vec4(temperature(sampled_depth), 1.0);
    //out_color = vec4(temperature(v_texcoord.x), 1.0);
    //out_color = vec4(0.0, 1.0, 0.0, 1.0);
}
