#include "tools/brushes/create/create_torus.hpp"

#include "tools/brushes/create/create_preview_settings.hpp"

#include "renderers/render_context.hpp"
#include "tools/brushes/brush.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/torus.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/node.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

namespace editor {

void Create_torus::render_preview(const Create_preview_settings& preview_settings)
{
    const Render_context& render_context = preview_settings.render_context;
    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    erhe::renderer::Primitive_renderer line_renderer = get_line_renderer(preview_settings);
    line_renderer.add_torus(
        preview_settings.transform,
        preview_settings.major_color,
        preview_settings.minor_color,
        preview_settings.major_thickness,
        m_major_radius,
        m_minor_radius,
        m_use_debug_camera
            ? m_debug_camera
            : camera_node->position_in_world(),
        preview_settings.ideal_shape ? std::max(20, m_major_steps) : m_major_steps,
        preview_settings.ideal_shape ? std::max(10, m_minor_steps) : m_minor_steps,
        m_epsilon,
        m_debug_major,
        m_debug_minor
    );
}

void Create_torus::imgui()
{
    ImGui::Text("Torus Parameters");

    ImGui::SliderFloat("Major Radius", &m_major_radius, 0.0f, 3.0f);
    ImGui::SliderFloat("Minor Radius", &m_minor_radius, 0.0f, m_major_radius);
    ImGui::SliderInt  ("Major Steps",  &m_major_steps,  3, 40);
    ImGui::SliderInt  ("Minor Steps",  &m_minor_steps,  3, 40);

    ImGui::Separator();

    ImGui::Checkbox    ("Use Debug Camera", &m_use_debug_camera);
    ImGui::SliderFloat3("Debug Camera",     &m_debug_camera.x, -4.0f, 4.0f);
    ImGui::SliderInt   ("Debug Major",      &m_debug_major,     0, 100);
    ImGui::SliderInt   ("Debug Minor",      &m_debug_minor,     0, 100);
    ImGui::SliderFloat ("Epsilon",          &m_epsilon,         0.0f, 1.0f);
}

auto Create_torus::create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush>
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("torus");
    brush_create_info.geometry = geometry;
    erhe::geometry::shapes::make_torus(
        geometry->get_mesh(),
        m_major_radius,
        m_minor_radius,
        m_major_steps,
        m_minor_steps
    );

    transform(*geometry.get(), *geometry.get(), to_geo_mat4f(erhe::math::mat4_swap_yz));

    //brush_create_info.geometry->build_edges();
    //brush_create_info.geometry->compute_polygon_normals();
    //brush_create_info.geometry->compute_tangents();
    //brush_create_info.geometry->compute_polygon_centroids();
    //brush_create_info.geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}

}
