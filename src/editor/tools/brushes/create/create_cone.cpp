#include "tools/brushes/create/create_cone.hpp"

#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "tools/brushes/brush.hpp"

#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/physics/icollision_shape.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

void Create_cone::render_preview(
    const Create_preview_settings& preview_settings
)
{
    const Render_context& render_context = preview_settings.render_context;
    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    auto& line_renderer = *erhe::application::g_line_renderer_set->hidden.at(2).get();
    line_renderer.add_cone(
        preview_settings.transform,
        preview_settings.major_color,
        preview_settings.minor_color,
        preview_settings.major_thickness,
        preview_settings.minor_thickness,
        glm::vec3{0.0f, 0.0f, 0.0f},
        m_height,
        m_bottom_radius,
        m_top_radius,
        camera_node->position_in_world(),
        preview_settings.ideal_shape
            ? std::max(80, m_slice_count)
            : m_slice_count
    );
}

void Create_cone::imgui()
{
    ImGui::Text("Cone Parameters");

    ImGui::SliderFloat("Height",        &m_height ,       0.0f, 3.0f);
    ImGui::SliderFloat("Bottom Radius", &m_bottom_radius, 0.0f, 3.0f);
    ImGui::SliderFloat("Top Radius",    &m_top_radius,    0.0f, 3.0f);
    ImGui::SliderInt  ("Slices",        &m_slice_count,   1, 40);
    ImGui::SliderInt  ("Stacks",        &m_stack_count,   1, 6);
    ImGui::Checkbox   ("Use Top",       &m_use_top);
    ImGui::Checkbox   ("Use Bottom",    &m_use_bottom);
}

[[nodiscard]] auto Create_cone::create(
    Brush_data& brush_create_info
) const -> std::shared_ptr<Brush>
{
    brush_create_info.geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_conical_frustum(
            0.0f,
            m_height,
            m_bottom_radius,
            m_top_radius,
            m_use_bottom,
            m_use_top,
            std::max(3, m_slice_count), // slice count
            std::max(1, m_stack_count)  // stack count
        )
    );

    brush_create_info.geometry->transform(erhe::toolkit::mat4_swap_xy);
    brush_create_info.geometry->build_edges();
    brush_create_info.geometry->compute_polygon_normals();
    brush_create_info.geometry->compute_tangents();
    brush_create_info.geometry->compute_polygon_centroids();
    brush_create_info.geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}


} // namespace editor
