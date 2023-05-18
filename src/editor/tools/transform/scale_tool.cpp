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

auto Scale_tool::update(
    Scene_view* scene_view
) -> bool
{
    Viewport_window* viewport_window = scene_view->as_viewport_window();

    switch (std::popcount(m_axis_mask)) {
        case 1: {
            if (viewport_window != nullptr) {
                g_scale_tool->update_axis_2d(viewport_window);
            } else {
                g_scale_tool->update_axis_3d(scene_view);
            }
            return true;
        }

        case 2: {
            if (viewport_window != nullptr) {
                g_scale_tool->update_plane_2d(viewport_window);
            } else {
                g_scale_tool->update_plane_3d(scene_view);
            }
            return true;
        }

        case 3: // TODO

        default: {
            return false;
        }
    }
}

void Scale_tool::update_axis_2d(Viewport_window* viewport_window)
{
    ERHE_PROFILE_FUNCTION();

    if (viewport_window == nullptr) {
        return;
    }
    const auto position_in_viewport_opt = viewport_window->get_position_in_viewport();
    if (!position_in_viewport_opt.has_value()) {
        return;
    }

    const auto& shared = get_shared();
    const vec3 drag_world_direction = get_axis_direction();
    const vec3 P0        = shared.drag.initial_position_in_world - drag_world_direction;
    const vec3 P1        = shared.drag.initial_position_in_world + drag_world_direction;
    const auto ss_P0_opt = viewport_window->project_to_viewport(P0);
    const auto ss_P1_opt = viewport_window->project_to_viewport(P1);
    if (
        !ss_P0_opt.has_value() ||
        !ss_P1_opt.has_value()
    ) {
        return;
    }

    const vec3 ss_P0      = ss_P0_opt.value();
    const vec3 ss_P1      = ss_P1_opt.value();
    const auto ss_closest = erhe::toolkit::closest_point<float>(
        vec2{ss_P0},
        vec2{ss_P1},
        vec2{position_in_viewport_opt.value()}
    );

    if (ss_closest.has_value()) {
        const auto R0_opt = viewport_window->unproject_to_world(vec3{ss_closest.value(), 0.0f});
        const auto R1_opt = viewport_window->unproject_to_world(vec3{ss_closest.value(), 1.0f});
        if (R0_opt.has_value() && R1_opt.has_value()) {
            const auto R0 = R0_opt.value();
            const auto R1 = R1_opt.value();
            const auto closest_points_r = erhe::toolkit::closest_points<float>(P0, P1, R0, R1);
            if (closest_points_r.has_value()) {
                update_final(closest_points_r.value().P);
            }
        }
    } else {
        const auto Q0_opt = viewport_window->position_in_world_viewport_depth(1.0);
        const auto Q1_opt = viewport_window->position_in_world_viewport_depth(0.0);
        if (Q0_opt.has_value() && Q1_opt.has_value()) {
            const auto Q0 = Q0_opt.value();
            const auto Q1 = Q1_opt.value();
            const auto closest_points_q = erhe::toolkit::closest_points<float>(P0, P1, Q0, Q1);
            if (closest_points_q.has_value()) {
                update_final(closest_points_q.value().P);
            }
        }
    }
}

// TODO Refactor share with update_axis_translate_3d()
void Scale_tool::update_axis_3d(Scene_view* scene_view)
{
    ERHE_PROFILE_FUNCTION();

    if (scene_view == nullptr) {
        log_trs_tool->trace("scene_view == nullptr");
        return;
    }

    const auto& shared = get_shared();
    const vec3 drag_world_direction = get_axis_direction();
    const vec3 P0                   = shared.drag.initial_position_in_world - drag_world_direction;
    const vec3 P1                   = shared.drag.initial_position_in_world + drag_world_direction;
    const auto Q_origin_opt         = scene_view->get_control_ray_origin_in_world();
    const auto Q_direction_opt      = scene_view->get_control_ray_direction_in_world();
    if (!
        Q_origin_opt.has_value() ||
        !Q_direction_opt.has_value()
    ) {
        log_trs_tool->trace("! (Q_origin_opt.has_value() && Q_direction_opt.has_value())");
        return;
    }

    const auto Q0 = Q_origin_opt.value();
    const auto Q1 = Q0 + Q_direction_opt.value();
    const auto closest_points_q = erhe::toolkit::closest_points<float>(P0, P1, Q0, Q1);
    if (!closest_points_q.has_value()) {
        log_trs_tool->trace("!closest_points_q.has_value()");
    }

    update_final(closest_points_q.value().P);
}

void Scale_tool::update_final(const vec3 drag_position_in_world)
{
    const auto& shared = get_shared();
    const float initial_distance          = glm::distance(shared.anchor_state_initial.pivot_point_in_world, shared.drag.initial_position_in_world);
    const float current_distance          = glm::distance(shared.anchor_state_initial.pivot_point_in_world, drag_position_in_world);
    const float scale_value               = current_distance / initial_distance;
    const float snapped_scale_value       = scale_value; // TODO
    const mat4  scale                     = erhe::toolkit::create_scale<float>(snapped_scale_value);

    const mat4  translate                 = erhe::toolkit::create_translation<float>(vec3{-shared.anchor_state_initial.pivot_point_in_world});
    const mat4  untranslate               = erhe::toolkit::create_translation<float>(vec3{ shared.anchor_state_initial.pivot_point_in_world});
    const mat4  updated_world_from_anchor = untranslate * scale * translate * shared.drag.initial_world_from_anchor;

    touch();
    g_transform_tool->update_world_from_anchor_transform(updated_world_from_anchor);
    g_transform_tool->update_transforms();
}

// TODO Refactor share with update_plane_translate_2d()
void Scale_tool::update_plane_2d(Viewport_window* viewport_window)
{
    ERHE_PROFILE_FUNCTION();

    if (viewport_window == nullptr) {
        return;
    }

    const auto& shared  = get_shared();
    const vec3  p0      = shared.drag.initial_position_in_world;
    const vec3  world_n = get_plane_normal(!shared.settings.local);
    const auto  Q0_opt  = viewport_window->position_in_world_viewport_depth(1.0);
    const auto  Q1_opt  = viewport_window->position_in_world_viewport_depth(0.0);
    if (
        !Q0_opt.has_value() ||
        !Q1_opt.has_value()
    ) {
        return;
    }
    const vec3 Q0 = Q0_opt.value();
    const vec3 Q1 = Q1_opt.value();
    const vec3 v  = normalize(Q1 - Q0);

    const auto intersection = erhe::toolkit::intersect_plane<float>(world_n, p0, Q0, v);
    if (!intersection.has_value()) {
        return;
    }

    const vec3 drag_point_new_position_in_world = Q0 + intersection.value() * v;
    update_final(drag_point_new_position_in_world);
}

void Scale_tool::update_plane_3d(Scene_view* scene_view)
{
    ERHE_PROFILE_FUNCTION();

    if (scene_view == nullptr) {
        return;
    }

    const auto& shared          = get_shared();
    const vec3  p0              = shared.drag.initial_position_in_world;
    const vec3  world_n         = get_plane_normal(!shared.settings.local);
    const auto  Q_origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto  Q_direction_opt = scene_view->get_control_ray_direction_in_world();
    if (
        !Q_origin_opt.has_value() ||
        !Q_direction_opt.has_value()
    ) {
        return;
    }
    const vec3 Q0 = Q_origin_opt.value();
    const vec3 Q1 = Q0 + Q_direction_opt.value();
    const vec3 v  = normalize(Q1 - Q0);

    const auto intersection = erhe::toolkit::intersect_plane<float>(world_n, p0, Q0, v);
    if (!intersection.has_value()) {
        return;
    }

    const vec3 drag_point_new_position_in_world = Q0 + intersection.value() * v;
    update_final(drag_point_new_position_in_world);
}

} // namespace editor
