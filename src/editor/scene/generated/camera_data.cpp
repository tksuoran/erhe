#include "camera_data.hpp"

#include <cstddef>

#include <erhe_codegen/serialize_helpers.hpp>
#include <erhe_codegen/migration.hpp>

auto serialize(const Camera_data& value) -> std::string
{
    std::string out;
    out += '{';
    out += "\"_version\":1,";
    out += "\"node_id\":";
    erhe::codegen::serialize_uint(out, static_cast<uint64_t>(value.node_id));
    out += ',';
    out += "\"name\":";
    erhe::codegen::serialize_string(out, value.name);
    out += ',';
    out += "\"projection\":";
    out += serialize(value.projection);
    out += ',';
    out += "\"exposure\":";
    erhe::codegen::serialize_float(out, value.exposure);
    out += ',';
    out += "\"shadow_range\":";
    erhe::codegen::serialize_float(out, value.shadow_range);
    out += ',';
    if (out.back() == ',') out.back() = '}'; else out += '}';
    return out;
}

auto deserialize(simdjson::ondemand::object obj, Camera_data& out) -> simdjson::error_code
{
    uint64_t version = 1;
    {
        simdjson::ondemand::value v;
        if (!obj["_version"].get(v)) {
            v.get_uint64().get(version);
        }
    }

    simdjson::ondemand::value val;

    // node_id: added_in=1
    if (!obj["node_id"].get(val)) {
        erhe::codegen::deserialize_field(val, out.node_id);
    }

    // name: added_in=1
    if (!obj["name"].get(val)) {
        erhe::codegen::deserialize_field(val, out.name);
    }

    // projection: added_in=1
    if (!obj["projection"].get(val)) {
        simdjson::ondemand::object nested_obj;
        if (!val.get_object().get(nested_obj)) {
            deserialize(nested_obj, out.projection);
        }
    }

    // exposure: added_in=1
    if (!obj["exposure"].get(val)) {
        erhe::codegen::deserialize_field(val, out.exposure);
    }

    // shadow_range: added_in=1
    if (!obj["shadow_range"].get(val)) {
        erhe::codegen::deserialize_field(val, out.shadow_range);
    }

    if (version != Camera_data::current_version) {
        erhe::codegen::run_migrations(
            out,
            static_cast<uint32_t>(version),
            Camera_data::current_version
        );
    }

    return simdjson::SUCCESS;
}

static const erhe::codegen::Field_info camera_data_fields[] = {
    {
        .name          = "node_id",
        .type_name     = "uint64_t",
        .offset        = offsetof(Camera_data, node_id),
        .size          = sizeof(uint64_t),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Node this camera is attached to",
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
        .name          = "name",
        .type_name     = "std::string",
        .offset        = offsetof(Camera_data, name),
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
        .name          = "projection",
        .type_name     = "Projection_data",
        .offset        = offsetof(Camera_data, projection),
        .size          = sizeof(Projection_data),
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
        .name          = "exposure",
        .type_name     = "float",
        .offset        = offsetof(Camera_data, exposure),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "1.0f",
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
        .name          = "shadow_range",
        .type_name     = "float",
        .offset        = offsetof(Camera_data, shadow_range),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "22.0f",
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

static const erhe::codegen::Struct_info camera_data_struct_info = {
    .name    = "Camera_data",
    .version = 1,
    .fields  = camera_data_fields,
};

auto get_struct_info(const Camera_data*) -> const erhe::codegen::Struct_info&
{
    return camera_data_struct_info;
}

auto get_fields(const Camera_data*) -> std::span<const erhe::codegen::Field_info>
{
    return camera_data_fields;
}
