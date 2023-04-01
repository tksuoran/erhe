#include "windows/viewport_config_window.hpp"

#include "renderers/primitive_buffer.hpp"
#include "tools/hotbar.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Viewport_config_window* g_viewport_config_window{nullptr};

Viewport_config_window::Viewport_config_window()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
{
    config.render_style_not_selected.line_color = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    config.render_style_not_selected.edge_lines = false;

    config.render_style_selected.edge_lines = false;
}

Viewport_config_window::~Viewport_config_window() = default;

void Viewport_config_window::deinitialize_component()
{
    ERHE_VERIFY(g_viewport_config_window == this);
    g_viewport_config_window = nullptr;
}

void Viewport_config_window::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::application::Imgui_windows>();
}

void Viewport_config_window::initialize_component()
{
    ERHE_VERIFY(g_viewport_config_window == nullptr);

    erhe::application::g_imgui_windows->register_imgui_window(this, "viewport_config");

    auto ini = erhe::application::get_ini("erhe.ini", "viewport");
    ini->get("polygon_fill",              polygon_fill);
    ini->get("edge_lines",                edge_lines);
    ini->get("edge_color",                edge_color);
    ini->get("selection_polygon_fill",    selection_polygon_fill);
    ini->get("selection_edge_lines",      selection_edge_lines);
    ini->get("corner_points",             corner_points);
    ini->get("polygon_centroids",         polygon_centroids);
    ini->get("selection_bounding_box",    selection_bounding_box);
    ini->get("selection_bounding_sphere", selection_bounding_sphere);
    ini->get("selection_edge_color",      selection_edge_color);
    ini->get("clear_color",               clear_color);

    config.render_style_not_selected.polygon_fill      = polygon_fill;
    config.render_style_not_selected.edge_lines        = edge_lines;
    config.render_style_not_selected.corner_points     = corner_points;
    config.render_style_not_selected.polygon_centroids = polygon_centroids;
    config.render_style_not_selected.line_color        = edge_color;
    config.render_style_not_selected.corner_color      = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f};
    config.render_style_not_selected.centroid_color    = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};

    config.render_style_selected.polygon_fill      = selection_polygon_fill;
    config.render_style_selected.edge_lines        = selection_edge_lines;
    config.render_style_selected.corner_points     = corner_points;
    config.render_style_selected.polygon_centroids = polygon_centroids;
    config.render_style_selected.line_color        = selection_edge_color;
    config.render_style_selected.corner_color      = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f};
    config.render_style_selected.centroid_color    = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};
    //data.render_style_selected.edge_lines = false;

    config.selection_bounding_box    = selection_bounding_box;
    config.selection_bounding_sphere = selection_bounding_sphere;
    config.clear_color               = clear_color;
    g_viewport_config_window = this;
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

    if (ImGui::TreeNodeEx("Edge Lines", flags)) {
        ImGui::Checkbox("Visible", &render_style.edge_lines);
        if (render_style.edge_lines) {
            ImGui::SliderFloat("Width",          &render_style.line_width, 0.0f, 20.0f);
            ImGui::ColorEdit4 ("Constant Color", &render_style.line_color.x,     ImGuiColorEditFlags_Float);
            erhe::application::make_combo(
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
            erhe::application::make_combo(
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
        erhe::application::make_combo(
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

void Viewport_config_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_Framed            |
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    if (edit_data != nullptr) {
        ImGui::ColorEdit4("Clear Color", &edit_data->clear_color.x, ImGuiColorEditFlags_Float);

        if (ImGui::TreeNodeEx("Default Style", flags)) {
            render_style_ui(edit_data->render_style_not_selected);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Selection", flags)) {
            render_style_ui(edit_data->render_style_selected);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Debug Visualizations", flags)) {
            erhe::application::make_combo("Light",  edit_data->debug_visualizations.light,  c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
            erhe::application::make_combo("Camera", edit_data->debug_visualizations.camera, c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
            ImGui::TreePop();
        }
    }

    ImGui::Separator();

    ImGui::SliderFloat("LoD Bias", &rendertarget_mesh_lod_bias, -8.0f, 8.0f);

    if (g_hotbar != nullptr) {
        if (ImGui::TreeNodeEx("Hotbar", flags)) {
            auto& color_inactive = g_hotbar->get_color(0);
            auto& color_hover    = g_hotbar->get_color(1);
            auto& color_active   = g_hotbar->get_color(2);
            ImGui::ColorEdit4("Inactive", &color_inactive.x, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit4("Hover",    &color_hover.x,    ImGuiColorEditFlags_Float);
            ImGui::ColorEdit4("Active",   &color_active.x,   ImGuiColorEditFlags_Float);

            auto position = g_hotbar->get_position();
            if (ImGui::DragFloat3("Position", &position.x, 0.1f)) {
                g_hotbar->set_position(position);
            }
            ImGui::TreePop();

            bool locked = g_hotbar->get_locked();
            if (ImGui::Checkbox("Locked", &locked)) {
                g_hotbar->set_locked(locked);
            }
        }
    }
#endif
}

} // namespace editor
