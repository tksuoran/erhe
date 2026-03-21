#include "projection_data.hpp"

#include <cstddef>

#include <erhe_codegen/serialize_helpers.hpp>
#include <erhe_codegen/migration.hpp>

auto serialize(const Projection_data& value) -> std::string
{
    std::string out;
    out += '{';
    out += "\"_version\":1,";
    out += "\"projection_type\":";
    erhe::codegen::serialize_int(out, static_cast<int64_t>(value.projection_type));
    out += ',';
    out += "\"z_near\":";
    erhe::codegen::serialize_float(out, value.z_near);
    out += ',';
    out += "\"z_far\":";
    erhe::codegen::serialize_float(out, value.z_far);
    out += ',';
    out += "\"fov_x\":";
    erhe::codegen::serialize_float(out, value.fov_x);
    out += ',';
    out += "\"fov_y\":";
    erhe::codegen::serialize_float(out, value.fov_y);
    out += ',';
    out += "\"fov_left\":";
    erhe::codegen::serialize_float(out, value.fov_left);
    out += ',';
    out += "\"fov_right\":";
    erhe::codegen::serialize_float(out, value.fov_right);
    out += ',';
    out += "\"fov_up\":";
    erhe::codegen::serialize_float(out, value.fov_up);
    out += ',';
    out += "\"fov_down\":";
    erhe::codegen::serialize_float(out, value.fov_down);
    out += ',';
    out += "\"ortho_left\":";
    erhe::codegen::serialize_float(out, value.ortho_left);
    out += ',';
    out += "\"ortho_width\":";
    erhe::codegen::serialize_float(out, value.ortho_width);
    out += ',';
    out += "\"ortho_bottom\":";
    erhe::codegen::serialize_float(out, value.ortho_bottom);
    out += ',';
    out += "\"ortho_height\":";
    erhe::codegen::serialize_float(out, value.ortho_height);
    out += ',';
    out += "\"frustum_left\":";
    erhe::codegen::serialize_float(out, value.frustum_left);
    out += ',';
    out += "\"frustum_right\":";
    erhe::codegen::serialize_float(out, value.frustum_right);
    out += ',';
    out += "\"frustum_bottom\":";
    erhe::codegen::serialize_float(out, value.frustum_bottom);
    out += ',';
    out += "\"frustum_top\":";
    erhe::codegen::serialize_float(out, value.frustum_top);
    out += ',';
    if (out.back() == ',') out.back() = '}'; else out += '}';
    return out;
}

auto deserialize(simdjson::ondemand::object obj, Projection_data& out) -> simdjson::error_code
{
    uint64_t version = 1;
    {
        simdjson::ondemand::value v;
        if (!obj["_version"].get(v)) {
            v.get_uint64().get(version);
        }
    }

    simdjson::ondemand::value val;

    // projection_type: added_in=1
    if (!obj["projection_type"].get(val)) {
        erhe::codegen::deserialize_field(val, out.projection_type);
    }

    // z_near: added_in=1
    if (!obj["z_near"].get(val)) {
        erhe::codegen::deserialize_field(val, out.z_near);
    }

    // z_far: added_in=1
    if (!obj["z_far"].get(val)) {
        erhe::codegen::deserialize_field(val, out.z_far);
    }

    // fov_x: added_in=1
    if (!obj["fov_x"].get(val)) {
        erhe::codegen::deserialize_field(val, out.fov_x);
    }

    // fov_y: added_in=1
    if (!obj["fov_y"].get(val)) {
        erhe::codegen::deserialize_field(val, out.fov_y);
    }

    // fov_left: added_in=1
    if (!obj["fov_left"].get(val)) {
        erhe::codegen::deserialize_field(val, out.fov_left);
    }

    // fov_right: added_in=1
    if (!obj["fov_right"].get(val)) {
        erhe::codegen::deserialize_field(val, out.fov_right);
    }

    // fov_up: added_in=1
    if (!obj["fov_up"].get(val)) {
        erhe::codegen::deserialize_field(val, out.fov_up);
    }

    // fov_down: added_in=1
    if (!obj["fov_down"].get(val)) {
        erhe::codegen::deserialize_field(val, out.fov_down);
    }

    // ortho_left: added_in=1
    if (!obj["ortho_left"].get(val)) {
        erhe::codegen::deserialize_field(val, out.ortho_left);
    }

    // ortho_width: added_in=1
    if (!obj["ortho_width"].get(val)) {
        erhe::codegen::deserialize_field(val, out.ortho_width);
    }

    // ortho_bottom: added_in=1
    if (!obj["ortho_bottom"].get(val)) {
        erhe::codegen::deserialize_field(val, out.ortho_bottom);
    }

    // ortho_height: added_in=1
    if (!obj["ortho_height"].get(val)) {
        erhe::codegen::deserialize_field(val, out.ortho_height);
    }

    // frustum_left: added_in=1
    if (!obj["frustum_left"].get(val)) {
        erhe::codegen::deserialize_field(val, out.frustum_left);
    }

    // frustum_right: added_in=1
    if (!obj["frustum_right"].get(val)) {
        erhe::codegen::deserialize_field(val, out.frustum_right);
    }

    // frustum_bottom: added_in=1
    if (!obj["frustum_bottom"].get(val)) {
        erhe::codegen::deserialize_field(val, out.frustum_bottom);
    }

    // frustum_top: added_in=1
    if (!obj["frustum_top"].get(val)) {
        erhe::codegen::deserialize_field(val, out.frustum_top);
    }

    if (version != Projection_data::current_version) {
        erhe::codegen::run_migrations(
            out,
            static_cast<uint32_t>(version),
            Projection_data::current_version
        );
    }

    return simdjson::SUCCESS;
}

static const erhe::codegen::Field_info projection_data_fields[] = {
    {
        .name          = "projection_type",
        .type_name     = "int",
        .offset        = offsetof(Projection_data, projection_type),
        .size          = sizeof(int),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Projection::Type as int",
        .long_desc     = nullptr,
        .default_value = "4",
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
        .name          = "z_near",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, z_near),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0.03f",
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
        .name          = "z_far",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, z_far),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "64.0f",
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
        .name          = "fov_x",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, fov_x),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "pi/4",
        .long_desc     = nullptr,
        .default_value = "0.7853981633974483f",
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
        .name          = "fov_y",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, fov_y),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "pi/4",
        .long_desc     = nullptr,
        .default_value = "0.7853981633974483f",
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
        .name          = "fov_left",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, fov_left),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "-0.7853981633974483f",
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
        .name          = "fov_right",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, fov_right),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0.7853981633974483f",
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
        .name          = "fov_up",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, fov_up),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0.7853981633974483f",
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
        .name          = "fov_down",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, fov_down),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "-0.7853981633974483f",
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
        .name          = "ortho_left",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, ortho_left),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "-0.5f",
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
        .name          = "ortho_width",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, ortho_width),
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
        .name          = "ortho_bottom",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, ortho_bottom),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "-0.5f",
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
        .name          = "ortho_height",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, ortho_height),
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
        .name          = "frustum_left",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, frustum_left),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "-0.5f",
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
        .name          = "frustum_right",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, frustum_right),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0.5f",
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
        .name          = "frustum_bottom",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, frustum_bottom),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "-0.5f",
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
        .name          = "frustum_top",
        .type_name     = "float",
        .offset        = offsetof(Projection_data, frustum_top),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0.5f",
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

static const erhe::codegen::Struct_info projection_data_struct_info = {
    .name    = "Projection_data",
    .version = 1,
    .fields  = projection_data_fields,
};

auto get_struct_info(const Projection_data*) -> const erhe::codegen::Struct_info&
{
    return projection_data_struct_info;
}

auto get_fields(const Projection_data*) -> std::span<const erhe::codegen::Field_info>
{
    return projection_data_fields;
}
