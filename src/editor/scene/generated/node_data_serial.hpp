#pragma once

#include <cstdint>
#include <span>
#include <string>

#include <simdjson.h>

#include <erhe_codegen/field_info.hpp>

#include "transform_data.hpp"

struct Node_data_serial
{
    static constexpr uint32_t current_version = 1;

    std::string name{""}; // v1+
    uint64_t id{0}; // v1+
    uint64_t parent_id{0}; // v1+
    Transform_data transform{}; // v1+
    uint64_t flag_bits{0}; // v1+
};

auto serialize  (const Node_data_serial& value) -> std::string;
auto deserialize(simdjson::ondemand::object obj, Node_data_serial& out) -> simdjson::error_code;

auto get_struct_info(const Node_data_serial*) -> const erhe::codegen::Struct_info&;
auto get_fields     (const Node_data_serial*) -> std::span<const erhe::codegen::Field_info>;
