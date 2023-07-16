#pragma once

#include "map_generator/terrain_variation.hpp"
#include "types.hpp"

#include <vector>

namespace hextiles
{

class Variations
{
public:
    void reset                   (size_t count);
    void push                    (float value);
    auto get_noise_value         (size_t index) const -> float;
    auto normalize               ();
    void compute_threshold_values();
    auto get                     (size_t index) const -> Terrain_variation;
    auto make                    (int id, terrain_t base_terrain, float ratio) const -> Terrain_variation;
    auto make                    (int id, terrain_t base_terrain, terrain_t variation_terrain) const -> Terrain_variation;
    auto make(
        int       id,
        terrain_t base_terrain,
        terrain_t variation_terrain,
        float     ratio
    ) const -> Terrain_variation;
    void assign                  (std::vector<Terrain_variation>&& terrains);

    float                          m_min_value{0.0f};
    float                          m_max_value{0.0f};
    std::vector<float>             m_values;
    std::vector<Terrain_variation> m_terrains;
};

} // namespace hextiles
