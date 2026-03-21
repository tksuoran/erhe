#include "transform_data.hpp"

#include <cstddef>

#include <erhe_codegen/serialize_helpers.hpp>
#include <erhe_codegen/migration.hpp>

auto serialize(const Transform_data& value) -> std::string
{
    std::string out;
    out += '{';
    out += "\"_version\":1,";
    out += "\"translation\":";
    erhe::codegen::serialize_vec3(out, value.translation);
    out += ',';
    out += "\"rotation\":";
    erhe::codegen::serialize_vec4(out, value.rotation);
    out += ',';
    out += "\"scale\":";
    erhe::codegen::serialize_vec3(out, value.scale);
    out += ',';
    if (out.back() == ',') out.back() = '}'; else out += '}';
    return out;
}

auto deserialize(simdjson::ondemand::object obj, Transform_data& out) -> simdjson::error_code
{
    uint64_t version = 1;
    {
        simdjson::ondemand::value v;
        if (!obj["_version"].get(v)) {
            v.get_uint64().get(version);
        }
    }

    simdjson::ondemand::value val;

    // translation: added_in=1
    if (!obj["translation"].get(val)) {
        erhe::codegen::deserialize_field(val, out.translation);
    }

    // rotation: added_in=1
    if (!obj["rotation"].get(val)) {
        erhe::codegen::deserialize_field(val, out.rotation);
    }

    // scale: added_in=1
    if (!obj["scale"].get(val)) {
        erhe::codegen::deserialize_field(val, out.scale);
    }

    if (version != Transform_data::current_version) {
        erhe::codegen::run_migrations(
            out,
            static_cast<uint32_t>(version),
            Transform_data::current_version
        );
    }

    return simdjson::SUCCESS;
}

static const erhe::codegen::Field_info transform_data_fields[] = {
    {
        .name          = "translation",
        .type_name     = "glm::vec3",
        .offset        = offsetof(Transform_data, translation),
        .size          = sizeof(glm::vec3),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = nullptr,
        .long_desc     = nullptr,
        .default_value = "0.0f, 0.0f, 0.0f",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "rotation",
        .type_name     = "glm::vec4",
        .offset        = offsetof(Transform_data, rotation),
        .size          = sizeof(glm::vec4),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Quaternion xyzw",
        .long_desc     = nullptr,
        .default_value = "0.0f, 0.0f, 0.0f, 1.0f",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "scale",
        .type_name     = "glm::vec3",
        .offset        = offsetof(Transform_data, scale),
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
};

static const erhe::codegen::Struct_info transform_data_struct_info = {
    .name    = "Transform_data",
    .version = 1,
    .fields  = transform_data_fields,
};

auto get_struct_info(const Transform_data*) -> const erhe::codegen::Struct_info&
{
    return transform_data_struct_info;
}

auto get_fields(const Transform_data*) -> std::span<const erhe::codegen::Field_info>
{
    return transform_data_fields;
}
