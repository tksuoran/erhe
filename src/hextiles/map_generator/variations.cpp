#include "map_generator/variations.hpp"

#include <gsl/assert>

#include <algorithm>
#include <limits>

namespace hextiles
{

void Variations::reset(size_t count)
{
    m_values.reserve(count);
    m_values.clear();
    m_min_value = std::numeric_limits<float>::max();
    m_max_value = std::numeric_limits<float>::lowest();
}

void Variations::push(float value)
{
    m_values.push_back(value);
    m_min_value = std::min(m_min_value, value);
    m_max_value = std::max(m_max_value, value);
}

auto Variations::get_noise_value(size_t index) const -> float
{
    Expects(index < m_values.size());
    return m_values[index];
}

auto Variations::normalize()
{
    float extent = m_max_value - m_min_value;

    // normalize values to 0..1
    for (auto& value : m_values)
    {
        value = (value - m_min_value) / extent;
    }

    float sum = 0.0f;
    for (const auto& entry : m_terrains)
    {
        sum += entry.ratio;
    }
    if (sum > 0.0f)
    {
        for (auto& entry : m_terrains)
        {
            entry.normalized_ratio = entry.ratio / sum;
        }
    }
    else
    {
        for (auto& entry : m_terrains)
        {
            entry.normalized_ratio = entry.ratio;
        }
    }
}

void Variations::compute_threshold_values()
{
    // Once all noise values have been generated, we
    // find threshold values.

    normalize();

    // Copy and sort
    std::vector<float> sorted_values = m_values;
    std::sort(
        sorted_values.begin(),
        sorted_values.end()
    );

    // Locate threshold values
    float offset{0.0f};
    size_t count = m_values.size();
    for (auto& terrain : m_terrains)
    {
        const float  normalized_ratio = std::min(1.0f, offset + terrain.normalized_ratio);
        const size_t terrain_index    = static_cast<size_t>(static_cast<float>(count - 1) * normalized_ratio);
        terrain.threshold = sorted_values[terrain_index];
        offset += terrain.normalized_ratio;
    }
}

auto Variations::get(size_t index) const -> Terrain_variation
{
    const float noise_value = get_noise_value(index);
    for (const auto& terrain : m_terrains)
    {
        if (noise_value < terrain.threshold)
        {
            return terrain;
        }
    }
    return *m_terrains.rbegin();
}

auto Variations::make(
    int       id,
    terrain_t base_terrain,
    terrain_t variation_terrain
) const -> Terrain_variation
{
    return make(id, base_terrain, variation_terrain, 0.0f);
}

auto Variations::make(
    int       id,
    terrain_t base_terrain,
    float     ratio
) const -> Terrain_variation
{
    return make(id, base_terrain, terrain_t{0}, ratio);
}

auto Variations::make(
    int       id,
    terrain_t base_terrain,
    terrain_t variation_terrain,
    float     ratio
) const -> Terrain_variation
{
    const auto i = std::find_if(
        m_terrains.begin(),
        m_terrains.end(),
        [base_terrain, variation_terrain](const Terrain_variation& entry)
        {
            return
                (entry.base_terrain      == base_terrain) &&
                (entry.variation_terrain == variation_terrain);
        }
    );
    if (i != m_terrains.end())
    {
        Terrain_variation result = *i;
        result.id    = id;
        result.ratio = ratio;
        return result;
    }
    return Terrain_variation
    {
        .id                = id,
        .base_terrain      = base_terrain,
        .variation_terrain = variation_terrain,
        .ratio             = ratio,
        .normalized_ratio  = 1.0f,
        .threshold         = 0.5f
    };
}

void Variations::assign(std::vector<Terrain_variation>&& terrains)
{
    m_terrains = std::move(terrains);
    std::sort(
        m_terrains.begin(),
        m_terrains.end(),
        [](const Terrain_variation& lhs, const Terrain_variation& rhs)
        {
            return lhs.id < rhs.id;
        }
    );
}

} // namespace hextiles

