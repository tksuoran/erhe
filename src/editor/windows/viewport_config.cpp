#include "windows/viewport_config.hpp"
#include "application.hpp"
#include "configuration.hpp"
#include "tools.hpp"
#include "scene/scene_manager.hpp"

#include "erhe/imgui/imgui_helpers.hpp"

#include <imgui.h>

namespace editor
{

Render_style::Render_style() = default;

Render_style::Render_style(const Configuration& configuration)
    : polygon_offset_factor{configuration.reverse_depth ? -1.0000f : 1.0000f}
    , polygon_offset_units {configuration.reverse_depth ? -1.0000f : 1.0000f}
    , polygon_offset_clamp {configuration.reverse_depth ? -0.0001f : 0.0001f}
{
}

Viewport_config::Viewport_config()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Viewport_config::~Viewport_config() = default;

void Viewport_config::connect()
{
    require<Configuration>();
}

void Viewport_config::initialize_component()
{
    render_style_not_selected            = Render_style(*get<Configuration>());
    render_style_not_selected.line_color = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    render_style_not_selected.edge_lines = false;

    render_style_selected            = Render_style(*get<Configuration>());
    render_style_selected.edge_lines = true;

    get<Editor_tools>()->register_imgui_window(this);
}

void Viewport_config::render_style_ui(Render_style& render_style)
{
    using namespace erhe::imgui;

    //const ImGuiTreeNodeFlags flags{
    //    ImGuiTreeNodeFlags_SpanFullWidth    |
    //    ImGuiTreeNodeFlags_NoTreePushOnOpen |
    //    ImGuiTreeNodeFlags_Leaf
    //};
    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    if (ImGui::TreeNodeEx("Polygon Fill", flags))
    //if (ImGui::CollapsingHeader("Polygon Fill"))
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

    //if (ImGui::CollapsingHeader("Edge Lines"))
    if (ImGui::TreeNodeEx("Edge Lines", flags))
    {
        ImGui::Checkbox("Visible", &render_style.edge_lines);
        if (render_style.edge_lines)
        {
            ImGui::SliderFloat("Width",          &render_style.line_width, 0.0f, 20.0f);
            ImGui::ColorEdit4 ("Constant Color", &render_style.line_color.x,     ImGuiColorEditFlags_Float);
            make_combo(
                "Color Source",
                render_style.edge_lines_color_source,
                Base_renderer::c_primitive_color_source_strings_data.data(),
                static_cast<int>(Base_renderer::c_primitive_color_source_strings_data.size())
            );
        }
        ImGui::TreePop();
    }

    //if (ImGui::CollapsingHeader("Polygon Centroids"))
    if (ImGui::TreeNodeEx("Polygon Centroids", flags))
    {
        ImGui::Checkbox("Visible", &render_style.polygon_centroids);
        if (render_style.polygon_centroids)
        {
            ImGui::ColorEdit4("Constant Color", &render_style.centroid_color.x, ImGuiColorEditFlags_Float);
            make_combo(
                "Color Source",
                render_style.polygon_centroids_color_source,
                Base_renderer::c_primitive_color_source_strings_data.data(),
                static_cast<int>(Base_renderer::c_primitive_color_source_strings_data.size())
            );
        }
        ImGui::TreePop();
    }

    //if (ImGui::CollapsingHeader("Corner Points"))
    if (ImGui::TreeNodeEx("Corner Points", flags))
    {
        ImGui::Checkbox  ("Visible",        &render_style.corner_points);
        ImGui::ColorEdit4("Constant Color", &render_style.corner_color.x,   ImGuiColorEditFlags_Float);
        erhe::imgui::make_combo(
            "Color Source",
            render_style.corner_points_color_source,
            Base_renderer::c_primitive_color_source_strings_data.data(),
            static_cast<int>(Base_renderer::c_primitive_color_source_strings_data.size())
        );
        ImGui::TreePop();
    }

    if (render_style.polygon_centroids || render_style.corner_points)
    {
        ImGui::SliderFloat("Point Size", &render_style.point_size, 0.0f, 20.0f);
    }

}

void Viewport_config::imgui(Pointer_context&)
{
    ImGui::Begin("Viewport");

    ImGui::ColorEdit4("Clear Color", &clear_color.x, ImGuiColorEditFlags_Float);

    const ImGuiTreeNodeFlags parent_flags{
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    if (ImGui::TreeNodeEx("Default Style", parent_flags))
    {
        //ImGui::PushID("default style");
        render_style_ui(render_style_not_selected);
        //ImGui::PopID();
        ImGui::TreePop();
    }

    //if (ImGui::CollapsingHeader("Selection"))
    if (ImGui::TreeNodeEx("Selection", parent_flags))
    {
        //ImGui::PushID("selection");
        render_style_ui(render_style_selected);
        //ImGui::PopID();
        ImGui::TreePop();
    }
    ImGui::End();
}

}
