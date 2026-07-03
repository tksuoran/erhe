#include "erhe_texgen/common_library.hpp"

#include <array>

namespace erhe::texgen {

namespace {

// GLSL functions ported from Material Maker (shader_functions.tres),
// https://github.com/RodZill4/material-maker - MIT license,
// Copyright (c) 2018-2024 Rodolphe Suescun and contributors.
constexpr std::string_view c_common_library_glsl{
    "float dot2(vec2 x) {\n"
    "    return dot(x, x);\n"
    "}\n"
    "\n"
    "float rand(vec2 x) {\n"
    "    return fract(cos(mod(dot(x, vec2(13.9898, 8.141)), 3.14)) * 43758.5);\n"
    "}\n"
    "\n"
    "vec2 rand2(vec2 x) {\n"
    "    return fract(cos(mod(vec2(dot(x, vec2(13.9898, 8.141)),\n"
    "                              dot(x, vec2(3.4562, 17.398))), vec2(3.14, 3.14))) * 43758.5);\n"
    "}\n"
    "\n"
    "vec3 rand3(vec2 x) {\n"
    "    return fract(cos(mod(vec3(dot(x, vec2(13.9898, 8.141)),\n"
    "                              dot(x, vec2(3.4562, 17.398)),\n"
    "                              dot(x, vec2(13.254, 5.867))), vec3(3.14, 3.14, 3.14))) * 43758.5);\n"
    "}\n"
    "\n"
    "vec3 rand3_4d(vec4 x) {\n"
    "    return fract(cos(mod(vec3(\n"
    "        dot(x, vec4(13.9898, 8.141, 3.4562, 17.398)),\n"
    "        dot(x + 0.1234, vec4(3.4562, 17.398, 13.254, 5.867)),\n"
    "        dot(x + 0.5678, vec4(11.1357, 23.234, 95.346, 9.123))\n"
    "    ), vec3(3.14))) * 43758.5);\n"
    "}\n"
    "\n"
    "vec3 rgb2hsv(vec3 c) {\n"
    "    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);\n"
    "    vec4 p = c.g < c.b ? vec4(c.bg, K.wz) : vec4(c.gb, K.xy);\n"
    "    vec4 q = c.r < p.x ? vec4(p.xyw, c.r) : vec4(c.r, p.yzx);\n"
    "\n"
    "    float d = q.x - min(q.w, q.y);\n"
    "    float e = 1.0e-10;\n"
    "    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);\n"
    "}\n"
    "\n"
    "vec3 hsv2rgb(vec3 c) {\n"
    "    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n"
    "    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
    "    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
    "}\n"
    "\n"
    "float param_rnd(float minimum, float maximum, float seed) {\n"
    "    return minimum + (maximum - minimum) * rand(vec2(seed));\n"
    "}\n"
    "\n"
    "float param_rndi(float minimum, float maximum, float seed) {\n"
    "    return floor(param_rnd(minimum, maximum + 1.0, seed));\n"
    "}\n"
};

// Call-site patterns for the reference heuristic. Note "rand(" is not a
// substring of "param_rnd(", so each entry matches only real call sites of
// that function (or a longer identifier ending in it, which is an accepted
// false positive - it only costs including the library).
constexpr std::array<std::string_view, 9> c_common_library_functions{
    "dot2(",
    "rand(",
    "rand2(",
    "rand3(",
    "rand3_4d(",
    "rgb2hsv(",
    "hsv2rgb(",
    "param_rnd(",
    "param_rndi("
};

} // anonymous namespace

auto common_library_glsl() -> std::string_view
{
    return c_common_library_glsl;
}

auto references_common_library(const std::string_view shader_text) -> bool
{
    for (const std::string_view function_pattern : c_common_library_functions) {
        if (shader_text.find(function_pattern) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

} // namespace erhe::texgen
