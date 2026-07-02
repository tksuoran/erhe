#include "geometry_graph/nodes/mesh_box_node.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <imgui/imgui.h>

namespace editor {

Mesh_box_node::Mesh_box_node()
    : Geometry_graph_node{"Box"}
{
    make_input_pin(Geometry_pin_key::float_value, "x size");
    make_input_pin(Geometry_pin_key::float_value, "y size");
    make_input_pin(Geometry_pin_key::float_value, "z size");
    make_output_pin(Geometry_pin_key::geometry, "geometry");
}

void Mesh_box_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const float x_size = get_input(0).get_float(m_size.x);
    const float y_size = get_input(1).get_float(m_size.y);
    const float z_size = get_input(2).get_float(m_size.z);

    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("box");
    erhe::geometry::shapes::make_box(
        geometry->get_mesh(),
        GEO::vec3f{x_size, y_size, z_size},
        erhe::geometry::to_geo_vec3i(m_steps),
        m_power
    );
    process_for_graph(*geometry.get());
    set_output(0, Geometry_payload{.value = geometry});
}

void Mesh_box_node::imgui()
{
    ImGui::TextUnformatted("Size");
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragFloat3("##size", &m_size.x, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    ImGui::TextUnformatted("Steps");
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragInt3("##steps", &m_steps.x, 0.1f, 1, 16)) { mark_dirty(); }
    ImGui::TextUnformatted("Power");
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragFloat("##power", &m_power, 0.01f, 0.1f, 10.0f)) { mark_dirty(); }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

}
