#include "windows/physics_window.hpp"

#include "editor_context.hpp"
#include "editor_scenes.hpp"
#include "editor_settings.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

namespace editor
{

Physics_window::Physics_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, Editor_context& editor_context)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Physics", "physics"}
    , m_context                {editor_context}
{
}

void Physics_window::viewport_toolbar(bool& hovered)
{
    bool physics_enabled = m_context.editor_settings->physics.dynamic_enable;

    ImGui::SameLine();
    const bool pressed = erhe::imgui::make_button("P", (physics_enabled) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal);
    if (ImGui::IsItemHovered()) {
        hovered = true;
        ImGui::SetTooltip(
            physics_enabled ? "Toggle physics on -> off" : "Toggle physics off -> on"
        );
    };

    if (pressed && m_context.editor_settings->physics.static_enable) {
        m_context.editor_settings->physics.dynamic_enable = !m_context.editor_settings->physics.dynamic_enable;
    }
}

void Physics_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    if (!m_context.editor_settings->physics.static_enable) {
        ImGui::BeginDisabled();
    }
    ImGui::Checkbox("Physics enabled", &m_context.editor_settings->physics.dynamic_enable);
    if (!m_context.editor_settings->physics.static_enable) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("erhe.ini has [physics] static_enable = false");
        }
    }

    if (!m_context.developer_mode) {
        return;
    }

    const auto& scene_roots = m_context.editor_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        if (!ImGui::TreeNodeEx(scene_root->get_name().c_str())) {
            continue;
        }

        const auto& physics_world = scene_root->get_physics_world();
        const auto& debug_info = physics_world.describe();
        for (const auto& line : debug_info) {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::TreePop();
    }


    //// if (g_debug_draw != nullptr) {  TODO
    ////     if (ImGui::CollapsingHeader("Visualizations")) {
    ////         ImGui::Checkbox("Enabled", &m_debug_draw.enable);
    ////         if (m_debug_draw.enable) {
    ////             ImGui::Checkbox("Wireframe",         &m_debug_draw.wireframe        );
    ////             ImGui::Checkbox("AABB",              &m_debug_draw.aabb             );
    ////             ImGui::Checkbox("Context Points",    &m_debug_draw.contact_points   );
    ////             ImGui::Checkbox("No Deactivation",   &m_debug_draw.no_deactivation  );
    ////             ImGui::Checkbox("Constraints",       &m_debug_draw.constraints      );
    ////             ImGui::Checkbox("Constraint Limits", &m_debug_draw.constraint_limits);
    ////             ImGui::Checkbox("Normals",           &m_debug_draw.normals          );
    ////             ImGui::Checkbox("Frames",            &m_debug_draw.frames           );
    ////         }
    //// 
    ////         //const ImVec2 color_button_size{32.0f, 32.0f};
    ////         ImGui::SliderFloat("Line Width", &g_debug_draw->line_width, 0.0f, 10.0f);
    ////         ImGui::ColorEdit3("Active",                &m_debug_draw.colors.active_object               .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
    ////         ImGui::ColorEdit3("Deactivated",           &m_debug_draw.colors.deactivated_object          .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
    ////         ImGui::ColorEdit3("Wants Deactivation",    &m_debug_draw.colors.wants_deactivation_object   .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
    ////         ImGui::ColorEdit3("Disabled Deactivation", &m_debug_draw.colors.disabled_deactivation_object.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
    ////         ImGui::ColorEdit3("Disabled Simulation",   &m_debug_draw.colors.disabled_simulation_object  .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
    ////         ImGui::ColorEdit3("AABB",                  &m_debug_draw.colors.aabb                        .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
    ////         ImGui::ColorEdit3("Contact Point",         &m_debug_draw.colors.contact_point               .x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
    ////     }
    //// }

    const auto viewport_scene_view = m_context.scene_views->last_scene_view();
    if (!viewport_scene_view) {
        return;
    }

    const auto scene_root = viewport_scene_view->get_scene_root();
    if (!scene_root) {
        return;
    }

    auto& physics_world = scene_root->get_physics_world();
    const auto gravity = physics_world.get_gravity();
    {
        float floats[3] = { gravity.x, gravity.y, gravity.z };
        ImGui::InputFloat3("Gravity", floats);
        glm::vec3 updated_gravity{ floats[0], floats[1], floats[2] };
        if (updated_gravity != gravity) {
            physics_world.set_gravity(updated_gravity);
        }
    }
#endif
}

auto Physics_window::get_debug_draw_parameters() -> Debug_draw_parameters
{
    return m_debug_draw;
}

//// TODO void Physics_window::tool_render(
//// TODO     const Render_context& context
//// TODO )
//// TODO {
//// TODO     ERHE_PROFILE_FUNCTION();
//// TODO 
//// TODO     if (context.scene_view == nullptr) {
//// TODO         return;
//// TODO     }
//// TODO     const auto& scene_root   = context.scene_view->get_scene_root();
//// TODO     if (
//// TODO         (g_debug_draw == nullptr) ||
//// TODO         !m_debug_draw.enable ||
//// TODO         !scene_root
//// TODO     ) {
//// TODO         return;
//// TODO     }
//// TODO 
//// TODO     int flags = 0;
//// TODO     if (m_debug_draw.wireframe        ) flags |= erhe::physics::IDebug_draw::c_Draw_wireframe;
//// TODO     if (m_debug_draw.aabb             ) flags |= erhe::physics::IDebug_draw::c_Draw_aabb;
//// TODO     if (m_debug_draw.contact_points   ) flags |= erhe::physics::IDebug_draw::c_Draw_contact_points;
//// TODO     if (m_debug_draw.no_deactivation  ) flags |= erhe::physics::IDebug_draw::c_No_deactivation;
//// TODO     if (m_debug_draw.constraints      ) flags |= erhe::physics::IDebug_draw::c_Draw_constraints;
//// TODO     if (m_debug_draw.constraint_limits) flags |= erhe::physics::IDebug_draw::c_Draw_constraint_limits;
//// TODO     if (m_debug_draw.normals          ) flags |= erhe::physics::IDebug_draw::c_Draw_normals;
//// TODO     if (m_debug_draw.frames           ) flags |= erhe::physics::IDebug_draw::c_Draw_frames;
//// TODO     g_debug_draw->set_debug_mode(flags);
//// TODO 
//// TODO     g_debug_draw->set_colors(m_debug_draw.colors);
//// TODO 
//// TODO     scene_root->physics_world().debug_draw();
//// TODO }

} // namespace editor
