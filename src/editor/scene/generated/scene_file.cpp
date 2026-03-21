#include "scene_file.hpp"

#include <cstddef>

#include <erhe_codegen/serialize_helpers.hpp>
#include <erhe_codegen/migration.hpp>

auto serialize(const Scene_file& value) -> std::string
{
    std::string out;
    out += '{';
    out += "\"_version\":1,";
    out += "\"name\":";
    erhe::codegen::serialize_string(out, value.name);
    out += ',';
    out += "\"enable_physics\":";
    erhe::codegen::serialize_bool(out, value.enable_physics);
    out += ',';
    out += "\"nodes\":";
    out += '[';
    for (std::size_t i = 0; i < value.nodes.size(); ++i) {
        if (i > 0) out += ',';
        out += serialize(value.nodes[i]);
    }
    out += ']';
    out += ',';
    out += "\"cameras\":";
    out += '[';
    for (std::size_t i = 0; i < value.cameras.size(); ++i) {
        if (i > 0) out += ',';
        out += serialize(value.cameras[i]);
    }
    out += ']';
    out += ',';
    out += "\"lights\":";
    out += '[';
    for (std::size_t i = 0; i < value.lights.size(); ++i) {
        if (i > 0) out += ',';
        out += serialize(value.lights[i]);
    }
    out += ']';
    out += ',';
    out += "\"mesh_references\":";
    out += '[';
    for (std::size_t i = 0; i < value.mesh_references.size(); ++i) {
        if (i > 0) out += ',';
        out += serialize(value.mesh_references[i]);
    }
    out += ']';
    out += ',';
    if (out.back() == ',') out.back() = '}'; else out += '}';
    return out;
}

auto deserialize(simdjson::ondemand::object obj, Scene_file& out) -> simdjson::error_code
{
    uint64_t version = 1;
    {
        simdjson::ondemand::value v;
        if (!obj["_version"].get(v)) {
            v.get_uint64().get(version);
        }
    }

    simdjson::ondemand::value val;

    // name: added_in=1
    if (!obj["name"].get(val)) {
        erhe::codegen::deserialize_field(val, out.name);
    }

    // enable_physics: added_in=1
    if (!obj["enable_physics"].get(val)) {
        erhe::codegen::deserialize_field(val, out.enable_physics);
    }

    // nodes: added_in=1
    if (!obj["nodes"].get(val)) {
        {
            simdjson::ondemand::array arr;
            if (!val.get_array().get(arr)) {
                out.nodes.clear();
                for (auto element : arr) {
                    Node_data_serial item{};
                    simdjson::ondemand::value elem_val;
                    if (!element.get(elem_val)) {
                        simdjson::ondemand::object nested_obj;
                        if (!elem_val.get_object().get(nested_obj)) {
                            deserialize(nested_obj, item);
                        }
                    }
                    out.nodes.push_back(std::move(item));
                }
            }
        }
    }

    // cameras: added_in=1
    if (!obj["cameras"].get(val)) {
        {
            simdjson::ondemand::array arr;
            if (!val.get_array().get(arr)) {
                out.cameras.clear();
                for (auto element : arr) {
                    Camera_data item{};
                    simdjson::ondemand::value elem_val;
                    if (!element.get(elem_val)) {
                        simdjson::ondemand::object nested_obj;
                        if (!elem_val.get_object().get(nested_obj)) {
                            deserialize(nested_obj, item);
                        }
                    }
                    out.cameras.push_back(std::move(item));
                }
            }
        }
    }

    // lights: added_in=1
    if (!obj["lights"].get(val)) {
        {
            simdjson::ondemand::array arr;
            if (!val.get_array().get(arr)) {
                out.lights.clear();
                for (auto element : arr) {
                    Light_data item{};
                    simdjson::ondemand::value elem_val;
                    if (!element.get(elem_val)) {
                        simdjson::ondemand::object nested_obj;
                        if (!elem_val.get_object().get(nested_obj)) {
                            deserialize(nested_obj, item);
                        }
                    }
                    out.lights.push_back(std::move(item));
                }
            }
        }
    }

    // mesh_references: added_in=1
    if (!obj["mesh_references"].get(val)) {
        {
            simdjson::ondemand::array arr;
            if (!val.get_array().get(arr)) {
                out.mesh_references.clear();
                for (auto element : arr) {
                    Mesh_reference item{};
                    simdjson::ondemand::value elem_val;
                    if (!element.get(elem_val)) {
                        simdjson::ondemand::object nested_obj;
                        if (!elem_val.get_object().get(nested_obj)) {
                            deserialize(nested_obj, item);
                        }
                    }
                    out.mesh_references.push_back(std::move(item));
                }
            }
        }
    }

    if (version != Scene_file::current_version) {
        erhe::codegen::run_migrations(
            out,
            static_cast<uint32_t>(version),
            Scene_file::current_version
        );
    }

    return simdjson::SUCCESS;
}

static const erhe::codegen::Field_info scene_file_fields[] = {
    {
        .name          = "name",
        .type_name     = "std::string",
        .offset        = offsetof(Scene_file, name),
        .size          = sizeof(std::string),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "\"\"",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "enable_physics",
        .type_name     = "bool",
        .offset        = offsetof(Scene_file, enable_physics),
        .size          = sizeof(bool),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "true",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "nodes",
        .type_name     = "std::vector<Node_data_serial>",
        .offset        = offsetof(Scene_file, nodes),
        .size          = sizeof(std::vector<Node_data_serial>),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = nullptr,
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "cameras",
        .type_name     = "std::vector<Camera_data>",
        .offset        = offsetof(Scene_file, cameras),
        .size          = sizeof(std::vector<Camera_data>),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = nullptr,
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "lights",
        .type_name     = "std::vector<Light_data>",
        .offset        = offsetof(Scene_file, lights),
        .size          = sizeof(std::vector<Light_data>),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = nullptr,
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "mesh_references",
        .type_name     = "std::vector<Mesh_reference>",
        .offset        = offsetof(Scene_file, mesh_references),
        .size          = sizeof(std::vector<Mesh_reference>),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = nullptr,
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
};

static const erhe::codegen::Struct_info scene_file_struct_info = {
    .name    = "Scene_file",
    .version = 1,
    .fields  = scene_file_fields,
};

auto get_struct_info(const Scene_file*) -> const erhe::codegen::Struct_info&
{
    return scene_file_struct_info;
}

auto get_fields(const Scene_file*) -> std::span<const erhe::codegen::Field_info>
{
    return scene_file_fields;
}
