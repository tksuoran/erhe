#pragma once

#include "hextiles/types.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace hextiles
{

struct Terrain_type
{
    //uint16_t color;
    //uint16_t move_type_allow_mask;
    //uint16_t move_cost;
    //uint16_t threat;
    //uint16_t damaged_version;
    //uint16_t defence_bonus;
    //uint16_t city_size;
    //uint16_t strength;
    int         group = -1;
    std::string name;
};

constexpr terrain_t Terrain_unknown        =  0u;
constexpr terrain_t Terrain_water_low      =  5u;
constexpr terrain_t Terrain_fortress       = 19u;
constexpr terrain_t Terrain_road           = 20u;
constexpr terrain_t Terrain_bridge         = 12u;
constexpr terrain_t Terrain_plain          = 31u;
constexpr terrain_t Terrain_field          = 39u;
constexpr terrain_t Terrain_medium_water   =  3u;
constexpr terrain_t Terrain_default        = Terrain_plain;

// Bridges
constexpr terrain_t Terrain_bridges[] = {
    terrain_t{11},
    terrain_t{12},
    terrain_t{13},
    terrain_t{11},
    terrain_t{12},
    terrain_t{13}
};

struct Multi_shape
{
    terrain_t base_terrain_type; // a
    int link_first;        // b
    int link_last;         // c
    int link_group;        // d
    int link_group2;       // e
    int link2_first;       // f
    int link2_last;        // g
    int promoted;
    int demoted;
};

constexpr std::array<Multi_shape, 7> multishapes { {
    { 20, 11, 22, -1, -1, -1, -1, -1, -1 }, // 0 Road
    { 28, 28, 29, -1, -1, -1, -1, -1,  3 }, // 1 Peak
    { 41, 41, 44, -1, -1, -1, -1, -1, -1 }, // 2 Forest
    { 45, 45, 49,  1, -1, 28, 29,  1, -1 }, // 3 Mountain
    {  5,  5, 18,  5,  6, -1, -1, -1, -1 }, // 4 Water Low
    {  3,  3,  4,  6, -1, -1, -1, -1, -1 }, // 5 Water Medium
    {  1,  1,  2, -1, -1, -1, -1, -1, -1 }, // 6 Water Deep
} };

} // namespace hextiles

