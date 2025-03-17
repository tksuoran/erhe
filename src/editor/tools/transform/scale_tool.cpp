#include "tools/transform/scale_tool.hpp"
#include "windows/property_editor.hpp"

#include "editor_context.hpp"
#include "graphics/icon_set.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"
#include "tools/transform/handle_enums.hpp"
#include "tools/transform/transform_tool.hpp"

namespace editor {

using namespace glm;

Scale_tool::Scale_tool(Editor_context& editor_context, Icon_set& icon_set, Tools& tools)
    : Subtool{editor_context}
{
    set_base_priority(c_priority);
    set_description  ("Scale");
    set_flags        (Tool_flags::toolbox | Tool_flags::allow_secondary);
    set_icon         (icon_set.icons.scale);
    tools.register_tool(this);
}

void Scale_tool::handle_priority_update(const int old_priority, const int new_priority)
{
    auto& shared = get_shared();
    shared.settings.show_scale = new_priority > old_priority;
}

auto Scale_tool::begin(const unsigned int axis_mask, Scene_view* scene_view) -> bool
{
    m_axis_mask = axis_mask;
    m_active    = true;
    return (axis_mask != 0) && (scene_view != nullptr);
}

auto Scale_tool::update(Scene_view* scene_view) -> bool
{
    using vec3 = glm::vec3;

    if ((m_axis_mask == 0) || (scene_view == nullptr)) {
        return false;
    }

    const auto& shared = get_shared();
    switch (std::popcount(m_axis_mask)) {
        case 1: {
            const vec3 drag_world_direction = get_axis_direction();
            const vec3 P0                   = shared.initial_drag_position_in_world - drag_world_direction;
            const vec3 P1                   = shared.initial_drag_position_in_world + drag_world_direction;
            const auto closest_point        = scene_view->get_closest_point_on_line(P0, P1);
            if (closest_point.has_value()) {
                update(closest_point.value());
                return true;
            }
            return false;
        }

        case 2: {
            const vec3 P             = shared.initial_drag_position_in_world;
            const vec3 N             = get_plane_normal(!shared.settings.local);
            const auto closest_point = scene_view->get_closest_point_on_plane(N, P);
            if (closest_point.has_value()) {
                update(closest_point.value());
                return true;
            }
            return false;
        }

        default: {
            return false;
        }
    }
}

void Scale_tool::update(const vec3 drag_position_in_world)
{
    const auto& shared           = get_shared();
    const vec3  center_of_scale  = shared.world_from_anchor_initial_state.get_translation();
    const float initial_distance = glm::distance(center_of_scale, shared.initial_drag_position_in_world);
    const float current_distance = glm::distance(center_of_scale, drag_position_in_world);
    const float s                = current_distance / initial_distance;
    const vec3  scale = [&](){
        switch (m_axis_mask) {
            case Axis_mask::x  : return vec3{s, 1.0f, 1.0f};
            case Axis_mask::y  : return vec3{1.0f, s, 1.0f};
            case Axis_mask::z  : return vec3{1.0f, 1.0f, s};
            case Axis_mask::xy : return vec3{s, s, 1.0f};
            case Axis_mask::xz : return vec3{s, 1.0f, s};
            case Axis_mask::yz : return vec3{1.0f, s, s};
            case Axis_mask::xyz: return vec3{s, s, s};
            default:             return vec3{1.0f};
        }
    }();

    m_context.transform_tool->adjust_scale(center_of_scale, scale);
}

} // namespace editor
