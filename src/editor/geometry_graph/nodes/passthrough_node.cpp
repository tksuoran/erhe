#include "geometry_graph/nodes/passthrough_node.hpp"

#include "erhe_geometry/geometry.hpp"

#include <imgui/imgui.h>

namespace editor {

Passthrough_node::Passthrough_node()
    : Geometry_graph_node{"Passthrough"}
{
    make_input_pin (Geometry_pin_key::geometry, "in");
    make_output_pin(Geometry_pin_key::geometry, "out");
}

void Passthrough_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    // Forward the input payload unchanged. The geometry pointer is shared,
    // not copied: passthrough does not mutate, and downstream nodes already
    // treat upstream payload geometry as immutable (copy before mutating).
    // The payload invariant (connectivity + edges present) holds because the
    // upstream producer processed the geometry it published.
    set_output(0, get_input(0));
}

void Passthrough_node::imgui()
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

}
