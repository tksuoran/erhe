#include "windows/viewport_config_window.hpp"
#include "app_context.hpp"
#include "config/generated/viewport_config_serialization.hpp"
#include "scene/viewport_scene_views.hpp"
#include "scene/viewport_scene_view.hpp"
#include "erhe_codegen/config_io.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

namespace editor {

void Viewport_config_window::render_style_ui(Render_style_data& render_style, bool& edited)
{
    ERHE_PROFILE_FUNCTION();

    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    if (ImGui::TreeNodeEx("Polygon Fill", flags)) {
        ImGui::Checkbox("Visible", &render_style.polygon_fill);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            edited = true;
        }

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
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            edited = true;
        }
        if (render_style.edge_lines) {
            ImGui::SliderFloat("Width", &render_style.line_width, 0.0f, 20.0f);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edited = true;
            }

            ImGui::ColorEdit4("Constant Color", &render_style.line_color.x, ImGuiColorEditFlags_Float);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edited = true;
            }

            erhe::imgui::make_combo(
                "Color Source",
                render_style.edge_lines_color_source,
                Primitive_interface_settings::c_primitive_color_source_strings_data.data(),
                static_cast<int>(Primitive_interface_settings::c_primitive_color_source_strings_data.size())
            );
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edited = true;
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Polygon Centroids", flags)) {
        ImGui::Checkbox("Visible", &render_style.polygon_centroids);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            edited = true;
        }

        if (render_style.polygon_centroids) {
            ImGui::ColorEdit4("Constant Color", &render_style.centroid_color.x, ImGuiColorEditFlags_Float);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edited = true;
            }

            erhe::imgui::make_combo(
                "Color Source",
                render_style.polygon_centroids_color_source,
                Primitive_interface_settings::c_primitive_color_source_strings_data.data(),
                static_cast<int>(Primitive_interface_settings::c_primitive_color_source_strings_data.size())
            );
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edited = true;
            }

        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Corner Points", flags)) {
        ImGui::Checkbox("Visible", &render_style.corner_points);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            edited = true;
        }

        ImGui::ColorEdit4("Constant Color", &render_style.corner_color.x, ImGuiColorEditFlags_Float);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            edited = true;
        }

        erhe::imgui::make_combo(
            "Color Source",
            render_style.corner_points_color_source,
            Primitive_interface_settings::c_primitive_color_source_strings_data.data(),
            static_cast<int>(Primitive_interface_settings::c_primitive_color_source_strings_data.size())
        );
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            edited = true;
        }

        ImGui::TreePop();
    }

    if (render_style.polygon_centroids || render_style.corner_points) {
        ImGui::SliderFloat("Point Size", &render_style.point_size, 0.0f, 20.0f);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            edited = true;
        }
    }
}

void Viewport_config_window::imgui(Viewport_config& edit_data)
{
    ERHE_PROFILE_FUNCTION();


    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_Framed            |
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth     |
        ImGuiTreeNodeFlags_DefaultOpen
    };

    const std::string before = serialize(edit_data);

    bool edited = false;
    auto collect = [&](const bool value) {
        static_cast<void>(value);
        edited = edited || ImGui::IsItemDeactivatedAfterEdit();
    };
    collect(ImGui::SliderFloat("Gizmo Scale", &edit_data.gizmo_scale, 1.0f, 20.0f, "%.2f"));
    collect(ImGui::ColorEdit4("Clear Color", &edit_data.clear_color.x, ImGuiColorEditFlags_Float));

    if (ImGui::TreeNodeEx("Default Style", flags)) {
        render_style_ui(edit_data.render_style_not_selected, edited);
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Selection", flags)) {
        render_style_ui(edit_data.render_style_selected, edited);
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Selection Outline", flags)) {
        collect(ImGui::ColorEdit4 ("Color Low",  &edit_data.selection_highlight_low.x,  ImGuiColorEditFlags_Float));
        collect(ImGui::ColorEdit4 ("Color High", &edit_data.selection_highlight_high.x, ImGuiColorEditFlags_Float));
        collect(ImGui::SliderFloat("Width Low",  &edit_data.selection_highlight_width_low,  -20.0f, 0.0f, "%.2f"));
        collect(ImGui::SliderFloat("Width High", &edit_data.selection_highlight_width_high, -20.0f, 0.0f, "%.2f"));
        collect(ImGui::SliderFloat("Frequency",  &edit_data.selection_highlight_frequency,    0.0f, 1.0f, "%.2f"));
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Debug Visualizations", flags)) {
        collect(erhe::imgui::make_combo("Light",  edit_data.debug_visualizations.light,  c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings)));
        collect(erhe::imgui::make_combo("Camera", edit_data.debug_visualizations.camera, c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings)));
        ImGui::TreePop();
    }

    if (edited) {
        const std::string after = serialize(edit_data);
        if (before != after) {
            // TODO Path needs to be scene view specific - add path to edit_data
            erhe::codegen::save_config(edit_data, "config/editor/default_viewport_config.json");
        }
    }
}

}
