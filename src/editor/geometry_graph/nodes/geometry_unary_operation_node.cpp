#include "geometry_graph/nodes/geometry_unary_operation_node.hpp"

#include "erhe_geometry/geometry.hpp"

#include <imgui/imgui.h>

namespace editor {

Geometry_unary_operation_node::Geometry_unary_operation_node(const char* label, Operation operation)
    : Geometry_graph_node{label}
    , m_operation        {operation}
{
    make_input_pin(Geometry_pin_key::geometry, "in");
    make_output_pin(Geometry_pin_key::geometry, "out");
}

void Geometry_unary_operation_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::geometry::Geometry> source = get_input(0).get_geometry();
    if (!source) {
        set_output(0, Geometry_payload{});
        return;
    }
    std::shared_ptr<erhe::geometry::Geometry> destination = std::make_shared<erhe::geometry::Geometry>(get_name());
    m_operation(*source.get(), *destination.get());
    process_for_graph(*destination.get());
    set_output(0, Geometry_payload{.value = destination});
}

void Geometry_unary_operation_node::imgui()
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

}
