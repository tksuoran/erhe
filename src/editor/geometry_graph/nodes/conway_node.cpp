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

namespace {

[[nodiscard]] auto node_name_for(const Conway_node::Conway_operation operation) -> const char*
{
    const Conway_node::Operation_info* info = Conway_node::find_operation_info(operation);
    return (info != nullptr) ? info->label : "Conway";
}

} // anonymous namespace

auto Conway_node::find_operation_info(const Conway_operation operation) -> const Operation_info*
{
    for (const Operation_info& info : c_operation_infos) {
        if (info.operation == operation) {
            return &info;
        }
    }
    return nullptr;
}

Conway_node::Conway_node(const Conway_operation operation)
    : Geometry_graph_node{node_name_for(operation)}
    , m_operation        {operation}
{
    make_input_pin(Geometry_pin_key::geometry, "in");
    make_output_pin(Geometry_pin_key::geometry, "out");
}

void Conway_node::set_operation(const Conway_operation operation)
{
    m_operation = operation;
    const Operation_info* info = find_operation_info(operation);
    if (info != nullptr) {
        set_name(info->label);
        set_factory_type_name(info->type_name);
    }
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
        default:                          break; // out-of-range operation: empty geometry
    }
    set_output(0, Geometry_payload{.value = destination});
}

void Conway_node::imgui()
{
    // The operation is fixed per node type (the node name shows it); only the
    // operator's own parameter is editable.
    switch (m_operation) {
        case Conway_operation::kis: {
            ImGui::TextUnformatted("Height");
            ImGui::SetNextItemWidth(140.0f * content_scale());
            if (ImGui::DragFloat("##height", &m_kis_height, 0.01f, -10.0f, 10.0f)) { mark_dirty(); }
            break;
        }
        case Conway_operation::truncate: {
            ImGui::TextUnformatted("Ratio");
            ImGui::SetNextItemWidth(140.0f * content_scale());
            if (ImGui::DragFloat("##ratio", &m_truncate_ratio, 0.005f, 0.0f, 0.5f)) { mark_dirty(); }
            break;
        }
        case Conway_operation::chamfer: {
            ImGui::TextUnformatted("Bevel ratio");
            ImGui::SetNextItemWidth(140.0f * content_scale());
            if (ImGui::DragFloat("##bevel_ratio", &m_chamfer_ratio, 0.005f, 0.0f, 1.0f)) { mark_dirty(); }
            break;
        }
        case Conway_operation::gyro: {
            ImGui::TextUnformatted("Ratio");
            ImGui::SetNextItemWidth(140.0f * content_scale());
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
    // "operation" is redundant with the factory type name for the
    // per-operator types, but keeps the parameter surface (MCP, older
    // readers) unchanged from the legacy combo node.
    out["operation"]      = static_cast<int>(m_operation);
    out["kis_height"]     = m_kis_height;
    out["truncate_ratio"] = m_truncate_ratio;
    out["chamfer_ratio"]  = m_chamfer_ratio;
    out["gyro_ratio"]     = m_gyro_ratio;
}

void Conway_node::read_parameters(const nlohmann::json& in)
{
    // Adopting "operation" is what migrates a legacy "conway" node (the
    // parameter is its only record of the operator) and keeps the parameter
    // writable through MCP; for per-operator types the value normally just
    // restates the type.
    set_operation(static_cast<Conway_operation>(in.value("operation", static_cast<int>(m_operation))));
    m_kis_height     = in.value("kis_height",     m_kis_height);
    m_truncate_ratio = in.value("truncate_ratio", m_truncate_ratio);
    m_chamfer_ratio  = in.value("chamfer_ratio",  m_chamfer_ratio);
    m_gyro_ratio     = in.value("gyro_ratio",     m_gyro_ratio);
    mark_dirty();
}

}
