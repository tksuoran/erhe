#include "renderers/probe_grid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace editor {

auto Probe_grid::get_probe_count() const -> int
{
    return counts.x * counts.y * counts.z;
}

auto Probe_grid::is_valid() const -> bool
{
    return (counts.x > 1) && (counts.y > 1) && (counts.z > 1);
}

auto Probe_grid::get_probe_index(const glm::ivec3& coords) const -> int
{
    return coords.x + (counts.x * (coords.y + (counts.y * coords.z)));
}

auto Probe_grid::get_probe_position(const glm::ivec3& coords) const -> glm::vec3
{
    return origin + (glm::vec3{coords} * spacing);
}

auto Probe_grid::operator==(const Probe_grid& other) const -> bool
{
    return (counts == other.counts) && (origin == other.origin) && (spacing == other.spacing);
}

auto fit_probe_grid(const erhe::math::Aabb& bounds, const float target_spacing, const int max_probes_in) -> Probe_grid
{
    Probe_grid grid{};
    if (!bounds.is_valid()) {
        return grid;
    }
    const glm::vec3 min    = bounds.min;
    const glm::vec3 extent = glm::max(bounds.max - bounds.min, glm::vec3{1.0e-3f});

    // Probe counts from the target spacing; at least 2 per axis so the
    // trilinear interpolation always has a cell to interpolate inside. If
    // the result exceeds the probe budget, grow the spacing and retry - the
    // budget is a hard memory bound, the spacing is a target.
    const int  max_probes = std::max(8, max_probes_in);
    float      spacing    = std::max(0.01f, target_spacing);
    glm::ivec3 counts{0};
    for (;;) {
        counts = glm::ivec3{
            std::max(2, static_cast<int>(std::ceil(extent.x / spacing)) + 1),
            std::max(2, static_cast<int>(std::ceil(extent.y / spacing)) + 1),
            std::max(2, static_cast<int>(std::ceil(extent.z / spacing)) + 1)
        };
        const int64_t probe_count =
            static_cast<int64_t>(counts.x) *
            static_cast<int64_t>(counts.y) *
            static_cast<int64_t>(counts.z);
        if (probe_count <= static_cast<int64_t>(max_probes)) {
            break;
        }
        // Cube root of the overshoot is the spacing factor that would land
        // exactly on the budget; the 1.05 keeps the loop from stalling on
        // rounding. Both counts are >= 2, so this terminates.
        const double overshoot = static_cast<double>(probe_count) / static_cast<double>(max_probes);
        spacing *= static_cast<float>(std::cbrt(overshoot)) * 1.05f;
        if ((counts.x == 2) && (counts.y == 2) && (counts.z == 2)) {
            break; // Cannot get any coarser.
        }
    }

    grid.counts  = counts;
    grid.origin  = min;
    grid.spacing = extent / glm::vec3{counts - glm::ivec3{1}};
    return grid;
}

auto Probe_volume_bounds::is_outside(const erhe::math::Aabb& content) const -> bool
{
    return m_bounds.is_valid() && (
        glm::any(glm::lessThan   (content.min, m_bounds.min)) ||
        glm::any(glm::greaterThan(content.max, m_bounds.max))
    );
}

auto Probe_volume_bounds::is_much_smaller(const erhe::math::Aabb& content) const -> bool
{
    return m_bounds.is_valid() && (content.volume() < (0.5f * m_bounds.volume()));
}

auto Probe_volume_bounds::content_changed(const erhe::math::Aabb& content) const -> bool
{
    return !m_bounds.is_valid() || is_outside(content) || is_much_smaller(content);
}

auto Probe_volume_bounds::get_fit_bounds(const erhe::math::Aabb& content, const Volume_refit_cause cause) const -> erhe::math::Aabb
{
    switch (cause) {
        case Volume_refit_cause::content: {
            if (is_outside(content) && !is_much_smaller(content)) {
                erhe::math::Aabb grown = content;
                grown.include(m_bounds);
                return grown;
            }
            return content;
        }
        case Volume_refit_cause::budget: {
            return m_bounds.is_valid() ? m_bounds : content;
        }
        case Volume_refit_cause::settings:
        case Volume_refit_cause::none:
        default: {
            return content;
        }
    }
}

auto Probe_volume_bounds::get() const -> const erhe::math::Aabb&
{
    return m_bounds;
}

void Probe_volume_bounds::set(const erhe::math::Aabb& bounds)
{
    m_bounds = bounds;
}

void Probe_volume_bounds::reset()
{
    m_bounds = erhe::math::Aabb{};
}

} // namespace editor
