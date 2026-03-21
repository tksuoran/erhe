#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <simdjson.h>

#include <erhe_codegen/field_info.hpp>

#include "camera_data.hpp"
#include "light_data.hpp"
#include "mesh_reference.hpp"
#include "node_data_serial.hpp"
#include "node_physics_data.hpp"

struct Scene_file
{
    static constexpr uint32_t current_version = 2;

    std::string name{""}; // v1+
    bool enable_physics{true}; // v1+
    std::vector<Node_data_serial> nodes{}; // v1+
    std::vector<Camera_data> cameras{}; // v1+
    std::vector<Light_data> lights{}; // v1+
    std::vector<Mesh_reference> mesh_references{}; // v1+
    std::vector<Node_physics_data> node_physics{}; // v2+
};

auto serialize  (const Scene_file& value) -> std::string;
auto deserialize(simdjson::ondemand::object obj, Scene_file& out) -> simdjson::error_code;

auto get_struct_info(const Scene_file*) -> const erhe::codegen::Struct_info&;
auto get_fields     (const Scene_file*) -> std::span<const erhe::codegen::Field_info>;
