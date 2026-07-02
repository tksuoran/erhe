#include "geometry_graph/nodes/boolean_node.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/csg/difference.hpp"
#include "erhe_geometry/operation/csg/intersection.hpp"
#include "erhe_geometry/operation/csg/union.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

Boolean_node::Boolean_node()
    : Geometry_graph_node{"Boolean"}
{
    make_input_pin(Geometry_pin_key::geometry, "A");
    make_input_pin(Geometry_pin_key::geometry, "B");
    make_output_pin(Geometry_pin_key::geometry, "out");
}

void Boolean_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::geometry::Geometry> lhs = get_input(0).get_geometry();
    const std::shared_ptr<erhe::geometry::Geometry> rhs = get_input(1).get_geometry();
    if (!lhs || !rhs) {
        // Pass through whichever side is connected so partial graphs stay visible
        set_output(0, lhs ? Geometry_payload{.value = lhs} : (rhs ? Geometry_payload{.value = rhs} : Geometry_payload{}));
        return;
    }

    std::shared_ptr<erhe::geometry::Geometry> destination = std::make_shared<erhe::geometry::Geometry>("boolean");
    switch (m_operation) {
        case Boolean_operation::union_operation: erhe::geometry::operation::union_      (*lhs.get(), *rhs.get(), *destination.get()); break;
        case Boolean_operation::intersection:    erhe::geometry::operation::intersection(*lhs.get(), *rhs.get(), *destination.get()); break;
        case Boolean_operation::difference:      erhe::geometry::operation::difference  (*lhs.get(), *rhs.get(), *destination.get()); break;
    }
    process_for_graph(*destination.get());
    set_output(0, Geometry_payload{.value = destination});
}

void Boolean_node::imgui()
{
    const char* operation_names[] = { "Union", "Intersection", "Difference" };
    int operation = static_cast<int>(m_operation);
    if (imgui_enum_stepper("operation", operation, operation_names, IM_ARRAYSIZE(operation_names))) {
        m_operation = static_cast<Boolean_operation>(operation);
        mark_dirty();
    }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

void Boolean_node::write_parameters(nlohmann::json& out) const
{
    out["operation"] = static_cast<int>(m_operation);
}

void Boolean_node::read_parameters(const nlohmann::json& in)
{
    m_operation = static_cast<Boolean_operation>(in.value("operation", static_cast<int>(m_operation)));
    mark_dirty();
}

}
