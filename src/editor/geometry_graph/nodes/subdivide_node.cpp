#include "geometry_graph/nodes/subdivide_node.hpp"

#include "graph_editor/graph_editor_widgets.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/operation/subdivision/sqrt3_subdivision.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

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
        // Intermediate iterations only need structure (connect + build_edges +
        // centroids): their normals / texture coordinates would be discarded
        // and re-derived from positions by the next iteration. The final
        // iteration runs the full default post-processing so the output
        // payload carries them.
        const erhe::geometry::operation::Post_processing post_processing_level =
            ((i + 1) < iterations)
                ? erhe::geometry::operation::Post_processing::structural_only
                : erhe::geometry::operation::Post_processing::full_default;
        // No process_for_graph() here: both subdivision operations post-process
        // internally with at least connect + build_edges (structural_only and
        // full_default alike), so re-running them would be pure redundancy.
        switch (m_mode) {
            case Mode::catmull_clark: erhe::geometry::operation::catmull_clark_subdivision(*current.get(), *next.get(), nullptr, nullptr, post_processing_level); break;
            case Mode::sqrt3:         erhe::geometry::operation::sqrt3_subdivision        (*current.get(), *next.get(), nullptr, nullptr, post_processing_level); break;
        }
        current = next;
    }
    set_output(0, Geometry_payload{.value = current});
}

void Subdivide_node::imgui()
{
    const char* mode_names[] = { "Catmull-Clark", "Sqrt3" };
    int mode = static_cast<int>(m_mode);
    if (imgui_enum_stepper("mode", mode, mode_names, IM_ARRAYSIZE(mode_names))) {
        m_mode = static_cast<Mode>(mode);
        mark_dirty();
    }
    ImGui::TextUnformatted("Iterations");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##iterations", &m_iterations, 0.1f, 0, 6)) { mark_dirty(); }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

void Subdivide_node::write_parameters(nlohmann::json& out) const
{
    out["mode"]       = static_cast<int>(m_mode);
    out["iterations"] = m_iterations;
}

void Subdivide_node::read_parameters(const nlohmann::json& in)
{
    m_mode       = static_cast<Mode>(in.value("mode", static_cast<int>(m_mode)));
    m_iterations = in.value("iterations", m_iterations);
    mark_dirty();
}

}
