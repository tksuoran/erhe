#include "geometry_graph/geometry_graph.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "editor_log.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"

namespace editor {

void Geometry_graph::mark_dirty()
{
    m_dirty = true;
}

auto Geometry_graph::is_evaluation_needed() const -> bool
{
    if (m_dirty) {
        return true;
    }
    for (erhe::graph::Node* node : m_nodes) {
        const Geometry_graph_node* geometry_graph_node = dynamic_cast<const Geometry_graph_node*>(node);
        if ((geometry_graph_node != nullptr) && geometry_graph_node->is_dirty()) {
            return true;
        }
    }
    return false;
}

void Geometry_graph::evaluate_if_dirty()
{
    if (!is_evaluation_needed()) {
        return;
    }
    evaluate();
}

void Geometry_graph::evaluate()
{
    sort();
    const bool evaluate_all = m_dirty;
    for (erhe::graph::Node* node : m_nodes) {
        Geometry_graph_node* geometry_graph_node = dynamic_cast<Geometry_graph_node*>(node);
        if (geometry_graph_node == nullptr) {
            continue;
        }
        if (evaluate_all) {
            geometry_graph_node->mark_dirty();
        }
        if (!geometry_graph_node->is_dirty()) {
            continue; // clean node keeps its cached output payloads
        }
        log_graph_editor->trace("Geometry_graph: evaluating node '{}' {}", geometry_graph_node->get_name(), geometry_graph_node->get_id());
        geometry_graph_node->evaluate(*this);
        geometry_graph_node->clear_dirty();
        // Nodes are visited in topological order, so marking link sinks
        // dirty here reaches every downstream node in this same pass.
        for (erhe::graph::Pin& pin : geometry_graph_node->get_output_pins()) {
            for (erhe::graph::Link* link : pin.get_links()) {
                Geometry_graph_node* sink_node = dynamic_cast<Geometry_graph_node*>(link->get_sink()->get_owner_node());
                if (sink_node != nullptr) {
                    sink_node->mark_dirty();
                }
            }
        }
    }
    m_dirty = false;
}

}
