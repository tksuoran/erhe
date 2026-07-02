#include "geometry_graph/nodes/subdivide_node.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/operation/subdivision/sqrt3_subdivision.hpp"

#include <imgui/imgui.h>

#include <algorithm>

namespace editor {

Subdivide_node::Subdivide_node()
    : Geometry_graph_node{"Subdivide"}
{
    make_input_pin(Geometry_pin_key::geometry,  "in");
    make_input_pin(Geometry_pin_key::int_value, "iterations");
    make_output_pin(Geometry_pin_key::geometry, "out");
}

void Subdivide_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::geometry::Geometry> source = get_input(0).get_geometry();
    if (!source) {
        set_output(0, Geometry_payload{});
        return;
    }

    // Facet count multiplies per iteration; keep the range small.
    const int iterations = std::clamp(get_input(1).get_int(m_iterations), 0, 6);

    std::shared_ptr<erhe::geometry::Geometry> current = source;
    for (int i = 0; i < iterations; ++i) {
        std::shared_ptr<erhe::geometry::Geometry> next = std::make_shared<erhe::geometry::Geometry>("subdivided");
        switch (m_mode) {
            case Mode::catmull_clark: erhe::geometry::operation::catmull_clark_subdivision(*current.get(), *next.get()); break;
            case Mode::sqrt3:         erhe::geometry::operation::sqrt3_subdivision        (*current.get(), *next.get()); break;
        }
        process_for_graph(*next.get());
        current = next;
    }
    set_output(0, Geometry_payload{.value = current});
}

void Subdivide_node::imgui()
{
    const char* mode_names[] = { "Catmull-Clark", "Sqrt3" };
    int mode = static_cast<int>(m_mode);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo("##mode", &mode, mode_names, IM_ARRAYSIZE(mode_names))) {
        m_mode = static_cast<Mode>(mode);
        mark_dirty();
    }
    ImGui::TextUnformatted("Iterations");
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragInt("##iterations", &m_iterations, 0.1f, 0, 6)) { mark_dirty(); }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

}
