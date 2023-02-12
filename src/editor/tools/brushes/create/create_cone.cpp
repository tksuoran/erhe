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
    const Render_context&         render_context,
    const erhe::scene::Transform& transform
)
{
    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr)
    {
        return;
    }

    auto& line_renderer = *erhe::application::g_line_renderer_set->hidden.at(2).get();
    const glm::vec4 major_color    {1.0f, 1.0f, 1.0f, 1.0f};
    const glm::vec4 minor_color    {1.0f, 1.0f, 1.0f, 0.5f};
    const float     major_thickness{6.0f};
    const float     minor_thickness{3.0f};
    line_renderer.add_cone(
        transform,
        major_color,
        minor_color,
        major_thickness,
        minor_thickness,
        glm::vec3{0.0f, 0.0f,0.0f},
        m_height,
        m_bottom_radius,
        m_top_radius,
        camera_node->position_in_world(),
        80
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
            true,
            true,
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
