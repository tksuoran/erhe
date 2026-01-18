#include "create/create_cone.hpp"

#include "brushes/brush.hpp"
#include "create/create_preview_settings.hpp"
#include "renderers/render_context.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/cone.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/node.hpp"

#include <imgui/imgui.h>

namespace editor {

Create_cone::~Create_cone() noexcept = default;

void Create_cone::render_preview(const Create_preview_settings& preview_settings)
{
    const Render_context& render_context = preview_settings.render_context;
    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    erhe::renderer::Primitive_renderer line_renderer = get_line_renderer(preview_settings);
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
        preview_settings.ideal_shape ? std::max(80, m_slice_count) : m_slice_count
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

auto Create_cone::create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush>
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("cone");;
    brush_create_info.geometry = geometry;
    GEO::Mesh& geo_mesh = geometry->get_mesh();
    erhe::geometry::shapes::make_conical_frustum(
        geo_mesh,
        0.0,
        m_height,
        m_bottom_radius,
        m_top_radius,
        m_use_bottom,
        m_use_top,
        std::max(3, m_slice_count), // slice count
        std::max(1, m_stack_count)  // stack count
    );

    transform(*geometry.get(), *geometry.get(), to_geo_mat4f(erhe::math::mat4_swap_xy));

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    geometry->process(flags);

    /// brush_create_info.mesh->compute_tangents();

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}


}
