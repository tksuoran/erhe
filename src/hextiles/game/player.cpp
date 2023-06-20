
namespace hextiles
{

[[nodiscard]] auto clamp(float x, const float lowerlimit, const float upperlimit) -> float
{
    if (x < lowerlimit) {
        x = lowerlimit;
    }
    if (x > upperlimit) {
        x = upperlimit;
    }
    return x;
}

[[nodiscard]] auto smoothstep(const float edge0, const float edge1, float x) -> float
{
    if (x == 0.0f) {
        return 0.0f;
    }
    if (x == 1.0f) {
        return 1.0f;
    }

    // Scale, and clamp x to 0..1 range
    x = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);

    // Evaluate polynomial
    return x * x * (3.0f - 2.0f * x);
}

[[nodiscard]] auto smootherstep(const float edge0, const float edge1, float x) -> float
{
    if (x == 0.0f) {
        return 0.0f;
    }
    if (x == 1.0f) {
        return 1.0f;
    }

    // Scale, and clamp x to 0..1 range
    x = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);

    // Evaluate polynomial
    return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}


} // namespace hextiles
