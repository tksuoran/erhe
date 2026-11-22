#include "tools/channel_brush.hpp"

#include <algorithm>
#include <cmath>

namespace editor {

void Channel_brush_stroke::begin(const std::span<const glm::vec4> values)
{
    before_values.clear();
    before_values.insert(before_values.end(), values.begin(), values.end());
    alpha_max.clear();
    alpha_max.resize(values.size(), 0.0f);
}

void Channel_brush_stroke::clear()
{
    before_values.clear();
    alpha_max.clear();
}

auto channel_brush_falloff(const float distance, const float radius) -> float
{
    if ((radius <= 0.0f) || (distance >= radius)) {
        return 0.0f;
    }
    const float p = 1.0f - (distance / radius);
    return (p * p) * (3.0f - (2.0f * p));
}

auto channel_brush_blend(const Channel_brush_settings& settings, const glm::vec4& before, const float alpha) -> glm::vec4
{
    const float a = std::clamp(alpha, 0.0f, 1.0f);
    glm::vec4 result = before;
    for (int channel = 0; channel < 4; ++channel) {
        if ((settings.channel_mask & (1u << channel)) == 0) {
            continue;
        }
        const float base   = before[channel];
        const float target = settings.target[channel];
        float value = base;
        switch (settings.blend) {
            case Channel_brush_blend::mix:      value = (target * a) + (base * (1.0f - a)); break;
            case Channel_brush_blend::add:      value = base + (target * a);                break;
            case Channel_brush_blend::subtract: value = base - (target * a);                break;
        }
        result[channel] = std::clamp(value, 0.0f, 1.0f);
    }
    return result;
}

void apply_dab(
    const Channel_brush_settings& settings,
    Channel_brush_stroke&         stroke,
    const glm::vec3&              center,
    const std::span<const glm::vec3> positions,
    const std::span<glm::vec4>    values,
    std::vector<std::uint32_t>&   out_changed
)
{
    out_changed.clear();
    const std::size_t count = std::min(
        {positions.size(), values.size(), stroke.before_values.size(), stroke.alpha_max.size()}
    );
    const float radius_squared = settings.radius * settings.radius;
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec3 delta            = positions[i] - center;
        const float     distance_squared = glm::dot(delta, delta);
        if (distance_squared >= radius_squared) {
            continue;
        }
        const float alpha = std::min(channel_brush_falloff(std::sqrt(distance_squared), settings.radius) * settings.strength, 1.0f);
        // Non-accumulating: only a stronger alpha than the stroke has reached blends, from the value before the stroke.
        if (alpha <= stroke.alpha_max[i]) {
            continue;
        }
        stroke.alpha_max[i] = alpha;
        const glm::vec4 value = channel_brush_blend(settings, stroke.before_values[i], alpha);
        if (value != values[i]) {
            values[i] = value;
            out_changed.push_back(static_cast<std::uint32_t>(i));
        }
    }
}

}
