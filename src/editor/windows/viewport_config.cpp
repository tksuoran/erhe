#include "windows/viewport_config.hpp"
#include "tools.hpp"
#include "scene/scene_manager.hpp"

#include "imgui.h"

namespace editor
{

Viewport_config::Viewport_config()
    : erhe::components::Component{c_name}
{
    render_style_not_selected.line_color = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
}

Viewport_config::~Viewport_config() = default;

void Viewport_config::initialize_component()
{
    get<Editor_tools>()->register_window(this);
}

void Viewport_config::render_style_ui(Render_style& render_style)
{
    if (ImGui::CollapsingHeader("Polygon Fill"))
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
    }

    if (ImGui::CollapsingHeader("Edge Lines"))
    {
        ImGui::Checkbox("Visible", &render_style.edge_lines);
        if (render_style.edge_lines)
        {
            ImGui::SliderFloat("Width",          &render_style.line_width, 0.0f, 20.0f);
            ImGui::ColorEdit4 ("Constant Color", &render_style.line_color.x,     ImGuiColorEditFlags_Float);
            make_combo("Color Source", render_style.edge_lines_color_source, Base_renderer::c_primitive_color_source_strings, IM_ARRAYSIZE(Base_renderer::c_primitive_color_source_strings));
        }
    }

    if (ImGui::CollapsingHeader("Polygon Centroids"))
    {
        ImGui::Checkbox("Visible", &render_style.polygon_centroids);
        if (render_style.polygon_centroids)
        {
            ImGui::ColorEdit4("Constant Color", &render_style.centroid_color.x, ImGuiColorEditFlags_Float);
            make_combo("Color Source", render_style.polygon_centroids_color_source, Base_renderer::c_primitive_color_source_strings, IM_ARRAYSIZE(Base_renderer::c_primitive_color_source_strings));
        }
    }

    if (ImGui::CollapsingHeader("Corner Points"))
    {
        ImGui::Checkbox  ("Visible",        &render_style.corner_points);
        ImGui::ColorEdit4("Constant Color", &render_style.corner_color.x,   ImGuiColorEditFlags_Float);
        make_combo("Color Source", render_style.corner_points_color_source, Base_renderer::c_primitive_color_source_strings, IM_ARRAYSIZE(Base_renderer::c_primitive_color_source_strings));
    }

    if (render_style.polygon_centroids || render_style.corner_points)
    {
        ImGui::SliderFloat("Point Size", &render_style.point_size, 0.0f, 20.0f);
    }

}

void Viewport_config::window(Pointer_context&)
{
    ImGui::Begin("Viewport");

    ImGui::ColorEdit4("Clear Color", &clear_color.x, ImGuiColorEditFlags_Float);

    if (ImGui::CollapsingHeader("Default Style"))
    {
        render_style_ui(render_style_not_selected);
    }

    if (ImGui::CollapsingHeader("Selection"))
    {
        render_style_ui(render_style_selected);
    }
    ImGui::End();
}

}
