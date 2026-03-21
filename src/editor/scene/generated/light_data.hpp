#pragma once

#include <cstdint>
#include <span>
#include <string>

#include <glm/glm.hpp>
#include <simdjson.h>

#include <erhe_codegen/field_info.hpp>

#include "light_type_serial.hpp"

struct Light_data
{
    static constexpr uint32_t current_version = 1;

    uint64_t node_id{0}; // v1+
    std::string name{""}; // v1+
    Light_type_serial type{Light_type_serial::directional}; // v1+
    glm::vec3 color{1.0f, 1.0f, 1.0f}; // v1+
    float intensity{1.0f}; // v1+
    float range{100.0f}; // v1+
    float inner_spot_angle{0.0f}; // v1+
    float outer_spot_angle{0.0f}; // v1+
    bool cast_shadow{true}; // v1+
};

auto serialize  (const Light_data& value) -> std::string;
auto deserialize(simdjson::ondemand::object obj, Light_data& out) -> simdjson::error_code;

auto get_struct_info(const Light_data*) -> const erhe::codegen::Struct_info&;
auto get_fields     (const Light_data*) -> std::span<const erhe::codegen::Field_info>;
