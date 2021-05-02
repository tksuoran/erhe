#include "windows/viewport_config.hpp"
#include "scene/scene_manager.hpp"

#include "imgui.h"

namespace editor
{

Viewport_config::Viewport_config()
{
}

void Viewport_config::window(Pointer_context&)
{
    ImGui::Begin("Viewport");

    ImGui::Checkbox("Polygon Fill",      &polygon_fill);
    ImGui::Checkbox("Edge Lines",        &edge_lines);
    ImGui::Checkbox("Polygon Centroids", &polygon_centroids);
    ImGui::Checkbox("Corner Points",     &corner_points);

    ImGui::Separator();

    ImGui::Text       ("Polygon Offset");
    ImGui::Checkbox   ("Enable", &polygon_offset_enable);
    ImGui::SliderFloat("Factor", &polygon_offset_factor, -2.0f, 2.0f);
    ImGui::SliderFloat("Units",  &polygon_offset_units,  -2.0f, 2.0f);
    ImGui::SliderFloat("clamp",  &polygon_offset_clamp,  -0.01f, 0.01f);

    ImGui::Separator();

    ImGui::SliderFloat("Point Size", &point_size, 0.0f, 20.0f);
    ImGui::SliderFloat("Lide Width", &line_width, 0.0f, 20.0f);

    ImGui::ColorEdit4 ("Line Color",     &line_color.x,     ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Corner Color",   &corner_color.x,   ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Centroid Color", &centroid_color.x, ImGuiColorEditFlags_Float);

    ImGui::Separator();
    ImGui::ColorEdit4 ("Clear Color", &clear_color.x, ImGuiColorEditFlags_Float);

    ImGui::End();
}

}
