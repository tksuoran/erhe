#include "motion_mode_serial.hpp"

auto to_string(Motion_mode_serial value) -> std::string_view
{
    switch (value) {
        case Motion_mode_serial::e_static: return "e_static";
        case Motion_mode_serial::e_kinematic_non_physical: return "e_kinematic_non_physical";
        case Motion_mode_serial::e_kinematic_physical: return "e_kinematic_physical";
        case Motion_mode_serial::e_dynamic: return "e_dynamic";
    }
    return "e_static"; // fallback to first value
}

auto from_string(std::string_view str, Motion_mode_serial& out) -> bool
{
    if (str == "e_static") { out = Motion_mode_serial::e_static; return true; }
    if (str == "e_kinematic_non_physical") { out = Motion_mode_serial::e_kinematic_non_physical; return true; }
    if (str == "e_kinematic_physical") { out = Motion_mode_serial::e_kinematic_physical; return true; }
    if (str == "e_dynamic") { out = Motion_mode_serial::e_dynamic; return true; }
    return false; // unknown string, out unchanged
}

static constexpr erhe::codegen::Enum_value_info motion_mode_serial_values[] = {
    { .name = "e_static", .value = 1, .short_desc = "Immovable", .long_desc = nullptr },
    { .name = "e_kinematic_non_physical", .value = 2, .short_desc = "Movable from scene graph (instant), not from physics", .long_desc = nullptr },
    { .name = "e_kinematic_physical", .value = 3, .short_desc = "Movable from scene graph (creates kinetic energy)", .long_desc = nullptr },
    { .name = "e_dynamic", .value = 4, .short_desc = "Movable from physics simulation", .long_desc = nullptr },
};

extern const erhe::codegen::Enum_info motion_mode_serial_enum_info = {
    .name                 = "Motion_mode_serial",
    .underlying_type_name = "int",
    .values               = motion_mode_serial_values,
};

auto get_enum_info(const Motion_mode_serial*) -> const erhe::codegen::Enum_info&
{
    return motion_mode_serial_enum_info;
}
