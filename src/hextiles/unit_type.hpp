#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>

namespace hextiles
{

constexpr int Category_air    = 1;
constexpr int Category_ground = 2;
constexpr int Category_sea    = 3;
constexpr int Category_sub    = 4;
constexpr int Category_city   = 5;

class Unit_flags
{
public:
    static auto c_char(uint32_t value) -> char;
    static auto c_str (uint32_t value) -> const char*;
    static constexpr int bit_has_parachute      =  0; // SA_HAS_PARACH
    static constexpr int bit_can_build_plain    =  1; // SA_DO_PLAIN
    static constexpr int bit_can_build_field    =  2; // SA_DO_FIELD
    static constexpr int bit_can_build_road     =  3; // SA_DO_ROAD
    static constexpr int bit_can_build_bridge   =  4; // SA_DO_BRIDGE
    static constexpr int bit_can_build_fortress =  5; // SA_DO_FORTRESS
    static constexpr int bit_city_defense       =  6; // SA_DO_NOT_CDEF
    static constexpr int bit_city_load          =  7; // SA_NCITY_LOAD
    static constexpr int bit_airdrop_unload     =  8; // SA_NPRCH_UNLOAD
    static constexpr int bit_has_stealth_mode   =  9;
    static constexpr int bit_count              = 10;
};

class Movement_type
{
public:
    static auto c_char(uint32_t value) -> char;
    static auto c_str (uint32_t value) -> const char*;
    static constexpr int bit_air          = 0; //   1 MT_AIR
    static constexpr int bit_advanced_air = 1; //   2 MT_AT_AIR
    static constexpr int bit_light_ground = 2; //   4 MT_LT_GND
    static constexpr int bit_heavy_ground = 3; //   8 MT_HV_GND
    static constexpr int bit_reserved     = 4; //  16
    static constexpr int bit_light_sea    = 5; //  32 MT_LT_SEA
    static constexpr int bit_heavy_sea    = 6; //  64 MT_HV_SEA
    static constexpr int bit_underwater   = 7; // 128 MT_SUB
    static constexpr int bit_count        = 8;
};

class Battle_type
{
public:
    static auto c_char(uint32_t value) -> char;
    static auto c_str (uint32_t value) -> const char*;
    static constexpr int bit_air        = 0; // 1 BT_AIR
    static constexpr int bit_ground     = 1; // 2 BT_GND
    static constexpr int bit_sea        = 2; // 3 BT_SEA
    static constexpr int bit_underwater = 3; // 4 BT_SUB
    static constexpr int bit_city       = 4; // 5 BT_CITY
    static constexpr int bit_count      = 5;
};

struct Unit_type
{
    std::string name;
    int         tech_level     {0};
    int         production_time{0};
    int         city_size      {0};
    uint32_t    flags          {0};
    int         hitpoints      {0};
    int         repair_per_turn{0};
    uint32_t    move_type_bits {0};
    int         moves[2] = {0, 0};
    int         terrain_defense{0};
    int         fuel           {0};
    int         refuel_per_turn{0};
    uint32_t    cargo_types    {0};
    int         cargo_space    {0};
    int         cargo_load_count_per_turn{0};
    uint32_t    battle_type    {0};
    int         attack [4] = { 0, 0, 0, 0};
    int         defense[4] = { 0, 0, 0, 0};
    int         city_light_ground_modifier;
    unit_t      stealth_version{0};
    int         stealth_attack {0};
    int         stealth_defense{0};
    int         vision_range      [2] = { 0, 0 };
    int         visible_from_range[2] = { 0, 0 };
    int         range             [2] = { 0, 0 };
    int         ranged_attack_modifier;
    int         ranged_attack_ammo;
    int         ranged_attack_count_per_turn;
    int         audio_loop     {0};
    int         audio_frequency{0};
};

} // namespace hextiles
