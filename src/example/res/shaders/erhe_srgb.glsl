#ifndef ERHE_SRGB_GLSL
#define ERHE_SRGB_GLSL

float srgb_to_linear(float x) {
    if (x <= 0.04045) {
        return x / 12.92;
    } else {
        return pow((x + 0.055) / 1.055, 2.4);
    }
}

vec3 srgb_to_linear(vec3 v) {
    return vec3(
        srgb_to_linear(v.x),
        srgb_to_linear(v.y),
        srgb_to_linear(v.z)
    );
}

vec2 srgb_to_linear(vec2 v) {
    return vec2(srgb_to_linear(v.r), srgb_to_linear(v.g));
}

#endif // ERHE_SRGB_GLSL
