#include "create/create_capsule.hpp"

#include "brushes/brush.hpp"
#include "create/create_preview_settings.hpp"
#include "renderers/render_context.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/capsule.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/node.hpp"

#include <imgui/imgui.h>

#include <cmath>

namespace editor {

Create_capsule::~Create_capsule() noexcept = default;

void Create_capsule::render_preview(const Create_preview_settings& preview_settings)
{
    const Render_context& render_context = preview_settings.render_context;
    const auto* camera_node = render_context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    const float half_length = 0.5f * m_length;
    erhe::renderer::Primitive_renderer line_renderer = get_line_renderer(preview_settings);
    line_renderer.add_capsule(
        preview_settings.transform,
        preview_settings.major_color,
        preview_settings.minor_color,
        preview_settings.major_thickness,
        preview_settings.minor_thickness,
        glm::vec3{0.0f, -half_length, 0.0f},
        m_length,
        m_bottom_radius,
        m_top_radius,
        camera_node->position_in_world(),
        preview_settings.ideal_shape ? std::max(80, m_slice_count) : m_slice_count
    );
}

void Create_capsule::imgui()
{
    ImGui::Text("Capsule Parameters");

    ImGui::SliderFloat("Length",        &m_length,        0.0f,  3.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Bottom Radius", &m_bottom_radius, 0.05f, 3.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Top Radius",    &m_top_radius,    0.05f, 3.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderInt  ("Slices",        &m_slice_count,            3, 40);
    ImGui::SliderInt  ("Stacks",        &m_hemisphere_stack_count, 1, 10);

    // make_capsule() requires |bottom_radius - top_radius| < length when the
    // radii differ: the tangent cone between the cap spheres exists only
    // while neither sphere contains the other.
    if (m_bottom_radius != m_top_radius) {
        const float min_length = std::abs(m_bottom_radius - m_top_radius) + 0.01f;
        m_length = std::max(m_length, min_length);
    }
}

auto Create_capsule::create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush>
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("capsule");
    brush_create_info.geometry = geometry;
    GEO::Mesh& geo_mesh = geometry->get_mesh();
    erhe::geometry::shapes::make_capsule( // axis = y
        geo_mesh,
        m_bottom_radius,
        m_top_radius,
        m_length,
        std::max(3, m_slice_count),            // slice count
        std::max(1, m_hemisphere_stack_count)  // hemisphere stack count
    );

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    geometry->process(flags);

    // erhe::physics exposes only the uniform-radius capsule shape; tapered
    // capsules fall back to the default brush collision shape.
    if (m_bottom_radius == m_top_radius) {
        brush_create_info.collision_shape = erhe::physics::ICollision_shape::create_capsule_shape_shared(
            erhe::physics::Axis::Y,
            m_bottom_radius,
            m_length
        );
    }

    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_create_info);
    return brush;
}

}
