#include "geometry_graph/nodes/mesh_torus_node.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/torus.hpp"

#include <imgui/imgui.h>

#include <algorithm>

namespace editor {

Mesh_torus_node::Mesh_torus_node()
    : Geometry_graph_node{"Torus"}
{
    make_input_pin(Geometry_pin_key::float_value, "major r");
    make_input_pin(Geometry_pin_key::float_value, "minor r");
    make_input_pin(Geometry_pin_key::int_value,   "major steps");
    make_input_pin(Geometry_pin_key::int_value,   "minor steps");
    make_output_pin(Geometry_pin_key::geometry, "geometry");
}

void Mesh_torus_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const float major_radius = get_input(0).get_float(m_major_radius);
    const float minor_radius = get_input(1).get_float(m_minor_radius);
    const int   major_steps  = std::max(3, get_input(2).get_int(m_major_steps));
    const int   minor_steps  = std::max(3, get_input(3).get_int(m_minor_steps));

    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("torus");
    erhe::geometry::shapes::make_torus(geometry->get_mesh(), major_radius, minor_radius, major_steps, minor_steps);
    process_for_graph(*geometry.get());
    set_output(0, Geometry_payload{.value = geometry});
}

void Mesh_torus_node::imgui()
{
    ImGui::TextUnformatted("Major / minor radius");
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragFloat("##major_radius", &m_major_radius, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragFloat("##minor_radius", &m_minor_radius, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    ImGui::TextUnformatted("Major / minor steps");
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragInt("##major_steps", &m_major_steps, 0.1f, 3, 128)) { mark_dirty(); }
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragInt("##minor_steps", &m_minor_steps, 0.1f, 3, 128)) { mark_dirty(); }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

}
