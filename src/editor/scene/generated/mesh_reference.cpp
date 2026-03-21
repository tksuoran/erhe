#include "mesh_reference.hpp"

#include <cstddef>

#include <erhe_codegen/serialize_helpers.hpp>
#include <erhe_codegen/migration.hpp>

auto serialize(const Mesh_reference& value) -> std::string
{
    std::string out;
    out += '{';
    out += "\"_version\":2,";
    out += "\"node_id\":";
    erhe::codegen::serialize_uint(out, static_cast<uint64_t>(value.node_id));
    out += ',';
    out += "\"gltf_source\":";
    out += serialize(value.gltf_source);
    out += ',';
    out += "\"material_refs\":";
    out += '[';
    for (std::size_t i = 0; i < value.material_refs.size(); ++i) {
        if (i > 0) out += ',';
        out += serialize(value.material_refs[i]);
    }
    out += ']';
    out += ',';
    out += "\"source_type\":";
    erhe::codegen::serialize_string(out, to_string(value.source_type));
    out += ',';
    out += "\"geometry_path\":";
    erhe::codegen::serialize_string(out, value.geometry_path);
    out += ',';
    out += "\"mesh_name\":";
    erhe::codegen::serialize_string(out, value.mesh_name);
    out += ',';
    out += "\"primitive_count\":";
    erhe::codegen::serialize_int(out, static_cast<int64_t>(value.primitive_count));
    out += ',';
    if (out.back() == ',') out.back() = '}'; else out += '}';
    return out;
}

auto deserialize(simdjson::ondemand::object obj, Mesh_reference& out) -> simdjson::error_code
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

    // gltf_source: added_in=1
    if (!obj["gltf_source"].get(val)) {
        simdjson::ondemand::object nested_obj;
        if (!val.get_object().get(nested_obj)) {
            deserialize(nested_obj, out.gltf_source);
        }
    }

    // material_refs: added_in=1
    if (!obj["material_refs"].get(val)) {
        {
            simdjson::ondemand::array arr;
            if (!val.get_array().get(arr)) {
                out.material_refs.clear();
                for (auto element : arr) {
                    Gltf_source_reference item{};
                    simdjson::ondemand::value elem_val;
                    if (!element.get(elem_val)) {
                        simdjson::ondemand::object nested_obj;
                        if (!elem_val.get_object().get(nested_obj)) {
                            deserialize(nested_obj, item);
                        }
                    }
                    out.material_refs.push_back(std::move(item));
                }
            }
        }
    }

    // source_type: added_in=2
    if (version >= 2) {
        if (!obj["source_type"].get(val)) {
            std::string_view str;
            if (!val.get_string().get(str)) {
                from_string(str, out.source_type);
            }
        }
    }

    // geometry_path: added_in=2
    if (version >= 2) {
        if (!obj["geometry_path"].get(val)) {
            erhe::codegen::deserialize_field(val, out.geometry_path);
        }
    }

    // mesh_name: added_in=2
    if (version >= 2) {
        if (!obj["mesh_name"].get(val)) {
            erhe::codegen::deserialize_field(val, out.mesh_name);
        }
    }

    // primitive_count: added_in=2
    if (version >= 2) {
        if (!obj["primitive_count"].get(val)) {
            erhe::codegen::deserialize_field(val, out.primitive_count);
        }
    }

    if (version != Mesh_reference::current_version) {
        erhe::codegen::run_migrations(
            out,
            static_cast<uint32_t>(version),
            Mesh_reference::current_version
        );
    }

    return simdjson::SUCCESS;
}

extern const erhe::codegen::Enum_info mesh_source_type_enum_info;

static const erhe::codegen::Field_info mesh_reference_fields[] = {
    {
        .name          = "node_id",
        .type_name     = "uint64_t",
        .offset        = offsetof(Mesh_reference, node_id),
        .size          = sizeof(uint64_t),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Which node this mesh is attached to",
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
        .name          = "gltf_source",
        .type_name     = "Gltf_source_reference",
        .offset        = offsetof(Mesh_reference, gltf_source),
        .size          = sizeof(Gltf_source_reference),
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
        .name          = "material_refs",
        .type_name     = "std::vector<Gltf_source_reference>",
        .offset        = offsetof(Mesh_reference, material_refs),
        .size          = sizeof(std::vector<Gltf_source_reference>),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Per-primitive material references",
        .long_desc     = nullptr,
        .default_value = nullptr,
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "source_type",
        .type_name     = "Mesh_source_type",
        .offset        = offsetof(Mesh_reference, source_type),
        .size          = sizeof(Mesh_source_type),
        .added_in      = 2,
        .removed_in    = 0,
        .short_desc    = "Normative data source",
        .long_desc     = nullptr,
        .default_value = "Mesh_source_type::gltf",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = true,
        .enum_info     = &mesh_source_type_enum_info,
    },
    {
        .name          = "geometry_path",
        .type_name     = "std::string",
        .offset        = offsetof(Mesh_reference, geometry_path),
        .size          = sizeof(std::string),
        .added_in      = 2,
        .removed_in    = 0,
        .short_desc    = "Base path for geogram mesh files (relative)",
        .long_desc     = nullptr,
        .default_value = "\"\"",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "mesh_name",
        .type_name     = "std::string",
        .offset        = offsetof(Mesh_reference, mesh_name),
        .size          = sizeof(std::string),
        .added_in      = 2,
        .removed_in    = 0,
        .short_desc    = "Mesh name",
        .long_desc     = nullptr,
        .default_value = "\"\"",
        .numeric_limits = {},
        .is_numeric    = false,
        .is_enum       = false,
        .enum_info     = nullptr,
    },
    {
        .name          = "primitive_count",
        .type_name     = "int",
        .offset        = offsetof(Mesh_reference, primitive_count),
        .size          = sizeof(int),
        .added_in      = 2,
        .removed_in    = 0,
        .short_desc    = "Number of primitives in the mesh",
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

static const erhe::codegen::Struct_info mesh_reference_struct_info = {
    .name    = "Mesh_reference",
    .version = 2,
    .fields  = mesh_reference_fields,
};

auto get_struct_info(const Mesh_reference*) -> const erhe::codegen::Struct_info&
{
    return mesh_reference_struct_info;
}

auto get_fields(const Mesh_reference*) -> std::span<const erhe::codegen::Field_info>
{
    return mesh_reference_fields;
}
