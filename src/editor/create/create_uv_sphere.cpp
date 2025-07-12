#include "create/create_uv_sphere.hpp"

#include "brushes/brush.hpp"
#include "create/create_preview_settings.hpp"
#include "renderers/render_context.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/node.hpp"

#include <imgui/imgui.h>

namespace editor {

void Create_uv_sphere::render_preview(const Create_preview_settings& preview_settings)
{
    const Render_context& render_context = preview_settings.render_context;
    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    erhe::renderer::Primitive_renderer line_renderer = get_line_renderer(preview_settings);
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

auto Create_uv_sphere::create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush>
{
    auto geometry = std::make_shared<erhe::geometry::Geometry>("sphere");
    erhe::geometry::shapes::make_sphere(
        geometry->get_mesh(),
        m_radius,
        std::max(1, m_slice_count), // slice count
        std::max(1, m_stack_count)  // stack count
    );
    brush_create_info.geometry = geometry;

    // geometry->build_edges();
    // geometry->compute_polygon_normals();
    // geometry->compute_tangents();
    // geometry->compute_polygon_centroids();
    // geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);
    brush_create_info.collision_shape = erhe::physics::ICollision_shape::create_sphere_shape_shared(m_radius);

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}

}
