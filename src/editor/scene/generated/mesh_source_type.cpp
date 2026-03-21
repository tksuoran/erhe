#include "mesh_source_type.hpp"

auto to_string(Mesh_source_type value) -> std::string_view
{
    switch (value) {
        case Mesh_source_type::geometry: return "geometry";
        case Mesh_source_type::gltf: return "gltf";
    }
    return "geometry"; // fallback to first value
}

auto from_string(std::string_view str, Mesh_source_type& out) -> bool
{
    if (str == "geometry") { out = Mesh_source_type::geometry; return true; }
    if (str == "gltf") { out = Mesh_source_type::gltf; return true; }
    return false; // unknown string, out unchanged
}

static constexpr erhe::codegen::Enum_value_info mesh_source_type_values[] = {
    { .name = "geometry", .value = 0, .short_desc = "Normative source is Geometry (geogram)", .long_desc = nullptr },
    { .name = "gltf", .value = 1, .short_desc = "Normative source is glTF (triangle soup)", .long_desc = nullptr },
};

extern const erhe::codegen::Enum_info mesh_source_type_enum_info = {
    .name                 = "Mesh_source_type",
    .underlying_type_name = "int",
    .values               = mesh_source_type_values,
};

auto get_enum_info(const Mesh_source_type*) -> const erhe::codegen::Enum_info&
{
    return mesh_source_type_enum_info;
}
