#include "windows/viewport_config_window.hpp"
#include "app_context.hpp"
#include "scene/viewport_scene_views.hpp"
#include "scene/viewport_scene_view.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
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

void Viewport_config_window::imgui(Viewport_config& edit_data)
{
    ERHE_PROFILE_FUNCTION();

    // This popup holds only per-Scene_view render-style visibility toggles.
    // Editor-global visual style lives in the Settings window: render-style
    // appearance, selection outline, mesh component style, gizmo scale, clear
    // color, and the content edge-line (wide-line) method + bias.

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

    // Debug Visualizations have their own dedicated scene-view popup (the
    // eye-settings toolbar button, Debug_visualizations::imgui) - not shown here.

    // Mesh Component Style (vertex / edge / face selection colors and sizes) is
    // editor-global (Editor_settings_config.mesh_component_style), edited in the
    // Settings window - no longer per scene view here.
}

}
