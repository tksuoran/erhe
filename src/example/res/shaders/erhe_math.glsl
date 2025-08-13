#ifndef ERHE_MATH_GLSL
#define ERHE_MATH_GLSL

const float m_pi    = 3.1415926535897932384626434;
const float m_i_pi  = 0.3183098861837906715377675;

float clamped_dot(vec3 x, vec3 y) {
    return clamp(dot(x, y), 0.001, 1.0);
}

float heaviside(float v) {
    if (v > 0.0) {
        return 1.0;
    } else {
        return 0.0;
    }
}

#endif // ERHE_MATH_GLSL
