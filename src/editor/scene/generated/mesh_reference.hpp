#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <simdjson.h>

#include <erhe_codegen/field_info.hpp>

#include "gltf_source_reference.hpp"
#include "mesh_source_type.hpp"

struct Mesh_reference
{
    static constexpr uint32_t current_version = 2;

    uint64_t node_id{0}; // v1+
    Gltf_source_reference gltf_source{}; // v1+
    std::vector<Gltf_source_reference> material_refs{}; // v1+
    Mesh_source_type source_type{Mesh_source_type::gltf}; // v2+
    std::string geometry_path{""}; // v2+
    std::string mesh_name{""}; // v2+
    int primitive_count{0}; // v2+
};

auto serialize  (const Mesh_reference& value) -> std::string;
auto deserialize(simdjson::ondemand::object obj, Mesh_reference& out) -> simdjson::error_code;

auto get_struct_info(const Mesh_reference*) -> const erhe::codegen::Struct_info&;
auto get_fields     (const Mesh_reference*) -> std::span<const erhe::codegen::Field_info>;
