#include "terrain_type.hpp"

namespace hextiles
{

auto Terrain_flags::c_char(const uint32_t value) -> char
{
    switch (value) {
        case bit_can_place_coastal_city: return 'c';
        case bit_can_place_land_city   : return 'C';
        case bit_can_place_fortress    : return 'F';
        case bit_can_place_plain       : return 'p';
        case bit_can_place_field       : return 'f';
        case bit_can_place_road        : return 'r';
        case bit_can_place_bridge      : return 'B';
        case bit_connects_to_bridge    : return 'b';
        default                        : return '?';
    }
}

auto Terrain_flags::c_str(const uint32_t value) -> const char*
{
    switch (value) {
        case bit_can_place_coastal_city: return "Can place coastal city";
        case bit_can_place_land_city   : return "Can place land city";
        case bit_can_place_fortress    : return "Can place fortress";
        case bit_can_place_plain       : return "Can place plain";
        case bit_can_place_field       : return "Can place field";
        case bit_can_place_road        : return "Can place road";
        case bit_can_place_bridge      : return "Can place bridge";
        case bit_connects_to_bridge    : return "Connects to bridge";
        default                        : return "?";
    }
}

} // namespace hextiles
