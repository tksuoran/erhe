#include "create/create_box.hpp"

#include "create/create_preview_settings.hpp"
#include "brushes/brush.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

namespace editor {

Create_box::~Create_box() noexcept = default;

void Create_box::render_preview(const Create_preview_settings& preview_settings)
{
    const Render_context& render_context = preview_settings.render_context;
    const auto& view_camera = render_context.scene_view.get_camera();
    if (!view_camera) {
        return;
    }

    erhe::renderer::Primitive_renderer line_renderer = get_line_renderer(preview_settings);
    line_renderer.add_cube(
        preview_settings.transform.get_matrix(),
        preview_settings.major_color,
        -0.5f * m_size,
         0.5f * m_size
    );
}

void Create_box::imgui()
{
    ERHE_PROFILE_FUNCTION();

    ImGui::Text("Box Parameters");

    ImGui::SliderFloat3("Size",  &m_size.x,  0.0f, 10.0f);
    ImGui::SliderInt3  ("Steps", &m_steps.x, 1,    10   );
    ImGui::SliderFloat ("Power", &m_power,   0.0f, 10.0f);
}

auto Create_box::create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush>
{
    auto geometry = std::make_shared<erhe::geometry::Geometry>("box");
    erhe::geometry::shapes::make_box(geometry->get_mesh(), to_geo_vec3f(m_size), to_geo_vec3i(m_steps), m_power);
    brush_create_info.geometry = geometry;
    transform(*geometry.get(), *geometry.get(), to_geo_mat4f(erhe::math::mat4_swap_xy));
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    geometry->process(flags);

    //// brush_create_info.geometry->compute_tangents();
    //// brush_create_info.geometry->compute_polygon_centroids();
    //// brush_create_info.collision_shape = TODO

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}


}
