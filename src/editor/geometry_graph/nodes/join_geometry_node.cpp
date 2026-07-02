#include "geometry_graph/nodes/join_geometry_node.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_graph/pin.hpp"

#include <imgui/imgui.h>

namespace editor {

Join_geometry_node::Join_geometry_node()
    : Geometry_graph_node{"Join"}
{
    make_input_pin(Geometry_pin_key::geometry, "in");
    make_output_pin(Geometry_pin_key::geometry, "out");
}

void Join_geometry_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::geometry::Geometry> merged = get_input(0).get_geometry();
    if (!merged) {
        set_output(0, Geometry_payload{});
        return;
    }
    // With a single link the accumulated geometry is the upstream node's
    // shared, already processed geometry and must not be mutated here.
    // With multiple links it is a fresh merge that still needs processing.
    const erhe::graph::Pin& input_pin = get_input_pins().at(0);
    if (input_pin.get_links().size() > 1) {
        process_for_graph(*merged.get());
    }
    set_output(0, Geometry_payload{.value = merged});
}

void Join_geometry_node::imgui()
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

}
