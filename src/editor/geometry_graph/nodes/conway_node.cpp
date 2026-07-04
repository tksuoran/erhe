#include "geometry_graph/nodes/conway_node.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/conway/ambo.hpp"
#include "erhe_geometry/operation/conway/chamfer3.hpp"
#include "erhe_geometry/operation/conway/dual.hpp"
#include "erhe_geometry/operation/conway/gyro.hpp"
#include "erhe_geometry/operation/conway/join.hpp"
#include "erhe_geometry/operation/conway/kis.hpp"
#include "erhe_geometry/operation/conway/meta.hpp"
#include "erhe_geometry/operation/conway/subdivide.hpp"
#include "erhe_geometry/operation/conway/truncate.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

Conway_node::Conway_node()
    : Geometry_graph_node{"Conway"}
{
    make_input_pin(Geometry_pin_key::geometry, "in");
    make_output_pin(Geometry_pin_key::geometry, "out");
}

void Conway_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::geometry::Geometry> source = get_input(0).get_geometry();
    if (!source) {
        set_output(0, Geometry_payload{});
        return;
    }

    // No process_for_graph() here: every Conway operation runs its own full
    // post-processing (connect + build_edges included), so re-running them
    // would be pure redundancy.
    std::shared_ptr<erhe::geometry::Geometry> destination = std::make_shared<erhe::geometry::Geometry>("conway");
    switch (m_operation) {
        case Conway_operation::ambo:      erhe::geometry::operation::ambo     (*source.get(), *destination.get()); break;
        case Conway_operation::dual:      erhe::geometry::operation::dual     (*source.get(), *destination.get()); break;
        case Conway_operation::join:      erhe::geometry::operation::join     (*source.get(), *destination.get()); break;
        case Conway_operation::kis:       erhe::geometry::operation::kis      (*source.get(), *destination.get(), m_kis_height); break;
        case Conway_operation::meta:      erhe::geometry::operation::meta     (*source.get(), *destination.get()); break;
        case Conway_operation::subdivide: erhe::geometry::operation::subdivide(*source.get(), *destination.get()); break;
        case Conway_operation::truncate:  erhe::geometry::operation::truncate (*source.get(), *destination.get(), m_truncate_ratio); break;
        case Conway_operation::chamfer:   erhe::geometry::operation::chamfer3 (*source.get(), *destination.get(), m_chamfer_ratio); break;
        case Conway_operation::gyro:      erhe::geometry::operation::gyro     (*source.get(), *destination.get(), m_gyro_ratio); break;
    }
    set_output(0, Geometry_payload{.value = destination});
}

void Conway_node::imgui()
{
    const char* operation_names[] = { "Ambo", "Dual", "Join", "Kis", "Meta", "Subdivide", "Truncate", "Chamfer", "Gyro" };
    const int operation = static_cast<int>(m_operation);
    // m_operation can be out of range (e.g. a malformed graph file or the MCP
    // set_parameter abuse path), so guard the preview index - operation_names[N]
    // for N out of bounds is an overread.
    const char* preview = (operation >= 0 && operation < IM_ARRAYSIZE(operation_names)) ? operation_names[operation] : "?";
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("operation", preview)) {
        for (int i = 0; i < IM_ARRAYSIZE(operation_names); ++i) {
            const bool is_selected = (operation == i);
            if (ImGui::Selectable(operation_names[i], is_selected)) {
                m_operation = static_cast<Conway_operation>(i);
                mark_dirty();
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    switch (m_operation) {
        case Conway_operation::kis: {
            ImGui::TextUnformatted("Height");
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::DragFloat("##height", &m_kis_height, 0.01f, -10.0f, 10.0f)) { mark_dirty(); }
            break;
        }
        case Conway_operation::truncate: {
            ImGui::TextUnformatted("Ratio");
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::DragFloat("##ratio", &m_truncate_ratio, 0.005f, 0.0f, 0.5f)) { mark_dirty(); }
            break;
        }
        case Conway_operation::chamfer: {
            ImGui::TextUnformatted("Bevel ratio");
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::DragFloat("##bevel_ratio", &m_chamfer_ratio, 0.005f, 0.0f, 1.0f)) { mark_dirty(); }
            break;
        }
        case Conway_operation::gyro: {
            ImGui::TextUnformatted("Ratio");
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::DragFloat("##ratio", &m_gyro_ratio, 0.005f, 0.0f, 1.0f)) { mark_dirty(); }
            break;
        }
        default: {
            break;
        }
    }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

void Conway_node::write_parameters(nlohmann::json& out) const
{
    out["operation"]      = static_cast<int>(m_operation);
    out["kis_height"]     = m_kis_height;
    out["truncate_ratio"] = m_truncate_ratio;
    out["chamfer_ratio"]  = m_chamfer_ratio;
    out["gyro_ratio"]     = m_gyro_ratio;
}

void Conway_node::read_parameters(const nlohmann::json& in)
{
    m_operation      = static_cast<Conway_operation>(in.value("operation", static_cast<int>(m_operation)));
    m_kis_height     = in.value("kis_height",     m_kis_height);
    m_truncate_ratio = in.value("truncate_ratio", m_truncate_ratio);
    m_chamfer_ratio  = in.value("chamfer_ratio",  m_chamfer_ratio);
    m_gyro_ratio     = in.value("gyro_ratio",     m_gyro_ratio);
    mark_dirty();
}

}
