#include "color_conversions.hpp"
#include <cmath>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.hpp"

namespace ImGG { namespace internal {

namespace {
struct Vec3 {
    float x;
    float y;
    float z;
};
} // namespace

static auto clamp(float x, float min, float max) -> float
{
    return x < min
               ? min
           : x > max
               ? max
               : x;
}

static auto saturate(Vec3 const& v) -> Vec3
{
    return {
        clamp(v.x, 0.f, 1.f),
        clamp(v.y, 0.f, 1.f),
        clamp(v.z, 0.f, 1.f),
    };
}

// Start of [Block1]
// From https://entropymine.com/imageworsener/srgbformula/

static auto LinearRGB_from_sRGB_impl(float x) -> float
{
    return x < 0.04045f
               ? x / 12.92f
               : std::pow((x + 0.055f) / 1.055f, 2.4f);
}

static auto sRGB_from_LinearRGB_impl(float x) -> float
{
    return x < 0.0031308f
               ? x * 12.92f
               : 1.055f * std::pow(x, 1.f / 2.4f) - 0.055f;
}

static auto LinearRGB_from_sRGB(Vec3 srgb) -> Vec3
{
    srgb = saturate(srgb);
    return {
        LinearRGB_from_sRGB_impl(srgb.x),
        LinearRGB_from_sRGB_impl(srgb.y),
        LinearRGB_from_sRGB_impl(srgb.z),
    };
}

static auto sRGB_from_LinearRGB(Vec3 rgb) -> Vec3
{
    rgb = saturate(rgb);
    return {
        sRGB_from_LinearRGB_impl(rgb.x),
        sRGB_from_LinearRGB_impl(rgb.y),
        sRGB_from_LinearRGB_impl(rgb.z),
    };
}
// End of [Block1]

// Start of [Block2]// From https://bottosson.github.io/posts/oklab/

static auto Oklab_from_LinearRGB(Vec3 c) -> Vec3
{
    float l = 0.4122214708f * c.x + 0.5363325363f * c.y + 0.0514459929f * c.z;
    float m = 0.2119034982f * c.x + 0.6806995451f * c.y + 0.1073969566f * c.z;
    float s = 0.0883024619f * c.x + 0.2817188376f * c.y + 0.6299787005f * c.z;

    float l_ = cbrtf(l);
    float m_ = cbrtf(m);
    float s_ = cbrtf(s);

    return {
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_,
    };
}

static auto LinearRGB_from_Oklab(Vec3 c) -> Vec3
{
    float l_ = c.x + 0.3963377774f * c.y + 0.2158037573f * c.z;
    float m_ = c.x - 0.1055613458f * c.y - 0.0638541728f * c.z;
    float s_ = c.x - 0.0894841775f * c.y - 1.2914855480f * c.z;

    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;

    return {
        +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
        -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
        -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s,
    };
}

// End of [Block2]

auto Oklab_Premultiplied_from_sRGB_Straight(ColorRGBA const& col) -> ImVec4
{
    auto const lab = Oklab_from_LinearRGB(LinearRGB_from_sRGB({col.x, col.y, col.z}));
    return {
        lab.x * col.w,
        lab.y * col.w,
        lab.z * col.w,
        col.w,
    };
}

auto sRGB_Straight_from_Oklab_Premultiplied(ImVec4 const& col) -> ColorRGBA
{
    auto const srgb = sRGB_from_LinearRGB(LinearRGB_from_Oklab({col.x / col.w, col.y / col.w, col.z / col.w}));
    return {
        srgb.x,
        srgb.y,
        srgb.z,
        col.w,
    };
}

}} // namespace ImGG::internal