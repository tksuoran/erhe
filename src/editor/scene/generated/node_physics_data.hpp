#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include <simdjson.h>

#include <erhe_codegen/field_info.hpp>

#include "motion_mode_serial.hpp"

struct Node_physics_data
{
    static constexpr uint32_t current_version = 1;

    uint64_t node_id{0}; // v1+
    Motion_mode_serial motion_mode{Motion_mode_serial::e_dynamic}; // v1+
    float friction{0.5f}; // v1+
    float restitution{0.2f}; // v1+
    float linear_damping{0.05f}; // v1+
    float angular_damping{0.05f}; // v1+
    std::optional<float> mass{}; // v1+
    std::optional<float> density{}; // v1+
    bool enable_collisions{true}; // v1+
};

auto serialize  (const Node_physics_data& value) -> std::string;
auto deserialize(simdjson::ondemand::object obj, Node_physics_data& out) -> simdjson::error_code;

auto get_struct_info(const Node_physics_data*) -> const erhe::codegen::Struct_info&;
auto get_fields     (const Node_physics_data*) -> std::span<const erhe::codegen::Field_info>;
