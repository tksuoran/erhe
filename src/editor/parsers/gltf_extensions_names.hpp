#pragma once

// Stable serialized names for enums carried by the editor-domain ERHE_*
// glTF extensions (doc/gltf-scene-roundtrip-plan.md phase 3). Names, never
// raw enum values: numeric values are not stable across erhe versions.
// Shared by parsers/gltf_extensions_export.cpp and
// parsers/gltf_extensions_import.cpp; the *_from_name() parsers fall back
// to the default-constructed value for unknown names.

#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/layout_item.hpp"

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

[[nodiscard]] inline auto layout_type_name(const erhe::scene::Layout_type type) -> const char*
{
    switch (type) {
        case erhe::scene::Layout_type::stack: return "stack";
        case erhe::scene::Layout_type::grid:  return "grid";
        case erhe::scene::Layout_type::flow:  return "flow";
        default:                              return "stack";
    }
}

[[nodiscard]] inline auto layout_type_from_name(const std::string_view name) -> erhe::scene::Layout_type
{
    if (name == "stack") return erhe::scene::Layout_type::stack;
    if (name == "grid")  return erhe::scene::Layout_type::grid;
    if (name == "flow")  return erhe::scene::Layout_type::flow;
    return erhe::scene::Layout_type::stack;
}

[[nodiscard]] inline auto axis_direction_name(const erhe::scene::Axis_direction direction) -> const char*
{
    switch (direction) {
        case erhe::scene::Axis_direction::pos_x: return "pos_x";
        case erhe::scene::Axis_direction::neg_x: return "neg_x";
        case erhe::scene::Axis_direction::pos_y: return "pos_y";
        case erhe::scene::Axis_direction::neg_y: return "neg_y";
        case erhe::scene::Axis_direction::pos_z: return "pos_z";
        case erhe::scene::Axis_direction::neg_z: return "neg_z";
        default:                                 return "pos_x";
    }
}

[[nodiscard]] inline auto axis_direction_from_name(const std::string_view name) -> erhe::scene::Axis_direction
{
    if (name == "pos_x") return erhe::scene::Axis_direction::pos_x;
    if (name == "neg_x") return erhe::scene::Axis_direction::neg_x;
    if (name == "pos_y") return erhe::scene::Axis_direction::pos_y;
    if (name == "neg_y") return erhe::scene::Axis_direction::neg_y;
    if (name == "pos_z") return erhe::scene::Axis_direction::pos_z;
    if (name == "neg_z") return erhe::scene::Axis_direction::neg_z;
    return erhe::scene::Axis_direction::pos_x;
}

[[nodiscard]] inline auto layout_alignment_name(const erhe::scene::Layout_alignment alignment) -> const char*
{
    switch (alignment) {
        case erhe::scene::Layout_alignment::negative: return "negative";
        case erhe::scene::Layout_alignment::positive: return "positive";
        case erhe::scene::Layout_alignment::stretch:  return "stretch";
        default:                                      return "negative";
    }
}

[[nodiscard]] inline auto layout_alignment_from_name(const std::string_view name) -> erhe::scene::Layout_alignment
{
    if (name == "negative") return erhe::scene::Layout_alignment::negative;
    if (name == "positive") return erhe::scene::Layout_alignment::positive;
    if (name == "stretch")  return erhe::scene::Layout_alignment::stretch;
    return erhe::scene::Layout_alignment::negative;
}

}
