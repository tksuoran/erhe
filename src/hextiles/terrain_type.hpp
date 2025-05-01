#pragma once

#include "types.hpp"

#include "etl/vector.h"

#include <cstdint>
#include <string>

namespace hextiles {

class Terrain_flags
{
public:
    static auto c_char(uint32_t value) -> char;
    static auto c_str (uint32_t value) -> const char*;
    static constexpr int bit_can_place_coastal_city = 0;
    static constexpr int bit_can_place_land_city    = 1;
    static constexpr int bit_can_place_fortress     = 2;
    static constexpr int bit_can_place_plain        = 3;
    static constexpr int bit_can_place_field        = 4;
    static constexpr int bit_can_place_road         = 5;
    static constexpr int bit_can_place_bridge       = 6;
    static constexpr int bit_connects_to_bridge     = 7;
    static constexpr int bit_count                  = 8;
};

struct Terrain_type
{
    std::string name;
    int         tile                    {0};
    uint32_t    move_type_allow_mask    {0};  // MAllow
    int         move_cost               {0};  // MCost
    int         threat                  {0};  // Threat
    terrain_t   damaged_version         {0};  // ADamage
    int         defence_bonus           {0};  // DBonus
    int         city_size               {0};  // CSize
    int         strength                {0};  // Strength
    uint32_t    move_type_level_mask    {0};  // VChg
    uint32_t    flags                   {0};  //
    int         generate_elevation      {0};
    int         generate_priority       {0};
    terrain_t   generate_base           {0};
    int         generate_min_temperature{0};
    int         generate_max_temperature{0};
    int         generate_min_humidity   {0};
    int         generate_max_humidity   {0};
    float       generate_ratio          {1.0f};
    int         group = -1;
};

constexpr terrain_t Terrain_field   = 39u; // For city gen
constexpr terrain_t Terrain_default = 31u;

// Bridges
constexpr terrain_t Terrain_bridges[] = {
    terrain_t{11},
    terrain_t{12},
    terrain_t{13},
    terrain_t{11},
    terrain_t{12},
    terrain_t{13}
};

struct Terrain_group
{
    bool      enabled          {true};
    int       shape_group      {0};
    terrain_t base_terrain_type{0};
    terrain_t link_first       [3];
    terrain_t link_last        [3];
    int       link_group       [3];
    int       promoted         {0};
    int       demoted          {0};
};

struct Terrain_replacement_rule
{
    static constexpr int max_secondary_count = 8;

    bool      enabled    {true};
    bool      equal      {false};
    terrain_t primary    {0u};
    etl::vector<terrain_t, max_secondary_count> secondary;
    terrain_t replacement{0u};
};

//constexpr std::array<Terrain_group, 7> terrain_groups { {
//    // b  <    >  g0  g1  <    >   p   d
//    { 20, 11, 22, -1, -1, -1, -1, -1, -1 }, // 0 Road
//    { 28, 28, 29, -1, -1, -1, -1, -1,  3 }, // 1 Peak
//    { 41, 41, 44, -1, -1, -1, -1, -1, -1 }, // 2 Forest
//    { 45, 45, 49,  1, -1, 28, 29,  1, -1 }, // 3 Mountain
//    {  6,  5, 18,  5,  6, -1, -1, -1, -1 }, // 4 Water Low - plain (coast)
//    {  4,  3,  4,  6, -1, -1, -1, -1, -1 }, // 5 Water Medium - low
//    {  2,  1,  2, -1, -1, -1, -1, -1, -1 }, // 6 Water Deep - medium
//} };

} // namespace hextiles

