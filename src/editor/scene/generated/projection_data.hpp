#pragma once

#include <cstdint>
#include <span>

#include <simdjson.h>

#include <erhe_codegen/field_info.hpp>

struct Projection_data
{
    static constexpr uint32_t current_version = 1;

    int projection_type{4}; // v1+
    float z_near{0.03f}; // v1+
    float z_far{64.0f}; // v1+
    float fov_x{0.7853981633974483f}; // v1+
    float fov_y{0.7853981633974483f}; // v1+
    float fov_left{-0.7853981633974483f}; // v1+
    float fov_right{0.7853981633974483f}; // v1+
    float fov_up{0.7853981633974483f}; // v1+
    float fov_down{-0.7853981633974483f}; // v1+
    float ortho_left{-0.5f}; // v1+
    float ortho_width{1.0f}; // v1+
    float ortho_bottom{-0.5f}; // v1+
    float ortho_height{1.0f}; // v1+
    float frustum_left{-0.5f}; // v1+
    float frustum_right{0.5f}; // v1+
    float frustum_bottom{-0.5f}; // v1+
    float frustum_top{0.5f}; // v1+
};

auto serialize  (const Projection_data& value) -> std::string;
auto deserialize(simdjson::ondemand::object obj, Projection_data& out) -> simdjson::error_code;

auto get_struct_info(const Projection_data*) -> const erhe::codegen::Struct_info&;
auto get_fields     (const Projection_data*) -> std::span<const erhe::codegen::Field_info>;
