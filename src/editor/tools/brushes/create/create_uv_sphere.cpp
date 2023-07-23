#include "tools/brushes/create/create_uv_sphere.hpp"
#include "tools/brushes/create/create_preview_settings.hpp"

#include "renderers/render_context.hpp"
#include "tools/brushes/brush.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/shapes/sphere.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/renderer/line_renderer.hpp"
#include "erhe/scene/node.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

void Create_uv_sphere::render_preview(
    const Create_preview_settings& preview_settings
)
{
    const Render_context& render_context = preview_settings.render_context;
    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    auto& line_renderer = get_line_renderer(preview_settings);
    line_renderer.add_sphere(
        preview_settings.transform,
        preview_settings.major_color,
        preview_settings.minor_color,
        preview_settings.major_thickness,
        preview_settings.minor_thickness,
        glm::vec3{0.0f, 0.0f,0.0f},
        m_radius,
        &camera_node->world_from_node_transform(),
        preview_settings.ideal_shape // TODO Current preview only works for ideal shape
            ? std::max(80, m_slice_count)
            : m_slice_count
    );
}

void Create_uv_sphere::imgui()
{
    ImGui::Text("Sphere Parameters");

    ImGui::SliderFloat("Radius", &m_radius,      0.0f, 4.0f);
    ImGui::SliderInt  ("Slices", &m_slice_count, 1, 100);
    ImGui::SliderInt  ("Stacks", &m_stack_count, 1, 100);
}

[[nodiscard]] auto Create_uv_sphere::create(
    Brush_data& brush_create_info
) const -> std::shared_ptr<Brush>
{
    brush_create_info.geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_sphere(
            m_radius,
            std::max(1, m_slice_count), // slice count
            std::max(1, m_stack_count)  // stack count
        )
    );

    brush_create_info.geometry->build_edges();
    brush_create_info.geometry->compute_polygon_normals();
    brush_create_info.geometry->compute_tangents();
    brush_create_info.geometry->compute_polygon_centroids();
    brush_create_info.geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);
    brush_create_info.collision_shape = erhe::physics::ICollision_shape::create_sphere_shape_shared(m_radius);

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}

} // namespace editor
