#include "tools/transform/move_tool.hpp"

#include "editor_context.hpp"
#include "graphics/icon_set.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_window.hpp"
#include "tools/selection_tool.hpp"
#include "tools/transform/handle_enums.hpp"
#include "tools/transform/transform_tool.hpp"

#include "erhe/imgui/imgui_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <bit>

namespace editor
{

using namespace glm;

Move_tool::Move_tool(
    Editor_context& editor_context,
    Icon_set&       icon_set
)
    : Subtool{editor_context}
{
    set_base_priority(c_priority);
    set_description  ("Move Tool");
    set_flags        (Tool_flags::toolbox | Tool_flags::allow_secondary);
    set_icon         (icon_set.icons.move);
}

Move_tool::~Move_tool() noexcept = default;

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
    erhe::imgui::make_combo(
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

auto Move_tool::update(Scene_view* scene_view) -> bool
{
    if (scene_view == nullptr) {
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
        }

        default: {
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

void Move_tool::update(const vec3 drag_position_in_world)
{
    const auto& shared = get_shared();

    const vec3 translation_vector  = drag_position_in_world - shared.initial_drag_position_in_world;
    const vec3 snapped_translation = snap(translation_vector);

    m_context.transform_tool->adjust_translation(snapped_translation);
}

} // namespace editor
