#pragma once

#include <cstdint>
#include <span>

#include <glm/glm.hpp>
#include <simdjson.h>

#include <erhe_codegen/field_info.hpp>

struct Transform_data
{
    static constexpr uint32_t current_version = 1;

    glm::vec3 translation{0.0f, 0.0f, 0.0f}; // v1+
    glm::vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f}; // v1+
    glm::vec3 scale{1.0f, 1.0f, 1.0f}; // v1+
};

auto serialize  (const Transform_data& value) -> std::string;
auto deserialize(simdjson::ondemand::object obj, Transform_data& out) -> simdjson::error_code;

auto get_struct_info(const Transform_data*) -> const erhe::codegen::Struct_info&;
auto get_fields     (const Transform_data*) -> std::span<const erhe::codegen::Field_info>;
