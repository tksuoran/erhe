#include "tools/transform/move_tool.hpp"

#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_window.hpp"
#include "tools/tools.hpp"
#include "tools/selection_tool.hpp"
#include "tools/transform/handle_enums.hpp"
#include "tools/transform/transform_tool.hpp"
#include "tools/transform/rotate_tool.hpp"

#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <bit>

namespace editor
{

using namespace glm;

Move_tool* g_move_tool{nullptr};

Move_tool::Move_tool()
    : erhe::components::Component{c_type_name}
{
}

Move_tool::~Move_tool() noexcept
{
    ERHE_VERIFY(g_move_tool == nullptr);
}

void Move_tool::deinitialize_component()
{
    ERHE_VERIFY(g_move_tool == this);
    g_move_tool = nullptr;
}

void Move_tool::declare_required_components()
{
    require<Icon_set>();
    require<Tools   >();
}

void Move_tool::initialize_component()
{
    ERHE_VERIFY(g_move_tool == nullptr);

    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::toolbox | Tool_flags::allow_secondary);
    set_icon         (g_icon_set->icons.move);
    g_tools->register_tool(this);

    g_move_tool = this;
}

void Move_tool::handle_priority_update(
    const int old_priority,
    const int new_priority
)
{
    auto& shared = get_shared();
    shared.settings.show_translate = new_priority > old_priority;
}

void Move_tool::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    auto& shared = get_shared();

    ImGui::Checkbox("Translate Snap Enable", &shared.settings.translate_snap_enable);
    const float translate_snap_values[] = {  0.001f,  0.01f,  0.1f,  0.2f,  0.25f,  0.5f,  1.0f,  2.0f,  5.0f,  10.0f,  100.0f };
    const char* translate_snap_items [] = { "0.001", "0.01", "0.1", "0.2", "0.25", "0.5", "1.0", "2.0", "5.0", "10.0", "100.0" };
    erhe::application::make_combo(
        "Translate Snap",
        m_translate_snap_index,
        translate_snap_items,
        IM_ARRAYSIZE(translate_snap_items)
    );
    if (
        (m_translate_snap_index >= 0) &&
        (m_translate_snap_index < IM_ARRAYSIZE(translate_snap_values))
    ) {
        shared.settings.translate_snap = translate_snap_values[m_translate_snap_index];
    }
#endif
}

auto Move_tool::begin(
    unsigned int axis_mask,
    Scene_view*  scene_view
) -> bool
{
    static_cast<void>(scene_view);
    m_axis_mask = axis_mask;
    m_active    = true;
    return (axis_mask != 0) && (scene_view != nullptr);
}

auto Move_tool::update(
    Scene_view*        scene_view
) -> bool
{
    Viewport_window* viewport_window = scene_view->as_viewport_window();

    switch (std::popcount(m_axis_mask)) {
        case 1: {
            if (viewport_window != nullptr) {
                update_axis_2d(viewport_window);
            } else {
                update_axis_3d(scene_view);
            }
            return true;
        }

        case 2: {
            if (viewport_window != nullptr) {
                update_plane_2d(viewport_window);
            } else {
                update_plane_3d(scene_view);
            }
            return true;
        }

        default:
        {
            return false;
        }
    }
}

auto Move_tool::snap(const glm::vec3 in_translation) const -> glm::vec3
{
    const auto& shared = get_shared();

    if (!shared.settings.translate_snap_enable) {
        return in_translation;
    }

    const float in_x = in_translation.x;
    const float in_y = in_translation.y;
    const float in_z = in_translation.z;
    const float snap = shared.settings.translate_snap;
    const float x    = (m_axis_mask & Axis_mask::x) ? std::floor((in_x + snap * 0.5f) / snap) * snap : in_x;
    const float y    = (m_axis_mask & Axis_mask::y) ? std::floor((in_y + snap * 0.5f) / snap) * snap : in_y;
    const float z    = (m_axis_mask & Axis_mask::z) ? std::floor((in_z + snap * 0.5f) / snap) * snap : in_z;

    return glm::vec3{x, y, z};
}

void Move_tool::update_axis_2d(Viewport_window* viewport_window) 
{
    ERHE_PROFILE_FUNCTION();

    const auto& shared = get_shared();

    if (m_axis_mask == 0) {
        return;
    }

    if (viewport_window == nullptr) {
        return;
    }
    const auto position_in_viewport_opt = viewport_window->get_position_in_viewport();
    if (!position_in_viewport_opt.has_value()) {
        return;
    }

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
                update_axis_final(closest_points_r.value().P);
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
                update_axis_final(closest_points_q.value().P);
            }
        }
    }
}

void Move_tool::update_axis_3d(Scene_view* scene_view)
{
    ERHE_PROFILE_FUNCTION();

    const auto& shared = get_shared();

    if (scene_view == nullptr) {
        log_trs_tool->trace("scene_view == nullptr");
        return;
    }

    const vec3 drag_world_direction = get_axis_direction();
    const vec3 P0 = shared.drag.initial_position_in_world - drag_world_direction;
    const vec3 P1 = shared.drag.initial_position_in_world + drag_world_direction;
    const auto Q_origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto Q_direction_opt = scene_view->get_control_ray_direction_in_world();
    if (Q_origin_opt.has_value() && Q_direction_opt.has_value()) {
        const auto Q0 = Q_origin_opt.value();
        const auto Q1 = Q0 + Q_direction_opt.value();
        const auto closest_points_q = erhe::toolkit::closest_points<float>(P0, P1, Q0, Q1);
        if (closest_points_q.has_value()) {
            update_axis_final(closest_points_q.value().P);
        } else {
            log_trs_tool->trace("!closest_points_q.has_value()");
        }
    } else {
        log_trs_tool->trace("! (Q_origin_opt.has_value() && Q_direction_opt.has_value())");
    }
}

void Move_tool::update_axis_final(const vec3 drag_position_in_world)
{
    const auto& shared = get_shared();

    const vec3 translation_vector        = drag_position_in_world - shared.drag.initial_position_in_world;
    const vec3 snapped_translation       = snap(translation_vector);
    const mat4 translation               = erhe::toolkit::create_translation<float>(snapped_translation);
    const mat4 updated_world_from_anchor = translation * shared.drag.initial_world_from_anchor;

    touch();
    g_transform_tool->update_world_from_anchor_transform(updated_world_from_anchor);
    g_transform_tool->update_transforms();
}

void Move_tool::update_plane_2d(Viewport_window* viewport_window)
{
    ERHE_PROFILE_FUNCTION();

    if (viewport_window == nullptr) {
        return;
    }

    const auto& shared = get_shared();
    const vec3 p0      = shared.drag.initial_position_in_world;
    const vec3 world_n = get_plane_normal(!shared.settings.local);
    const auto Q0_opt  = viewport_window->position_in_world_viewport_depth(1.0);
    const auto Q1_opt  = viewport_window->position_in_world_viewport_depth(0.0);
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
    const vec3 translation_vector               = drag_point_new_position_in_world - shared.drag.initial_position_in_world;
    const vec3 snapped_translation              = snap(translation_vector);
    const mat4 translation                      = erhe::toolkit::create_translation<float>(snapped_translation);
    const mat4 updated_world_from_anchor        = translation * shared.drag.initial_world_from_anchor;

    touch();
    g_transform_tool->update_world_from_anchor_transform(updated_world_from_anchor);
    g_transform_tool->update_transforms();
}

void Move_tool::update_plane_3d(Scene_view* scene_view)
{
    ERHE_PROFILE_FUNCTION();

    if (scene_view == nullptr) {
        return;
    }

    const auto& shared = get_shared();
    const vec3 p0              = shared.drag.initial_position_in_world;
    const vec3 world_n         = get_plane_normal(!shared.settings.local);
    const auto Q_origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto Q_direction_opt = scene_view->get_control_ray_direction_in_world();
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
    const vec3 translation_vector               = drag_point_new_position_in_world - shared.drag.initial_position_in_world;
    const vec3 snapped_translation              = snap(translation_vector);
    const mat4 translation                      = erhe::toolkit::create_translation<float>(snapped_translation);
    const mat4 updated_world_from_anchor        = translation * shared.drag.initial_world_from_anchor;

    touch();
    g_transform_tool->update_world_from_anchor_transform(updated_world_from_anchor);
    g_transform_tool->update_transforms();
}

} // namespace editor
