#include "unit_type.hpp"

namespace hextiles
{

auto Unit_flags::c_char(const uint32_t value) -> char
{
    switch (value) {
        case bit_has_parachute      : return '^';
        case bit_can_build_plain    : return 'p';
        case bit_can_build_field    : return 'f';
        case bit_can_build_road     : return 'r';
        case bit_can_build_bridge   : return 'b';
        case bit_can_build_fortress : return 'F';
        case bit_city_defense       : return 'd';
        case bit_city_load          : return 'l';
        case bit_airdrop_unload     : return 'a';
        case bit_has_stealth_mode   : return 's';
        case bit_count              : return ' ';
        default                     : return '?';
    }
}

auto Unit_flags::c_str(const uint32_t value) -> const char*
{
    switch (value) {
        case bit_has_parachute      : return "Has parachute";
        case bit_can_build_plain    : return "Can build Plain";
        case bit_can_build_field    : return "Can build Field";
        case bit_can_build_road     : return "Can build Road";
        case bit_can_build_bridge   : return "Can build Bridge";
        case bit_can_build_fortress : return "Can build Fortress";
        case bit_city_defense       : return "Participates in City Defense";
        case bit_city_load          : return "Can be loaded from city";
        case bit_airdrop_unload     : return "Can be Airdrop unloaded";
        case bit_has_stealth_mode   : return "Has stealth mode";
        case bit_count              : return "None";
        default                     : return "";
    }
}

auto Battle_type::c_char(const uint32_t value) -> char
{
    switch (value) {
        case bit_air        : return 'a';
        case bit_ground     : return 'g';
        case bit_sea        : return 's';
        case bit_underwater : return 'u';
        case bit_city       : return 'C';
        case bit_count      : return ' ';
        default             : return '?';
    }
}

auto Battle_type::c_str(const uint32_t value) -> const char*
{
    switch (value) {
        case bit_air        : return "Air";
        case bit_ground     : return "Ground";
        case bit_sea        : return "Sea";
        case bit_underwater : return "Underwater";
        case bit_city       : return "City";
        case bit_count      : return "None";
        default             : return "?";
    }
}

auto Movement_type::c_char(const uint32_t value) -> char
{
    switch (value) {
        case bit_air         : return 'a';
        case bit_advanced_air: return 'A';
        case bit_light_ground: return 'g';
        case bit_heavy_ground: return 'G';
        case bit_light_sea   : return 's';
        case bit_heavy_sea   : return 'S';
        case bit_underwater  : return 'u';
        case bit_count       : return ' ';
        default              : return '?';
    }
}

auto Movement_type::c_str(const uint32_t value) -> const char*
{
    switch (value) {
        case bit_air         : return "Air";
        case bit_advanced_air: return "Advanced Air";
        case bit_light_ground: return "Light Ground";
        case bit_heavy_ground: return "Heavy Ground";
        case bit_reserved    : return "(reserved)";
        case bit_light_sea   : return "Light Sea";
        case bit_heavy_sea   : return "Heavy Sea";
        case bit_underwater  : return "Underwater";
        case bit_count       : return "None";
        default              : return "?";
    }
}

} // namespace hextiles
