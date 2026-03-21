#pragma once

#include <cstdint>
#include <span>
#include <string>

#include <simdjson.h>

#include <erhe_codegen/field_info.hpp>

#include "projection_data.hpp"

struct Camera_data
{
    static constexpr uint32_t current_version = 1;

    uint64_t node_id{0}; // v1+
    std::string name{""}; // v1+
    Projection_data projection{}; // v1+
    float exposure{1.0f}; // v1+
    float shadow_range{22.0f}; // v1+
};

auto serialize  (const Camera_data& value) -> std::string;
auto deserialize(simdjson::ondemand::object obj, Camera_data& out) -> simdjson::error_code;

auto get_struct_info(const Camera_data*) -> const erhe::codegen::Struct_info&;
auto get_fields     (const Camera_data*) -> std::span<const erhe::codegen::Field_info>;
