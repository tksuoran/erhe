#include "tools/brushes/create/create_torus.hpp"

#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "tools/brushes/brush.hpp"

#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/physics/icollision_shape.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

void Create_torus::render_preview(
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
    line_renderer.add_torus(
        transform,
        major_color,
        minor_color,
        major_thickness,
        m_major_radius,
        m_minor_radius,
        m_use_debug_camera
            ? m_debug_camera
            : camera_node->position_in_world(),
        20,
        10,
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

[[nodiscard]] auto Create_torus::create(
    Brush_data& brush_create_info
) const -> std::shared_ptr<Brush>
{
    brush_create_info.geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_torus(
            m_major_radius,
            m_minor_radius,
            m_major_steps,
            m_minor_steps
        )
    );

    brush_create_info.geometry->transform(erhe::toolkit::mat4_swap_yz);
    brush_create_info.geometry->build_edges();
    brush_create_info.geometry->compute_polygon_normals();
    brush_create_info.geometry->compute_tangents();
    brush_create_info.geometry->compute_polygon_centroids();
    brush_create_info.geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}

} // namespace editor
