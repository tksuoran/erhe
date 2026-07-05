#include "geometry_graph/nodes/mesh_cone_node.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/cone.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace editor {

Mesh_cone_node::Mesh_cone_node()
    : Geometry_graph_node{"Cone"}
{
    make_input_pin(Geometry_pin_key::float_value, "height");
    make_input_pin(Geometry_pin_key::float_value, "radius");
    make_output_pin(Geometry_pin_key::geometry, "geometry");
}

void Mesh_cone_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const float height        = get_input(0).get_float(m_height);
    const float bottom_radius = get_input(1).get_float(m_bottom_radius);
    const int   slice_count   = std::max(3, m_slice_count);
    const int   stack_division = std::max(1, m_stack_division);

    // make_cone() extends along the x axis from min_x to max_x.
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("cone");
    erhe::geometry::shapes::make_cone(geometry->get_mesh(), 0.0f, height, bottom_radius, m_use_bottom, slice_count, stack_division);
    process_for_graph(*geometry.get());
    set_output(0, Geometry_payload{.value = geometry});
}

void Mesh_cone_node::imgui()
{
    ImGui::TextUnformatted("Height");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##height", &m_height, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    ImGui::TextUnformatted("Radius");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##radius", &m_bottom_radius, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    if (ImGui::Checkbox("Bottom", &m_use_bottom)) { mark_dirty(); }
    ImGui::TextUnformatted("Slices");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##slices", &m_slice_count, 0.1f, 3, 128)) { mark_dirty(); }
    ImGui::TextUnformatted("Stacks");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##stacks", &m_stack_division, 0.1f, 1, 128)) { mark_dirty(); }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

void Mesh_cone_node::write_parameters(nlohmann::json& out) const
{
    out["height"]     = m_height;
    out["radius"]     = m_bottom_radius;
    out["use_bottom"] = m_use_bottom;
    out["slices"]     = m_slice_count;
    out["stacks"]     = m_stack_division;
}

void Mesh_cone_node::read_parameters(const nlohmann::json& in)
{
    m_height         = in.value("height",     m_height);
    m_bottom_radius  = in.value("radius",     m_bottom_radius);
    m_use_bottom     = in.value("use_bottom", m_use_bottom);
    m_slice_count    = in.value("slices",     m_slice_count);
    m_stack_division = in.value("stacks",     m_stack_division);
    mark_dirty();
}

}
