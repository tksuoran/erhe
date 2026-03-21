#include "node_physics_data.hpp"

#include <cstddef>

#include <erhe_codegen/serialize_helpers.hpp>
#include <erhe_codegen/migration.hpp>

auto serialize(const Node_physics_data& value) -> std::string
{
    std::string out;
    out += '{';
    out += "\"_version\":1,";
    out += "\"node_id\":";
    erhe::codegen::serialize_uint(out, static_cast<uint64_t>(value.node_id));
    out += ',';
    out += "\"motion_mode\":";
    erhe::codegen::serialize_string(out, to_string(value.motion_mode));
    out += ',';
    out += "\"friction\":";
    erhe::codegen::serialize_float(out, value.friction);
    out += ',';
    out += "\"restitution\":";
    erhe::codegen::serialize_float(out, value.restitution);
    out += ',';
    out += "\"linear_damping\":";
    erhe::codegen::serialize_float(out, value.linear_damping);
    out += ',';
    out += "\"angular_damping\":";
    erhe::codegen::serialize_float(out, value.angular_damping);
    out += ',';
    out += "\"mass\":";
    if (value.mass.has_value()) {
        erhe::codegen::serialize_float(out, value.mass.value());
    } else {
        out += "null";
    }
    out += ',';
    out += "\"density\":";
    if (value.density.has_value()) {
        erhe::codegen::serialize_float(out, value.density.value());
    } else {
        out += "null";
    }
    out += ',';
    out += "\"enable_collisions\":";
    erhe::codegen::serialize_bool(out, value.enable_collisions);
    out += ',';
    if (out.back() == ',') out.back() = '}'; else out += '}';
    return out;
}

auto deserialize(simdjson::ondemand::object obj, Node_physics_data& out) -> simdjson::error_code
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

    // motion_mode: added_in=1
    if (!obj["motion_mode"].get(val)) {
        std::string_view str;
        if (!val.get_string().get(str)) {
            from_string(str, out.motion_mode);
        }
    }

    // friction: added_in=1
    if (!obj["friction"].get(val)) {
        erhe::codegen::deserialize_field(val, out.friction);
    }

    // restitution: added_in=1
    if (!obj["restitution"].get(val)) {
        erhe::codegen::deserialize_field(val, out.restitution);
    }

    // linear_damping: added_in=1
    if (!obj["linear_damping"].get(val)) {
        erhe::codegen::deserialize_field(val, out.linear_damping);
    }

    // angular_damping: added_in=1
    if (!obj["angular_damping"].get(val)) {
        erhe::codegen::deserialize_field(val, out.angular_damping);
    }

    // mass: added_in=1
    if (!obj["mass"].get(val)) {
        if (val.is_null()) {
            out.mass = std::nullopt;
        } else {
            float tmp{};
            erhe::codegen::deserialize_field(val, tmp);
            out.mass = std::move(tmp);
        }
    }

    // density: added_in=1
    if (!obj["density"].get(val)) {
        if (val.is_null()) {
            out.density = std::nullopt;
        } else {
            float tmp{};
            erhe::codegen::deserialize_field(val, tmp);
            out.density = std::move(tmp);
        }
    }

    // enable_collisions: added_in=1
    if (!obj["enable_collisions"].get(val)) {
        erhe::codegen::deserialize_field(val, out.enable_collisions);
    }

    if (version != Node_physics_data::current_version) {
        erhe::codegen::run_migrations(
            out,
            static_cast<uint32_t>(version),
            Node_physics_data::current_version
        );
    }

    return simdjson::SUCCESS;
}

extern const erhe::codegen::Enum_info motion_mode_serial_enum_info;

static const erhe::codegen::Field_info node_physics_data_fields[] = {
    {
        .name          = "node_id",
        .type_name     = "uint64_t",
        .offset        = offsetof(Node_physics_data, node_id),
        .size          = sizeof(uint64_t),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Node this physics body is attached to",
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
        .name          = "motion_mode",
        .type_name     = "Motion_mode_serial",
        .offset        = offsetof(Node_physics_data, motion_mode),
        .size          = sizeof(Motion_mode_serial),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "Motion_mode_serial::e_dynamic",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = true,
        .enum_info     = &motion_mode_serial_enum_info,
    },
    {
        .name          = "friction",
        .type_name     = "float",
        .offset        = offsetof(Node_physics_data, friction),
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
        .name          = "restitution",
        .type_name     = "float",
        .offset        = offsetof(Node_physics_data, restitution),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0.2f",
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
        .name          = "linear_damping",
        .type_name     = "float",
        .offset        = offsetof(Node_physics_data, linear_damping),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0.05f",
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
        .name          = "angular_damping",
        .type_name     = "float",
        .offset        = offsetof(Node_physics_data, angular_damping),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0.05f",
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
        .name          = "mass",
        .type_name     = "std::optional<float>",
        .offset        = offsetof(Node_physics_data, mass),
        .size          = sizeof(std::optional<float>),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Override mass; if absent, derived from density and volume",
        .long_desc     = nullptr,
        .default_value = nullptr,
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "density",
        .type_name     = "std::optional<float>",
        .offset        = offsetof(Node_physics_data, density),
        .size          = sizeof(std::optional<float>),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Density for mass calculation",
        .long_desc     = nullptr,
        .default_value = nullptr,
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "enable_collisions",
        .type_name     = "bool",
        .offset        = offsetof(Node_physics_data, enable_collisions),
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

static const erhe::codegen::Struct_info node_physics_data_struct_info = {
    .name    = "Node_physics_data",
    .version = 1,
    .fields  = node_physics_data_fields,
};

auto get_struct_info(const Node_physics_data*) -> const erhe::codegen::Struct_info&
{
    return node_physics_data_struct_info;
}

auto get_fields(const Node_physics_data*) -> std::span<const erhe::codegen::Field_info>
{
    return node_physics_data_fields;
}
