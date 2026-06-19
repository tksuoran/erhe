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

using erhe::geometry::to_geo_mat4f;
using erhe::geometry::transform;

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
        m_parameters.height,
        m_parameters.bottom_radius,
        m_parameters.top_radius,
        camera_node->position_in_world(),
        preview_settings.ideal_shape ? std::max(80, m_parameters.slice_count) : m_parameters.slice_count
    );
}

void Create_cone::imgui()
{
    ImGui::Text("Cone Parameters");

    ImGui::SliderFloat("Height",        &m_parameters.height ,       0.0f, 3.0f);
    ImGui::SliderFloat("Bottom Radius", &m_parameters.bottom_radius, 0.0f, 3.0f);
    ImGui::SliderFloat("Top Radius",    &m_parameters.top_radius,    0.0f, 3.0f);
    ImGui::SliderInt  ("Slices",        &m_parameters.slice_count,   1, 40);
    ImGui::SliderInt  ("Stacks",        &m_parameters.stack_count,   1, 6);
    ImGui::Checkbox   ("Use Top",       &m_parameters.use_top);
    ImGui::Checkbox   ("Use Bottom",    &m_parameters.use_bottom);
}

auto Create_cone::create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush>
{
    return create_brush(brush_create_info, m_parameters);
}

auto Create_cone::create_brush(Brush_data& brush_create_info, const Cone_parameters& parameters) -> std::shared_ptr<Brush>
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("cone");;
    brush_create_info.geometry = geometry;
    GEO::Mesh& geo_mesh = geometry->get_mesh();
    erhe::geometry::shapes::make_conical_frustum(
        geo_mesh,
        0.0,
        parameters.height,
        parameters.bottom_radius,
        parameters.top_radius,
        parameters.use_bottom,
        parameters.use_top,
        std::max(3, parameters.slice_count), // slice count
        std::max(1, parameters.stack_count)  // stack count
    );

    transform(*geometry.get(), *geometry.get(), to_geo_mat4f(erhe::math::mat4_swap_xy));

    geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates
        }
    );

    brush_create_info.normal_style = erhe::primitive::Normal_style::corner_normals;
    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}


}
