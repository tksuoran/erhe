#include "transform/move_tool.hpp"
#include "windows/property_editor.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "input_state.hpp"
#include "graphics/icon_set.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"
#include "transform/handle_enums.hpp"
#include "transform/transform_tool.hpp"

#include <imgui/imgui.h>

#include <bit>

namespace editor {

using namespace glm;

Move_tool::Move_tool(App_context& app_context, Icon_set& icon_set, Tools& tools)
    : Subtool{app_context, tools, Tool_flags::toolbox | Tool_flags::allow_secondary}
{
    set_base_priority  (c_priority);
    set_description    ("Move Tool");
    set_icon           (icon_set.custom_icons, icon_set.icons.move);
}

Move_tool::~Move_tool() noexcept = default;

void Move_tool::handle_priority_update(const int old_priority, const int new_priority)
{
    auto& shared = get_shared();
    shared.settings.show_translate = new_priority > old_priority;
}

void Move_tool::imgui(Property_editor& property_editor)
{
    Property_editor& p = property_editor;
    p.reset();
    p.push_group("Move tool", ImGuiTreeNodeFlags_DefaultOpen);
    p.add_entry("Snap Enable", [this]() { ImGui::Checkbox("##", &get_shared().settings.translate_snap_enable); });
    // Persistent preference (Transform_tool_config); touch() schedules the autosave.
    p.add_entry("Snap Absolute", [this]() {
        if (ImGui::Checkbox("##", &m_context.editor_settings->transform_tool.translate_snap_absolute)) {
            m_context.app_settings->settings_store().touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Snap the anchor's resulting position coordinates to snap multiples; when off, snap the drag delta instead");
        }
    });
    p.add_entry("Snap Value", [this]() {
        const float snap_values[] = {  0.001f,  0.01f,  0.1f,  0.2f,  0.25f,  0.5f,  1.0f,  2.0f,  5.0f,  10.0f,  100.0f };
        const char* snap_items [] = { "0.001", "0.01", "0.1", "0.2", "0.25", "0.5", "1.0", "2.0", "5.0", "10.0", "100.0" };
        if (ImGui::BeginCombo("##", snap_items[m_translate_snap_index])) {
            ImGui::TextUnformatted("Translate Snap Value:");
            for (int i = 0, end = IM_ARRAYSIZE(snap_items); i < end; ++i) {
                bool selected = (i == m_translate_snap_index);
                bool clicked = ImGui::Selectable(snap_items[i], &selected, ImGuiSelectableFlags_None);
                if (clicked) {
                    m_translate_snap_index = i;
                }
            }
            ImGui::EndCombo();
        }

        if (
            (m_translate_snap_index >= 0) &&
            (m_translate_snap_index < IM_ARRAYSIZE(snap_values))
        ) {
            get_shared().settings.translate_snap = snap_values[m_translate_snap_index];
        }
    });
    p.pop_group();
    p.show_entries();
}

auto Move_tool::begin(unsigned int axis_mask, Scene_view* scene_view) -> bool
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
            const vec3 P0                   = shared.get_initial_drag_position_in_world() - drag_world_direction;
            const vec3 P1                   = shared.get_initial_drag_position_in_world() + drag_world_direction;
            const auto closest_point        = scene_view->get_closest_point_on_line(P0, P1);
            if (closest_point.has_value()) {
                update(closest_point.value());
                return true;
            }
            return false;
        }

        case 2: {
            const vec3 P             = shared.get_initial_drag_position_in_world();
            const vec3 N             = get_plane_normal(!shared.settings.use_anchor_orientation());
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

auto Move_tool::snap(const glm::vec3 in_translation) const -> glm::vec3
{
    const auto& shared = get_shared();

    // Snap when the toggle is enabled or while Control is held. The live key state
    // is read each update, so toggling Control mid-drag takes effect immediately.
    const bool snap_enabled = shared.settings.translate_snap_enable || m_context.input_state->control;
    if (!snap_enabled) {
        return in_translation;
    }

    // Snap component-wise in the ACTIVE space (Global / Local / Reference /
    // Selection): the basis maps active-space axes to world, so express the
    // delta - and, for absolute snapping, the anchor's start position - in
    // that basis, snap the masked components there, and map back. In Global
    // the basis is identity and this is plain world-component snapping.
    // Absolute snapping lands the anchor's resulting active-space position
    // coordinates on snap multiples (bias = the anchor's start position);
    // relative snapping snaps the drag delta itself, preserving any initial
    // off-grid position.
    const bool world            = !shared.settings.use_anchor_orientation();
    glm::mat3  world_from_basis = glm::mat3{get_basis(world)};
    // The anchor matrix can carry scale; normalized columns make the basis
    // orthonormal so the transpose below is its inverse.
    world_from_basis[0] = glm::normalize(world_from_basis[0]);
    world_from_basis[1] = glm::normalize(world_from_basis[1]);
    world_from_basis[2] = glm::normalize(world_from_basis[2]);
    const glm::mat3 basis_from_world = glm::transpose(world_from_basis);
    const glm::vec3 bias = m_context.editor_settings->transform_tool.translate_snap_absolute
        ? basis_from_world * glm::vec3{shared.world_from_anchor_initial_state.get_translation()}
        : glm::vec3{0.0f};
    const glm::vec3 t    = basis_from_world * in_translation;
    const float     snap = shared.settings.translate_snap;
    const float x = (m_axis_mask & Axis_mask::x) ? std::floor((bias.x + t.x + snap * 0.5f) / snap) * snap - bias.x : t.x;
    const float y = (m_axis_mask & Axis_mask::y) ? std::floor((bias.y + t.y + snap * 0.5f) / snap) * snap - bias.y : t.y;
    const float z = (m_axis_mask & Axis_mask::z) ? std::floor((bias.z + t.z + snap * 0.5f) / snap) * snap - bias.z : t.z;

    return world_from_basis * glm::vec3{x, y, z};
}

void Move_tool::update(const vec3 drag_position_in_world)
{
    const auto& shared = get_shared();

    const vec3 translation_vector  = drag_position_in_world - shared.get_initial_drag_position_in_world();
    const vec3 snapped_translation = snap(translation_vector);

    m_context.transform_tool->adjust_translation(snapped_translation);
}

}
