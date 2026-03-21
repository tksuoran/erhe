#include "gltf_source_reference.hpp"

#include <cstddef>

#include <erhe_codegen/serialize_helpers.hpp>
#include <erhe_codegen/migration.hpp>

auto serialize(const Gltf_source_reference& value) -> std::string
{
    std::string out;
    out += '{';
    out += "\"_version\":1,";
    out += "\"gltf_path\":";
    erhe::codegen::serialize_string(out, value.gltf_path);
    out += ',';
    out += "\"item_name\":";
    erhe::codegen::serialize_string(out, value.item_name);
    out += ',';
    out += "\"item_index\":";
    erhe::codegen::serialize_int(out, static_cast<int64_t>(value.item_index));
    out += ',';
    out += "\"item_type\":";
    erhe::codegen::serialize_string(out, value.item_type);
    out += ',';
    if (out.back() == ',') out.back() = '}'; else out += '}';
    return out;
}

auto deserialize(simdjson::ondemand::object obj, Gltf_source_reference& out) -> simdjson::error_code
{
    uint64_t version = 1;
    {
        simdjson::ondemand::value v;
        if (!obj["_version"].get(v)) {
            v.get_uint64().get(version);
        }
    }

    simdjson::ondemand::value val;

    // gltf_path: added_in=1
    if (!obj["gltf_path"].get(val)) {
        erhe::codegen::deserialize_field(val, out.gltf_path);
    }

    // item_name: added_in=1
    if (!obj["item_name"].get(val)) {
        erhe::codegen::deserialize_field(val, out.item_name);
    }

    // item_index: added_in=1
    if (!obj["item_index"].get(val)) {
        erhe::codegen::deserialize_field(val, out.item_index);
    }

    // item_type: added_in=1
    if (!obj["item_type"].get(val)) {
        erhe::codegen::deserialize_field(val, out.item_type);
    }

    if (version != Gltf_source_reference::current_version) {
        erhe::codegen::run_migrations(
            out,
            static_cast<uint32_t>(version),
            Gltf_source_reference::current_version
        );
    }

    return simdjson::SUCCESS;
}

static const erhe::codegen::Field_info gltf_source_reference_fields[] = {
    {
        .name          = "gltf_path",
        .type_name     = "std::string",
        .offset        = offsetof(Gltf_source_reference, gltf_path),
        .size          = sizeof(std::string),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Relative path to glTF file",
        .long_desc     = nullptr,
        .default_value = "\"\"",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "item_name",
        .type_name     = "std::string",
        .offset        = offsetof(Gltf_source_reference, item_name),
        .size          = sizeof(std::string),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Name of the item in glTF",
        .long_desc     = nullptr,
        .default_value = "\"\"",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "item_index",
        .type_name     = "int",
        .offset        = offsetof(Gltf_source_reference, item_index),
        .size          = sizeof(int),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Index of the item in glTF",
        .long_desc     = nullptr,
        .default_value = "-1",
        .numeric_limits = {
            .ui_min       = 0.0,
            .ui_max       = 0.0,
            .hard_min     = 0.0,
            .hard_max     = 0.0,
            .has_ui_min   = false,
            .has_ui_max   = false,
            .has_hard_min = false,
            .has_hard_max = false,
        },
        .is_numeric    = true,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "item_type",
        .type_name     = "std::string",
        .offset        = offsetof(Gltf_source_reference, item_type),
        .size          = sizeof(std::string),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Type: mesh, material, animation, skin, texture",
        .long_desc     = nullptr,
        .default_value = "\"\"",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
};

static const erhe::codegen::Struct_info gltf_source_reference_struct_info = {
    .name    = "Gltf_source_reference",
    .version = 1,
    .fields  = gltf_source_reference_fields,
};

auto get_struct_info(const Gltf_source_reference*) -> const erhe::codegen::Struct_info&
{
    return gltf_source_reference_struct_info;
}

auto get_fields(const Gltf_source_reference*) -> std::span<const erhe::codegen::Field_info>
{
    return gltf_source_reference_fields;
}
