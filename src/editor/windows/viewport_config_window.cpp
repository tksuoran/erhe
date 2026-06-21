#include "windows/viewport_config_window.hpp"
#include "app_context.hpp"
#include "scene/viewport_scene_views.hpp"
#include "scene/viewport_scene_view.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

namespace editor {

void Viewport_config_window::render_style_ui(Render_style_data& render_style)
{
    ERHE_PROFILE_FUNCTION();

    // Per-Scene_view visibility toggles only: which primitives are drawn. The
    // appearance of these primitives (colors, line / point widths, point size,
    // color sources) is editor-global and edited in the Settings window
    // (Render_style_appearance). Widgets edit render_style in place; the owning
    // Scene_view's collect callback persists the change (autosaved).
    ImGui::Checkbox("Polygon Fill",      &render_style.polygon_fill);
    ImGui::Checkbox("Edge Lines",        &render_style.edge_lines);
    ImGui::Checkbox("Solid Wireframe",   &render_style.solid_wireframe);
    ImGui::Checkbox("Polygon Centroids", &render_style.polygon_centroids);
    ImGui::Checkbox("Corner Points",     &render_style.corner_points);
}

void Viewport_config_window::imgui(App_context& context, Viewport_config& edit_data)
{
    ERHE_PROFILE_FUNCTION();

    // Content wide-line method selector. The simple quad and the two-face
    // surface tent each win in different cases (the tent hugs sharp creases but
    // can poke through overlapping thin geometry; the simple quad is flatter but
    // more predictable), so both are kept and chosen at runtime here. Global
    // runtime state on the content wide-line renderer (not persisted config).
    erhe::scene_renderer::Content_wide_line_renderer* wide_line_renderer = context.content_wide_line_renderer;
    if (wide_line_renderer != nullptr) {
        bool use_tent = wide_line_renderer->get_use_tent();
        if (ImGui::Checkbox("Edge Lines: Surface Tent", &use_tent)) {
            wide_line_renderer->set_use_tent(use_tent);
        }
        if (use_tent) {
            float bias = wide_line_renderer->get_line_bias_margin();
            if (ImGui::DragFloat("Edge Lines: Tent Bias (ULPs)", &bias, 1.0f, 0.0f, 4096.0f, "%.0f")) {
                wide_line_renderer->set_line_bias_margin(bias);
            }
            float clamp = wide_line_renderer->get_line_bias_clamp();
            if (ImGui::DragFloat("Edge Lines: Tent Bias Clamp (ULPs)", &clamp, 16.0f, 0.0f, 65536.0f, "%.0f")) {
                wide_line_renderer->set_line_bias_clamp(clamp);
            }
        }
    }

    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_Framed            |
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth     |
        ImGuiTreeNodeFlags_DefaultOpen
    };

    // edit_data aliases the owning Scene_view's m_viewport_config. Widgets
    // mutate it in place; the view's collect callback writes it to
    // editor_settings.json, which Editor_settings_store autosaves on change.
    ImGui::SliderFloat("Gizmo Scale", &edit_data.gizmo_scale, 1.0f, 20.0f, "%.2f");
    ImGui::ColorEdit4("Clear Color", &edit_data.clear_color.x, ImGuiColorEditFlags_Float);

    if (ImGui::TreeNodeEx("Default Style", flags)) {
        render_style_ui(edit_data.render_style_not_selected);
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Selection", flags)) {
        render_style_ui(edit_data.render_style_selected);
        ImGui::TreePop();
    }

    // Selection Outline appearance is editor-global (Selection_outline_style),
    // edited in the Settings window - no longer per scene view here.

    if (ImGui::TreeNodeEx("Debug Visualizations", flags)) {
        erhe::imgui::make_combo("Light",  edit_data.debug_visualizations.light,  c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
        erhe::imgui::make_combo("Camera", edit_data.debug_visualizations.camera, c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
        ImGui::TreePop();
    }

    // Mesh Component Style (vertex / edge / face selection colors and sizes) is
    // editor-global (Editor_settings_config.mesh_component_style), edited in the
    // Settings window - no longer per scene view here.
}

}
