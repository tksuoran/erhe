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

    // Widgets edit render_style in place; the owning Scene_view's collect
    // callback persists the change (autosaved). No per-widget edit tracking.
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
            ImGui::SliderFloat("Width", &render_style.line_width, 0.0f, 20.0f);
            ImGui::ColorEdit4("Constant Color", &render_style.line_color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
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
            ImGui::ColorEdit4("Constant Color", &render_style.centroid_color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
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
        ImGui::Checkbox("Visible", &render_style.corner_points);
        ImGui::ColorEdit4("Constant Color", &render_style.corner_color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
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

    if (ImGui::TreeNodeEx("Selection Outline", flags)) {
        ImGui::ColorEdit4 ("Color Low",  &edit_data.selection_highlight_low.x,  ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4 ("Color High", &edit_data.selection_highlight_high.x, ImGuiColorEditFlags_Float);
        ImGui::SliderFloat("Width Low",  &edit_data.selection_highlight_width_low,  -20.0f, 0.0f, "%.2f");
        ImGui::SliderFloat("Width High", &edit_data.selection_highlight_width_high, -20.0f, 0.0f, "%.2f");
        ImGui::SliderFloat("Frequency",  &edit_data.selection_highlight_frequency,    0.0f, 1.0f, "%.2f");
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Debug Visualizations", flags)) {
        erhe::imgui::make_combo("Light",  edit_data.debug_visualizations.light,  c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
        erhe::imgui::make_combo("Camera", edit_data.debug_visualizations.camera, c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Mesh Components", flags)) {
        Mesh_component_style& mesh_component_style = edit_data.mesh_component_style;
        ImGui::ColorEdit4("Vertex Color", &mesh_component_style.vertex_color.x, ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4("Edge Color",   &mesh_component_style.edge_color.x,   ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4("Face Color",   &mesh_component_style.face_color.x,   ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4("Hover Color",  &mesh_component_style.hover_color.x,  ImGuiColorEditFlags_Float);
        // edge_thickness: negative = constant screen-space pixel width.
        ImGui::DragFloat("Edge Thickness", &mesh_component_style.edge_thickness, 0.1f,   -20.0f, -0.5f);
        ImGui::DragFloat("Vertex Size",    &mesh_component_style.vertex_size,    0.001f,  0.001f, 0.1f);
        // edge_depth_bias: surface-line bias headroom in depth ULPs.
        ImGui::DragFloat("Edge Depth Bias (ULPs)", &mesh_component_style.edge_depth_bias, 1.0f, 0.0f, 4096.0f, "%.0f");
        ImGui::TreePop();
    }
}

}
