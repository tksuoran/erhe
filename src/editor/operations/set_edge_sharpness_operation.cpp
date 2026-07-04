#include "operations/set_edge_sharpness_operation.hpp"

#include "erhe_geometry/geometry.hpp"

#include <string>

namespace editor {

Set_edge_sharpness_operation::Set_edge_sharpness_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    const std::string verb = m_parameters.after.has_value() ? "Set" : "Clear";
    set_description(verb + " crease sharpness on " + std::to_string(m_parameters.edges.size()) + " edges");
}

void Set_edge_sharpness_operation::execute(App_context&)
{
    if (!m_parameters.geometry) {
        set_error("Set_edge_sharpness_operation: geometry is null");
        return;
    }
    if (m_parameters.before.size() != m_parameters.edges.size()) {
        set_error("Set_edge_sharpness_operation: before count mismatch");
        return;
    }
    for (const std::pair<GEO::index_t, GEO::index_t>& edge : m_parameters.edges) {
        if (m_parameters.after.has_value()) {
            m_parameters.geometry->set_edge_sharpness(edge.first, edge.second, m_parameters.after.value());
        } else {
            m_parameters.geometry->clear_edge_sharpness(edge.first, edge.second);
        }
    }
}

void Set_edge_sharpness_operation::undo(App_context&)
{
    if (!m_parameters.geometry) {
        set_error("Set_edge_sharpness_operation: geometry is null");
        return;
    }
    if (m_parameters.before.size() != m_parameters.edges.size()) {
        set_error("Set_edge_sharpness_operation: before count mismatch");
        return;
    }
    for (std::size_t i = 0, end = m_parameters.edges.size(); i < end; ++i) {
        const std::pair<GEO::index_t, GEO::index_t>& edge = m_parameters.edges[i];
        if (m_parameters.before[i].has_value()) {
            m_parameters.geometry->set_edge_sharpness(edge.first, edge.second, m_parameters.before[i].value());
        } else {
            m_parameters.geometry->clear_edge_sharpness(edge.first, edge.second);
        }
    }
}

}
