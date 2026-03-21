#pragma once

#include <string_view>

#include <erhe_codegen/field_info.hpp>

enum class Motion_mode_serial : int
{
    e_static = 1,
    e_kinematic_non_physical = 2,
    e_kinematic_physical = 3,
    e_dynamic = 4,
};

// String conversion (for serialization)
auto to_string  (Motion_mode_serial value) -> std::string_view;
auto from_string(std::string_view str, Motion_mode_serial& out) -> bool;

// Reflection
auto get_enum_info(const Motion_mode_serial*) -> const erhe::codegen::Enum_info&;
