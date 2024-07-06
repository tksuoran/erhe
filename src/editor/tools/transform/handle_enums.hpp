#pragma once

#include <glm/glm.hpp>

namespace editor {

enum class Handle : unsigned int {
    e_handle_none         =  0,
    e_handle_translate_x  =  1,
    e_handle_translate_y  =  2,
    e_handle_translate_z  =  3,
    e_handle_translate_xy =  4,
    e_handle_translate_xz =  5,
    e_handle_translate_yz =  6,
    e_handle_rotate_x     =  7,
    e_handle_rotate_y     =  8,
    e_handle_rotate_z     =  9,
    e_handle_scale_x      = 10,
    e_handle_scale_y      = 11,
    e_handle_scale_z      = 12,
    e_handle_scale_xy     = 13,
    e_handle_scale_xz     = 14,
    e_handle_scale_yz     = 15,
    e_handle_scale_xyz    = 16
};

enum class Handle_tool : unsigned int {
    e_handle_tool_none      = 0,
    e_handle_tool_translate = 1,
    e_handle_tool_rotate    = 2,
    e_handle_tool_scale     = 3
};

enum class Handle_class : unsigned int {
    e_handle_class_none    = 0,
    e_handle_class_axis    = 1,
    e_handle_class_plane   = 2,
    e_handle_class_uniform = 3
};

enum class Handle_axis : unsigned int {
    e_handle_axis_none = 0,
    e_handle_axis_x = 1,
    e_handle_axis_y = 2,
    e_handle_axis_z = 3,
};

enum class Handle_plane : unsigned int {
    e_handle_plane_none = 0,
    e_handle_plane_xy = 1,
    e_handle_plane_xz = 2,
    e_handle_plane_yz = 3,
};

enum class Handle_type : unsigned int {
    e_handle_type_none            = 0,
    e_handle_type_translate_axis  = 1,
    e_handle_type_translate_plane = 2,
    e_handle_type_rotate          = 3,
    e_handle_type_scale_axis      = 4,
    e_handle_type_scale_plane     = 5,
    e_handle_type_scale_uniform   = 6,
};

class Axis_mask
{
public:
    static constexpr unsigned int x   = (1 << 0u);
    static constexpr unsigned int y   = (1 << 1u);
    static constexpr unsigned int z   = (1 << 2u);
    static constexpr unsigned int xy  = x | y;
    static constexpr unsigned int xz  = x | z;
    static constexpr unsigned int yz  = y | z;
    static constexpr unsigned int xyz = x | y | z;
};

[[nodiscard]] auto get_handle_class(Handle handle) -> Handle_class;
[[nodiscard]] auto get_handle_axis (Handle handle) -> Handle_axis;
[[nodiscard]] auto get_handle_plane(Handle handle) -> Handle_plane;
[[nodiscard]] auto get_handle_tool (Handle handle) -> Handle_tool;
[[nodiscard]] auto get_handle_type (Handle handle) -> Handle_type;
[[nodiscard]] auto get_axis_mask   (Handle handle) -> unsigned int;
[[nodiscard]] auto c_str           (Handle handle) -> const char*;

[[nodiscard]] auto get_axis_color  (unsigned int axis_mask) -> glm::vec4;

}
