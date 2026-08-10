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
        // iteration also regenerates smooth normals and facet texture
        // coordinates so the output payload carries them; passing the final
        // flag set as regeneration_flags throughout keeps the intermediates
        // from interpolating those throwaway channels.
        constexpr uint64_t structural_flags =
            erhe::geometry::Geometry::process_flag_connect |
            erhe::geometry::Geometry::process_flag_build_edges |
            erhe::geometry::Geometry::process_flag_compute_facet_centroids;
        constexpr uint64_t full_flags =
            structural_flags |
            erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
            erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
        const uint64_t post_process_flags = ((i + 1) < iterations) ? structural_flags : full_flags;
        // No process_for_graph() here: both subdivision operations post-process
        // internally with at least connect + build_edges, so re-running them
        // would be pure redundancy.
        switch (m_mode) {
            case Mode::catmull_clark: erhe::geometry::operation::catmull_clark_subdivision(*current.get(), *next.get(), nullptr, nullptr, post_process_flags, full_flags); break;
            case Mode::sqrt3:         erhe::geometry::operation::sqrt3_subdivision        (*current.get(), *next.get(), nullptr, nullptr, post_process_flags, full_flags); break;
        }
        current = next;
    }
    set_output(0, Geometry_payload{.value = current});
}

void Subdivide_node::imgui()
{
    const char* mode_names[] = { "Catmull-Clark", "Sqrt3" };
    int mode = static_cast<int>(m_mode);
    if (imgui_enum_combo("mode", mode, mode_names, IM_ARRAYSIZE(mode_names), content_scale())) {
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
