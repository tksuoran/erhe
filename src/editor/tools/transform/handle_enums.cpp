#include "tools/transform/handle_enums.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

auto get_handle_class(const Handle handle) -> Handle_class
{
    switch (handle) {
        case Handle::e_handle_none           : return Handle_class::e_handle_class_none;
        case Handle::e_handle_translate_pos_x: return Handle_class::e_handle_class_axis;
        case Handle::e_handle_translate_neg_x: return Handle_class::e_handle_class_axis;
        case Handle::e_handle_translate_pos_y: return Handle_class::e_handle_class_axis;
        case Handle::e_handle_translate_neg_y: return Handle_class::e_handle_class_axis;
        case Handle::e_handle_translate_pos_z: return Handle_class::e_handle_class_axis;
        case Handle::e_handle_translate_neg_z: return Handle_class::e_handle_class_axis;
        case Handle::e_handle_translate_xy   : return Handle_class::e_handle_class_plane;
        case Handle::e_handle_translate_xz   : return Handle_class::e_handle_class_plane;
        case Handle::e_handle_translate_yz   : return Handle_class::e_handle_class_plane;
        case Handle::e_handle_rotate_x       : return Handle_class::e_handle_class_axis;
        case Handle::e_handle_rotate_y       : return Handle_class::e_handle_class_axis;
        case Handle::e_handle_rotate_z       : return Handle_class::e_handle_class_axis;
        case Handle::e_handle_scale_x        : return Handle_class::e_handle_class_axis;
        case Handle::e_handle_scale_y        : return Handle_class::e_handle_class_axis;
        case Handle::e_handle_scale_z        : return Handle_class::e_handle_class_axis;
        case Handle::e_handle_scale_xy       : return Handle_class::e_handle_class_plane;
        case Handle::e_handle_scale_xz       : return Handle_class::e_handle_class_plane;
        case Handle::e_handle_scale_yz       : return Handle_class::e_handle_class_plane;
        case Handle::e_handle_scale_xyz      : return Handle_class::e_handle_class_uniform;
        default:                               return Handle_class::e_handle_class_none;
    };
}

auto get_handle_axis(const Handle handle) -> Handle_axis
{
    switch (handle) {
        case Handle::e_handle_none           : return Handle_axis::e_handle_axis_none;
        case Handle::e_handle_translate_pos_x: return Handle_axis::e_handle_axis_x;
        case Handle::e_handle_translate_neg_x: return Handle_axis::e_handle_axis_x;
        case Handle::e_handle_translate_pos_y: return Handle_axis::e_handle_axis_y;
        case Handle::e_handle_translate_neg_y: return Handle_axis::e_handle_axis_y;
        case Handle::e_handle_translate_pos_z: return Handle_axis::e_handle_axis_z;
        case Handle::e_handle_translate_neg_z: return Handle_axis::e_handle_axis_z;
        case Handle::e_handle_translate_xy   : return Handle_axis::e_handle_axis_none;
        case Handle::e_handle_translate_xz   : return Handle_axis::e_handle_axis_none;
        case Handle::e_handle_translate_yz   : return Handle_axis::e_handle_axis_none;
        case Handle::e_handle_rotate_x       : return Handle_axis::e_handle_axis_x;
        case Handle::e_handle_rotate_y       : return Handle_axis::e_handle_axis_y;
        case Handle::e_handle_rotate_z       : return Handle_axis::e_handle_axis_z;
        case Handle::e_handle_scale_x        : return Handle_axis::e_handle_axis_x;
        case Handle::e_handle_scale_y        : return Handle_axis::e_handle_axis_y;
        case Handle::e_handle_scale_z        : return Handle_axis::e_handle_axis_z;
        case Handle::e_handle_scale_xy       : return Handle_axis::e_handle_axis_none;
        case Handle::e_handle_scale_xz       : return Handle_axis::e_handle_axis_none;
        case Handle::e_handle_scale_yz       : return Handle_axis::e_handle_axis_none;
        case Handle::e_handle_scale_xyz      : return Handle_axis::e_handle_axis_none;
        default:                               return Handle_axis::e_handle_axis_none;
    };
}

auto get_handle_plane(const Handle handle) -> Handle_plane
{
    switch (handle) {
        case Handle::e_handle_none           : return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_translate_pos_x: return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_translate_neg_x: return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_translate_pos_y: return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_translate_neg_y: return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_translate_pos_z: return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_translate_neg_z: return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_translate_xy   : return Handle_plane::e_handle_plane_xy;
        case Handle::e_handle_translate_xz   : return Handle_plane::e_handle_plane_xz;
        case Handle::e_handle_translate_yz   : return Handle_plane::e_handle_plane_yz;
        case Handle::e_handle_rotate_x       : return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_rotate_y       : return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_rotate_z       : return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_scale_x        : return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_scale_y        : return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_scale_z        : return Handle_plane::e_handle_plane_none;
        case Handle::e_handle_scale_xy       : return Handle_plane::e_handle_plane_xy;
        case Handle::e_handle_scale_xz       : return Handle_plane::e_handle_plane_xz;
        case Handle::e_handle_scale_yz       : return Handle_plane::e_handle_plane_yz;
        case Handle::e_handle_scale_xyz      : return Handle_plane::e_handle_plane_none;
        default:                               return Handle_plane::e_handle_plane_none;
    };
}

auto get_handle_tool(const Handle handle) -> Handle_tool
{
    switch (handle) {
        //using enum Handle;
        case Handle::e_handle_translate_pos_x: return Handle_tool::e_handle_tool_translate;
        case Handle::e_handle_translate_neg_x: return Handle_tool::e_handle_tool_translate;
        case Handle::e_handle_translate_pos_y: return Handle_tool::e_handle_tool_translate;
        case Handle::e_handle_translate_neg_y: return Handle_tool::e_handle_tool_translate;
        case Handle::e_handle_translate_pos_z: return Handle_tool::e_handle_tool_translate;
        case Handle::e_handle_translate_neg_z: return Handle_tool::e_handle_tool_translate;
        case Handle::e_handle_translate_xy:    return Handle_tool::e_handle_tool_translate;
        case Handle::e_handle_translate_xz:    return Handle_tool::e_handle_tool_translate;
        case Handle::e_handle_translate_yz:    return Handle_tool::e_handle_tool_translate;
        case Handle::e_handle_rotate_x:        return Handle_tool::e_handle_tool_rotate;
        case Handle::e_handle_rotate_y:        return Handle_tool::e_handle_tool_rotate;
        case Handle::e_handle_rotate_z:        return Handle_tool::e_handle_tool_rotate;
        case Handle::e_handle_scale_x:         return Handle_tool::e_handle_tool_scale;
        case Handle::e_handle_scale_y:         return Handle_tool::e_handle_tool_scale;
        case Handle::e_handle_scale_z:         return Handle_tool::e_handle_tool_scale;
        case Handle::e_handle_scale_xy:        return Handle_tool::e_handle_tool_scale;
        case Handle::e_handle_scale_xz:        return Handle_tool::e_handle_tool_scale;
        case Handle::e_handle_scale_yz:        return Handle_tool::e_handle_tool_scale;
        case Handle::e_handle_scale_xyz:       return Handle_tool::e_handle_tool_scale;
        case Handle::e_handle_none:            return Handle_tool::e_handle_tool_none;
        default: {
            ERHE_FATAL("bad handle %04x", static_cast<unsigned int>(handle));
        }
    }
}

auto get_handle_type(const Handle handle) -> Handle_type
{
    switch (handle) {
        //using enum Handle;
        case Handle::e_handle_translate_pos_x: return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_neg_x: return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_pos_y: return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_neg_y: return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_pos_z: return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_neg_z: return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_xy:    return Handle_type::e_handle_type_translate_plane;
        case Handle::e_handle_translate_xz:    return Handle_type::e_handle_type_translate_plane;
        case Handle::e_handle_translate_yz:    return Handle_type::e_handle_type_translate_plane;
        case Handle::e_handle_rotate_x:        return Handle_type::e_handle_type_rotate;
        case Handle::e_handle_rotate_y:        return Handle_type::e_handle_type_rotate;
        case Handle::e_handle_rotate_z:        return Handle_type::e_handle_type_rotate;
        case Handle::e_handle_scale_x:         return Handle_type::e_handle_type_scale_axis;
        case Handle::e_handle_scale_y:         return Handle_type::e_handle_type_scale_axis;
        case Handle::e_handle_scale_z:         return Handle_type::e_handle_type_scale_axis;
        case Handle::e_handle_scale_xy:        return Handle_type::e_handle_type_scale_plane;
        case Handle::e_handle_scale_xz:        return Handle_type::e_handle_type_scale_plane;
        case Handle::e_handle_scale_yz:        return Handle_type::e_handle_type_scale_plane;
        case Handle::e_handle_scale_xyz:       return Handle_type::e_handle_type_scale_uniform;
        case Handle::e_handle_none:            return Handle_type::e_handle_type_none;
        default: {
            ERHE_FATAL("bad handle %04x", static_cast<unsigned int>(handle));
        }
    }
}

auto get_axis_mask(const Handle handle) -> unsigned int
{
    switch (handle) {
        //using enum Handle;
        case Handle::e_handle_translate_pos_x: return Axis_mask::x;
        case Handle::e_handle_translate_neg_x: return Axis_mask::x;
        case Handle::e_handle_translate_pos_y: return Axis_mask::y;
        case Handle::e_handle_translate_neg_y: return Axis_mask::y;
        case Handle::e_handle_translate_pos_z: return Axis_mask::z;
        case Handle::e_handle_translate_neg_z: return Axis_mask::z;
        case Handle::e_handle_translate_xy:    return Axis_mask::x | Axis_mask::y;
        case Handle::e_handle_translate_xz:    return Axis_mask::x | Axis_mask::z;
        case Handle::e_handle_translate_yz:    return Axis_mask::y | Axis_mask::z;
        case Handle::e_handle_rotate_x:        return Axis_mask::x;
        case Handle::e_handle_rotate_y:        return Axis_mask::y;
        case Handle::e_handle_rotate_z:        return Axis_mask::z;
        case Handle::e_handle_scale_x:         return Axis_mask::x;
        case Handle::e_handle_scale_y:         return Axis_mask::y;
        case Handle::e_handle_scale_z:         return Axis_mask::z;
        case Handle::e_handle_scale_xy:        return Axis_mask::x | Axis_mask::y;
        case Handle::e_handle_scale_xz:        return Axis_mask::x | Axis_mask::z;
        case Handle::e_handle_scale_yz:        return Axis_mask::y | Axis_mask::z;
        case Handle::e_handle_scale_xyz:       return Axis_mask::x | Axis_mask::y | Axis_mask::z;
        case Handle::e_handle_none:            return 0;
        default: {
            ERHE_FATAL("bad handle %04x", static_cast<unsigned int>(handle));
        }
    }
}

auto c_str(const Handle handle) -> const char*
{
    switch (handle) {
        case Handle::e_handle_none           : return "None";
        case Handle::e_handle_translate_pos_x: return "Translate X";
        case Handle::e_handle_translate_neg_x: return "Translate X";
        case Handle::e_handle_translate_pos_y: return "Translate Y";
        case Handle::e_handle_translate_neg_y: return "Translate Y";
        case Handle::e_handle_translate_pos_z: return "Translate Z";
        case Handle::e_handle_translate_neg_z: return "Translate Z";
        case Handle::e_handle_translate_xy   : return "Translate XY";
        case Handle::e_handle_translate_xz   : return "Translate XZ";
        case Handle::e_handle_translate_yz   : return "Translate YZ";
        case Handle::e_handle_rotate_x       : return "Rotate X";
        case Handle::e_handle_rotate_y       : return "Rotate Y";
        case Handle::e_handle_rotate_z       : return "Rotate Z";
        case Handle::e_handle_scale_x        : return "Scale X";
        case Handle::e_handle_scale_y        : return "Scale Y";
        case Handle::e_handle_scale_z        : return "Scale Z";
        case Handle::e_handle_scale_xy       : return "Scale XY";
        case Handle::e_handle_scale_xz       : return "Scale YZ";
        case Handle::e_handle_scale_yz       : return "Scale YZ";
        case Handle::e_handle_scale_xyz      : return "Scale XYZ";
        default: return "?";
    };
}

auto get_axis_color(const unsigned int axis_mask) -> glm::vec4
{
    switch (axis_mask) {
        //using enum Handle;
        case Axis_mask::x:  return glm::vec4{2.0f, 0.0f, 0.0f, 1.0f};
        case Axis_mask::y:  return glm::vec4{0.0f, 2.0f, 0.0f, 1.0f};
        case Axis_mask::z:  return glm::vec4{0.0f, 0.0f, 2.0f, 1.0f};
        case Axis_mask::yz: return glm::vec4{2.0f, 0.0f, 0.0f, 1.0f};
        case Axis_mask::xz: return glm::vec4{0.0f, 2.0f, 0.0f, 1.0f};
        case Axis_mask::xy: return glm::vec4{0.0f, 0.0f, 2.0f, 1.0f};
        default:            return glm::vec4{0.7f, 0.7f, 0.7f, 1.0f};
    }
}

} // namespace editor
