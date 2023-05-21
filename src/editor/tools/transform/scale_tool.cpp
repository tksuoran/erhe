#include "tools/transform/scale_tool.hpp"

#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_window.hpp"
#include "tools/tools.hpp"
#include "tools/transform/handle_enums.hpp"
#include "tools/transform/transform_tool.hpp"

#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor
{

using namespace glm;

Scale_tool* g_scale_tool{nullptr};

Scale_tool::Scale_tool()
    : erhe::components::Component{c_type_name}
{
}

Scale_tool::~Scale_tool() noexcept
{
    ERHE_VERIFY(g_scale_tool == nullptr);
}

void Scale_tool::deinitialize_component()
{
    ERHE_VERIFY(g_scale_tool == this);
    g_scale_tool = nullptr;
}

void Scale_tool::declare_required_components()
{
    require<Icon_set>();
    require<Tools   >();
}

void Scale_tool::initialize_component()
{
    ERHE_VERIFY(g_scale_tool == nullptr);
    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::toolbox | Tool_flags::allow_secondary);
    set_icon         (g_icon_set->icons.scale);
    g_tools->register_tool(this);
    g_scale_tool = this;
}

void Scale_tool::handle_priority_update(
    const int old_priority,
    const int new_priority
)
{
    auto& shared = get_shared();
    shared.settings.show_scale = new_priority > old_priority;
}

void Scale_tool::imgui()
{
}

auto Scale_tool::begin(
    const unsigned int axis_mask,
    Scene_view*        scene_view
) -> bool
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
            const vec3 P0                   = shared.drag.initial_position_in_world - drag_world_direction;
            const vec3 P1                   = shared.drag.initial_position_in_world + drag_world_direction;
            const auto closest_point        = scene_view->get_closest_point_on_line(P0, P1);
            if (closest_point.has_value()) {
                update(closest_point.value());
                return true;
            }
            return false;
        }

        case 2: {
            const vec3 P             = shared.drag.initial_position_in_world;
            const vec3 N             = get_plane_normal(!shared.settings.local);
            const auto closest_point = scene_view->get_closest_point_on_plane(N, P);
            if (closest_point.has_value()) {
                update(closest_point.value());
                return true;
            }
        }

        default: {
            return false;
        }
    }
}

void Scale_tool::update(const vec3 drag_position_in_world)
{
    const auto& shared           = get_shared();
    const float initial_distance = glm::distance(shared.anchor_state_initial.pivot_point_in_world, shared.drag.initial_position_in_world);
    const float current_distance = glm::distance(shared.anchor_state_initial.pivot_point_in_world, drag_position_in_world);
    const float s                = current_distance / initial_distance;
    const mat4  scale = [&](){
        switch (m_axis_mask) {
        case Axis_mask::x  : return erhe::toolkit::create_scale<float>(s, 1.0f, 1.0f);
        case Axis_mask::y  : return erhe::toolkit::create_scale<float>(1.0f, s, 1.0f);
        case Axis_mask::z  : return erhe::toolkit::create_scale<float>(1.0f, 1.0f, s);
        case Axis_mask::xy : return erhe::toolkit::create_scale<float>(s, s, 1.0f);
        case Axis_mask::xz : return erhe::toolkit::create_scale<float>(s, 1.0f, s);
        case Axis_mask::yz : return erhe::toolkit::create_scale<float>(1.0f, s, s);
        case Axis_mask::xyz: return erhe::toolkit::create_scale<float>(s, s, s);
        default:             return glm::mat4{1.0f};
        }
    }();
    const mat4 translate                 = erhe::toolkit::create_translation<float>(vec3{-shared.anchor_state_initial.pivot_point_in_world});
    const mat4 untranslate               = erhe::toolkit::create_translation<float>(vec3{ shared.anchor_state_initial.pivot_point_in_world});
    const mat4 updated_world_from_anchor = untranslate * scale * translate * shared.drag.initial_world_from_anchor;

    touch();
    g_transform_tool->update_world_from_anchor_transform(updated_world_from_anchor);
    g_transform_tool->update_transforms();
}

} // namespace editor
