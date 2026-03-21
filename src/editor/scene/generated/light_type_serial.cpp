#include "light_type_serial.hpp"

auto to_string(Light_type_serial value) -> std::string_view
{
    switch (value) {
        case Light_type_serial::directional: return "directional";
        case Light_type_serial::point: return "point";
        case Light_type_serial::spot: return "spot";
    }
    return "directional"; // fallback to first value
}

auto from_string(std::string_view str, Light_type_serial& out) -> bool
{
    if (str == "directional") { out = Light_type_serial::directional; return true; }
    if (str == "point") { out = Light_type_serial::point; return true; }
    if (str == "spot") { out = Light_type_serial::spot; return true; }
    return false; // unknown string, out unchanged
}

static constexpr erhe::codegen::Enum_value_info light_type_serial_values[] = {
    { .name = "directional", .value = 0, .short_desc = nullptr, .long_desc = nullptr },
    { .name = "point", .value = 1, .short_desc = nullptr, .long_desc = nullptr },
    { .name = "spot", .value = 2, .short_desc = nullptr, .long_desc = nullptr },
};

extern const erhe::codegen::Enum_info light_type_serial_enum_info = {
    .name                 = "Light_type_serial",
    .underlying_type_name = "int",
    .values               = light_type_serial_values,
};

auto get_enum_info(const Light_type_serial*) -> const erhe::codegen::Enum_info&
{
    return light_type_serial_enum_info;
}
