#include "node_data_serial.hpp"

#include <cstddef>

#include <erhe_codegen/serialize_helpers.hpp>
#include <erhe_codegen/migration.hpp>

auto serialize(const Node_data_serial& value) -> std::string
{
    std::string out;
    out += '{';
    out += "\"_version\":1,";
    out += "\"name\":";
    erhe::codegen::serialize_string(out, value.name);
    out += ',';
    out += "\"id\":";
    erhe::codegen::serialize_uint(out, static_cast<uint64_t>(value.id));
    out += ',';
    out += "\"parent_id\":";
    erhe::codegen::serialize_uint(out, static_cast<uint64_t>(value.parent_id));
    out += ',';
    out += "\"transform\":";
    out += serialize(value.transform);
    out += ',';
    out += "\"flag_bits\":";
    erhe::codegen::serialize_uint(out, static_cast<uint64_t>(value.flag_bits));
    out += ',';
    if (out.back() == ',') out.back() = '}'; else out += '}';
    return out;
}

auto deserialize(simdjson::ondemand::object obj, Node_data_serial& out) -> simdjson::error_code
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

    // id: added_in=1
    if (!obj["id"].get(val)) {
        erhe::codegen::deserialize_field(val, out.id);
    }

    // parent_id: added_in=1
    if (!obj["parent_id"].get(val)) {
        erhe::codegen::deserialize_field(val, out.parent_id);
    }

    // transform: added_in=1
    if (!obj["transform"].get(val)) {
        simdjson::ondemand::object nested_obj;
        if (!val.get_object().get(nested_obj)) {
            deserialize(nested_obj, out.transform);
        }
    }

    // flag_bits: added_in=1
    if (!obj["flag_bits"].get(val)) {
        erhe::codegen::deserialize_field(val, out.flag_bits);
    }

    if (version != Node_data_serial::current_version) {
        erhe::codegen::run_migrations(
            out,
            static_cast<uint32_t>(version),
            Node_data_serial::current_version
        );
    }

    return simdjson::SUCCESS;
}

static const erhe::codegen::Field_info node_data_serial_fields[] = {
    {
        .name          = "name",
        .type_name     = "std::string",
        .offset        = offsetof(Node_data_serial, name),
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
        .name          = "id",
        .type_name     = "uint64_t",
        .offset        = offsetof(Node_data_serial, id),
        .size          = sizeof(uint64_t),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Unique node ID within scene file",
        .long_desc     = nullptr,
        .default_value = "0",
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
        .name          = "parent_id",
        .type_name     = "uint64_t",
        .offset        = offsetof(Node_data_serial, parent_id),
        .size          = sizeof(uint64_t),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Parent node ID, 0 = root",
        .long_desc     = nullptr,
        .default_value = "0",
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
        .name          = "transform",
        .type_name     = "Transform_data",
        .offset        = offsetof(Node_data_serial, transform),
        .size          = sizeof(Transform_data),
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
        .name          = "flag_bits",
        .type_name     = "uint64_t",
        .offset        = offsetof(Node_data_serial, flag_bits),
        .size          = sizeof(uint64_t),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0",
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
};

static const erhe::codegen::Struct_info node_data_serial_struct_info = {
    .name    = "Node_data_serial",
    .version = 1,
    .fields  = node_data_serial_fields,
};

auto get_struct_info(const Node_data_serial*) -> const erhe::codegen::Struct_info&
{
    return node_data_serial_struct_info;
}

auto get_fields(const Node_data_serial*) -> std::span<const erhe::codegen::Field_info>
{
    return node_data_serial_fields;
}
