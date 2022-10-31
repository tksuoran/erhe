#include "windows/viewport_config.hpp"
#include "renderers/primitive_buffer.hpp"
#include "tools/hotbar.hpp"

#include "erhe/application/application.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Viewport_config::Viewport_config()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
{
    render_style_not_selected.line_color = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    render_style_not_selected.edge_lines = false;

    render_style_selected.edge_lines = false;
}

void Viewport_config::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::application::Imgui_windows>();
}

void Viewport_config::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
    const auto& config = get<erhe::application::Configuration>()->viewport;
    render_style_not_selected.polygon_fill      = config.polygon_fill;
    render_style_not_selected.edge_lines        = config.edge_lines;
    render_style_not_selected.corner_points     = config.corner_points;
    render_style_not_selected.polygon_centroids = config.polygon_centroids;
    render_style_not_selected.line_color        = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    render_style_not_selected.corner_color      = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f};
    render_style_not_selected.centroid_color    = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};

    render_style_selected.polygon_fill      = config.selection_polygon_fill;
    render_style_selected.edge_lines        = config.selection_edge_lines;
    render_style_selected.corner_points     = config.corner_points;
    render_style_selected.polygon_centroids = config.polygon_centroids;
    render_style_selected.line_color        = config.selection_edge_color;
    render_style_selected.corner_color      = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f};
    render_style_selected.centroid_color    = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};
    //render_style_selected.edge_lines = false;

    selection_bounding_box    = config.selection_bounding_box;
    selection_bounding_sphere = config.selection_bounding_sphere;
    clear_color               = config.clear_color;
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Viewport_config::render_style_ui(Render_style& render_style)
{
    ERHE_PROFILE_FUNCTION

    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    if (ImGui::TreeNodeEx("Polygon Fill", flags))
    {
        ImGui::Checkbox("Visible", &render_style.polygon_fill);
        if (render_style.polygon_fill)
        {
            ImGui::Text       ("Polygon Offset");
            ImGui::Checkbox   ("Enable", &render_style.polygon_offset_enable);
            ImGui::SliderFloat("Factor", &render_style.polygon_offset_factor, -2.0f, 2.0f);
            ImGui::SliderFloat("Units",  &render_style.polygon_offset_units,  -2.0f, 2.0f);
            ImGui::SliderFloat("clamp",  &render_style.polygon_offset_clamp,  -0.01f, 0.01f);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Edge Lines", flags))
    {
        ImGui::Checkbox("Visible", &render_style.edge_lines);
        if (render_style.edge_lines)
        {
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

    if (ImGui::TreeNodeEx("Polygon Centroids", flags))
    {
        ImGui::Checkbox("Visible", &render_style.polygon_centroids);
        if (render_style.polygon_centroids)
        {
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

    if (ImGui::TreeNodeEx("Corner Points", flags))
    {
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

    if (render_style.polygon_centroids || render_style.corner_points)
    {
        ImGui::SliderFloat("Point Size", &render_style.point_size, 0.0f, 20.0f);
    }
}
#endif

void Viewport_config::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    ImGui::ColorEdit4("Clear Color", &clear_color.x, ImGuiColorEditFlags_Float);
    ImGui::Checkbox  ("Post Processing", &post_processing_enable);

    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_Framed            |
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    if (ImGui::TreeNodeEx("Default Style", flags))
    {
        render_style_ui(render_style_not_selected);
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Selection", flags))
    {
        render_style_ui(render_style_selected);
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Debug Visualizations", flags))
    {
        erhe::application::make_combo("Light",  debug_visualizations.light,  c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
        erhe::application::make_combo("Camera", debug_visualizations.camera, c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
        ImGui::TreePop();
    }
    ImGui::SliderFloat("LoD Bias", &rendertarget_node_lod_bias, -8.0f, 8.0f);

    const auto& hotbar = get<Hotbar>();
    if (hotbar)
    {
        if (ImGui::TreeNodeEx("Hotbar", flags))
        {
            auto& color_inactive = hotbar->get_color(0);
            auto& color_hover    = hotbar->get_color(1);
            auto& color_active   = hotbar->get_color(2);
            ImGui::ColorEdit4("Inactive", &color_inactive.x, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit4("Hover",    &color_hover.x,    ImGuiColorEditFlags_Float);
            ImGui::ColorEdit4("Active",   &color_active.x,   ImGuiColorEditFlags_Float);
            ImGui::TreePop();
        }
    }
#endif
}

} // namespace editor
