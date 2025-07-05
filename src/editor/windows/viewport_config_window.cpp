#include "windows/viewport_config_window.hpp"
#include "app_context.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

namespace editor {

Viewport_config_window::Viewport_config_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Viewport config", "viewport_config"}
    , m_context                {app_context}
{
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Viewport_config_window::render_style_ui(Render_style_data& render_style)
{
    ERHE_PROFILE_FUNCTION();

    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    if (ImGui::TreeNodeEx("Polygon Fill", flags)) {
        ImGui::Checkbox("Visible", &render_style.polygon_fill);
        //if (render_style.polygon_fill) {
        //    ImGui::Text       ("Polygon Offset");
        //    ImGui::Checkbox   ("Enable", &render_style.polygon_offset_enable);
        //    ImGui::SliderFloat("Factor", &render_style.polygon_offset_factor, -2.0f, 2.0f);
        //    ImGui::SliderFloat("Units",  &render_style.polygon_offset_units,  -2.0f, 2.0f);
        //    ImGui::SliderFloat("clamp",  &render_style.polygon_offset_clamp,  -0.01f, 0.01f);
        //}
        ImGui::TreePop();
    }

    using Primitive_interface_settings = erhe::scene_renderer::Primitive_interface_settings;
    if (ImGui::TreeNodeEx("Edge Lines", flags)) {
        ImGui::Checkbox("Visible", &render_style.edge_lines);
        if (render_style.edge_lines) {
            ImGui::SliderFloat("Width",          &render_style.line_width, 0.0f, 20.0f);
            ImGui::ColorEdit4 ("Constant Color", &render_style.line_color.x,     ImGuiColorEditFlags_Float);
            erhe::imgui::make_combo(
                "Color Source",
                render_style.edge_lines_color_source,
                Primitive_interface_settings::c_primitive_color_source_strings_data.data(),
                static_cast<int>(Primitive_interface_settings::c_primitive_color_source_strings_data.size())
            );
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Polygon Centroids", flags)) {
        ImGui::Checkbox("Visible", &render_style.polygon_centroids);
        if (render_style.polygon_centroids) {
            ImGui::ColorEdit4("Constant Color", &render_style.centroid_color.x, ImGuiColorEditFlags_Float);
            erhe::imgui::make_combo(
                "Color Source",
                render_style.polygon_centroids_color_source,
                Primitive_interface_settings::c_primitive_color_source_strings_data.data(),
                static_cast<int>(Primitive_interface_settings::c_primitive_color_source_strings_data.size())
            );
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Corner Points", flags)) {
        ImGui::Checkbox  ("Visible",        &render_style.corner_points);
        ImGui::ColorEdit4("Constant Color", &render_style.corner_color.x,   ImGuiColorEditFlags_Float);
        erhe::imgui::make_combo(
            "Color Source",
            render_style.corner_points_color_source,
            Primitive_interface_settings::c_primitive_color_source_strings_data.data(),
            static_cast<int>(Primitive_interface_settings::c_primitive_color_source_strings_data.size())
        );
        ImGui::TreePop();
    }

    if (render_style.polygon_centroids || render_style.corner_points) {
        ImGui::SliderFloat("Point Size", &render_style.point_size, 0.0f, 20.0f);
    }
}
#endif

void Viewport_config_window::set_edit_data(Viewport_config* edit_data)
{
    m_edit_data = edit_data;
}

void Viewport_config_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_Framed            |
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth     |
        ImGuiTreeNodeFlags_DefaultOpen
    };

    if (m_edit_data != nullptr) {
        ImGui::SliderFloat("Gizmo Scale", &m_edit_data->gizmo_scale, 1.0f, 8.0f, "%.2f");
        ImGui::ColorEdit4("Clear Color", &m_edit_data->clear_color.x, ImGuiColorEditFlags_Float);

        if (ImGui::TreeNodeEx("Default Style", flags)) {
            render_style_ui(m_edit_data->render_style_not_selected);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Selection", flags)) {
            render_style_ui(m_edit_data->render_style_selected);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Selection Outline", flags)) {
            ImGui::ColorEdit4 ("Color Low",  &m_edit_data->selection_highlight_low.x,  ImGuiColorEditFlags_Float);
            ImGui::ColorEdit4 ("Color High", &m_edit_data->selection_highlight_high.x, ImGuiColorEditFlags_Float);
            ImGui::SliderFloat("Width Low",  &m_edit_data->selection_highlight_width_low,  -20.0f, 0.0f, "%.2f");
            ImGui::SliderFloat("Width High", &m_edit_data->selection_highlight_width_high, -20.0f, 0.0f, "%.2f");
            ImGui::SliderFloat("Frequency",  &m_edit_data->selection_highlight_frequency,    0.0f, 1.0f, "%.2f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Debug Visualizations", flags)) {
            erhe::imgui::make_combo("Light",  m_edit_data->debug_visualizations.light,  c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
            erhe::imgui::make_combo("Camera", m_edit_data->debug_visualizations.camera, c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
            ImGui::TreePop();
        }
    }
#endif
}

} // namespace editor
