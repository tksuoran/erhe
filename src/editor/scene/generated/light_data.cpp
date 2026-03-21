#include "light_data.hpp"

#include <cstddef>

#include <erhe_codegen/serialize_helpers.hpp>
#include <erhe_codegen/migration.hpp>

auto serialize(const Light_data& value) -> std::string
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
    out += "\"type\":";
    erhe::codegen::serialize_string(out, to_string(value.type));
    out += ',';
    out += "\"color\":";
    erhe::codegen::serialize_vec3(out, value.color);
    out += ',';
    out += "\"intensity\":";
    erhe::codegen::serialize_float(out, value.intensity);
    out += ',';
    out += "\"range\":";
    erhe::codegen::serialize_float(out, value.range);
    out += ',';
    out += "\"inner_spot_angle\":";
    erhe::codegen::serialize_float(out, value.inner_spot_angle);
    out += ',';
    out += "\"outer_spot_angle\":";
    erhe::codegen::serialize_float(out, value.outer_spot_angle);
    out += ',';
    out += "\"cast_shadow\":";
    erhe::codegen::serialize_bool(out, value.cast_shadow);
    out += ',';
    if (out.back() == ',') out.back() = '}'; else out += '}';
    return out;
}

auto deserialize(simdjson::ondemand::object obj, Light_data& out) -> simdjson::error_code
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

    // type: added_in=1
    if (!obj["type"].get(val)) {
        std::string_view str;
        if (!val.get_string().get(str)) {
            from_string(str, out.type);
        }
    }

    // color: added_in=1
    if (!obj["color"].get(val)) {
        erhe::codegen::deserialize_field(val, out.color);
    }

    // intensity: added_in=1
    if (!obj["intensity"].get(val)) {
        erhe::codegen::deserialize_field(val, out.intensity);
    }

    // range: added_in=1
    if (!obj["range"].get(val)) {
        erhe::codegen::deserialize_field(val, out.range);
    }

    // inner_spot_angle: added_in=1
    if (!obj["inner_spot_angle"].get(val)) {
        erhe::codegen::deserialize_field(val, out.inner_spot_angle);
    }

    // outer_spot_angle: added_in=1
    if (!obj["outer_spot_angle"].get(val)) {
        erhe::codegen::deserialize_field(val, out.outer_spot_angle);
    }

    // cast_shadow: added_in=1
    if (!obj["cast_shadow"].get(val)) {
        erhe::codegen::deserialize_field(val, out.cast_shadow);
    }

    if (version != Light_data::current_version) {
        erhe::codegen::run_migrations(
            out,
            static_cast<uint32_t>(version),
            Light_data::current_version
        );
    }

    return simdjson::SUCCESS;
}

extern const erhe::codegen::Enum_info light_type_serial_enum_info;

static const erhe::codegen::Field_info light_data_fields[] = {
    {
        .name          = "node_id",
        .type_name     = "uint64_t",
        .offset        = offsetof(Light_data, node_id),
        .size          = sizeof(uint64_t),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Node this light is attached to",
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
        .offset        = offsetof(Light_data, name),
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
        .name          = "type",
        .type_name     = "Light_type_serial",
        .offset        = offsetof(Light_data, type),
        .size          = sizeof(Light_type_serial),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "Light_type_serial::directional",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = true,
        .enum_info     = &light_type_serial_enum_info,
    },
    {
        .name          = "color",
        .type_name     = "glm::vec3",
        .offset        = offsetof(Light_data, color),
        .size          = sizeof(glm::vec3),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "1.0f, 1.0f, 1.0f",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "intensity",
        .type_name     = "float",
        .offset        = offsetof(Light_data, intensity),
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
        .name          = "range",
        .type_name     = "float",
        .offset        = offsetof(Light_data, range),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "100.0f",
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
        .name          = "inner_spot_angle",
        .type_name     = "float",
        .offset        = offsetof(Light_data, inner_spot_angle),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0.0f",
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
        .name          = "outer_spot_angle",
        .type_name     = "float",
        .offset        = offsetof(Light_data, outer_spot_angle),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0.0f",
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
        .name          = "cast_shadow",
        .type_name     = "bool",
        .offset        = offsetof(Light_data, cast_shadow),
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
};

static const erhe::codegen::Struct_info light_data_struct_info = {
    .name    = "Light_data",
    .version = 1,
    .fields  = light_data_fields,
};

auto get_struct_info(const Light_data*) -> const erhe::codegen::Struct_info&
{
    return light_data_struct_info;
}

auto get_fields(const Light_data*) -> std::span<const erhe::codegen::Field_info>
{
    return light_data_fields;
}
