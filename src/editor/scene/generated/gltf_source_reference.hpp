#pragma once

#include <cstdint>
#include <span>
#include <string>

#include <simdjson.h>

#include <erhe_codegen/field_info.hpp>

struct Gltf_source_reference
{
    static constexpr uint32_t current_version = 1;

    std::string gltf_path{""}; // v1+
    std::string item_name{""}; // v1+
    int item_index{-1}; // v1+
    std::string item_type{""}; // v1+
};

auto serialize  (const Gltf_source_reference& value) -> std::string;
auto deserialize(simdjson::ondemand::object obj, Gltf_source_reference& out) -> simdjson::error_code;

auto get_struct_info(const Gltf_source_reference*) -> const erhe::codegen::Struct_info&;
auto get_fields     (const Gltf_source_reference*) -> std::span<const erhe::codegen::Field_info>;
