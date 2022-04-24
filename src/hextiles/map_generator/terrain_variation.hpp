#pragma once

#include "terrain_type.hpp"

namespace hextiles
{

struct Terrain_variation
{
    int       id               {0};
    terrain_t base_terrain     {0};
    terrain_t variation_terrain{0};
    float     ratio            {1.0f};
    float     normalized_ratio {1.0f};
    float     threshold        {0.0f};
};

} // namespace hextiles
