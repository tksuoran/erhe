#pragma once

#include <string_view>

#include <erhe_codegen/field_info.hpp>

enum class Mesh_source_type : int
{
    geometry = 0,
    gltf = 1,
};

// String conversion (for serialization)
auto to_string  (Mesh_source_type value) -> std::string_view;
auto from_string(std::string_view str, Mesh_source_type& out) -> bool;

// Reflection
auto get_enum_info(const Mesh_source_type*) -> const erhe::codegen::Enum_info&;
