#pragma once

#include <string_view>

#include <erhe_codegen/field_info.hpp>

enum class Light_type_serial : int
{
    directional = 0,
    point = 1,
    spot = 2,
};

// String conversion (for serialization)
auto to_string  (Light_type_serial value) -> std::string_view;
auto from_string(std::string_view str, Light_type_serial& out) -> bool;

// Reflection
auto get_enum_info(const Light_type_serial*) -> const erhe::codegen::Enum_info&;
