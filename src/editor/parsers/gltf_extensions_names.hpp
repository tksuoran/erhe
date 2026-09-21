#pragma once

// Stable serialized names for enums carried by the editor-domain ERHE_*
// glTF extensions (doc/editor/gltf_scene_roundtrip.md phase 3). Names, never
// raw enum values: numeric values are not stable across erhe versions.
// Shared by parsers/gltf_extensions_export.cpp and
// parsers/gltf_extensions_import.cpp; the *_from_name() parsers fall back
// to the default-constructed value for unknown names.

#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/enums.hpp"

#include <string_view>

namespace editor {

[[nodiscard]] inline auto motion_mode_name(const erhe::physics::Motion_mode mode) -> const char*
{
    switch (mode) {
        case erhe::physics::Motion_mode::e_static:                 return "static";
        case erhe::physics::Motion_mode::e_kinematic_non_physical: return "kinematic_non_physical";
        case erhe::physics::Motion_mode::e_kinematic_physical:     return "kinematic_physical";
        case erhe::physics::Motion_mode::e_dynamic:                return "dynamic";
        default:                                                   return "dynamic";
    }
}

[[nodiscard]] inline auto motion_mode_from_name(const std::string_view name) -> erhe::physics::Motion_mode
{
    if (name == "static")                 return erhe::physics::Motion_mode::e_static;
    if (name == "kinematic_non_physical") return erhe::physics::Motion_mode::e_kinematic_non_physical;
    if (name == "kinematic_physical")     return erhe::physics::Motion_mode::e_kinematic_physical;
    if (name == "dynamic")                return erhe::physics::Motion_mode::e_dynamic;
    return erhe::physics::Motion_mode::e_dynamic;
}

[[nodiscard]] inline auto normal_style_name(const erhe::primitive::Normal_style style) -> const char*
{
    switch (style) {
        case erhe::primitive::Normal_style::none:            return "none";
        case erhe::primitive::Normal_style::corner_normals:  return "corner_normals";
        case erhe::primitive::Normal_style::polygon_normals: return "polygon_normals";
        case erhe::primitive::Normal_style::point_normals:   return "point_normals";
        default:                                             return "corner_normals";
    }
}

[[nodiscard]] inline auto normal_style_from_name(const std::string_view name) -> erhe::primitive::Normal_style
{
    if (name == "none")            return erhe::primitive::Normal_style::none;
    if (name == "corner_normals")  return erhe::primitive::Normal_style::corner_normals;
    if (name == "polygon_normals") return erhe::primitive::Normal_style::polygon_normals;
    if (name == "point_normals")   return erhe::primitive::Normal_style::point_normals;
    return erhe::primitive::Normal_style::corner_normals;
}

}
