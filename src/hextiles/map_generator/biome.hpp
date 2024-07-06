#pragma once

#include "types.hpp"

namespace hextiles {

struct Biome
{
    terrain_t base_terrain;
    terrain_t variation;
    int       priority;
    float     min_temperature;
    float     max_temperature;
    float     min_humidity;
    float     max_humidity;
};

} // namespace hextiles
