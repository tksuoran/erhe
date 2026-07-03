#include "texture_graph/texture_graph.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "editor_log.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"

namespace editor {

void Texture_graph::mark_dirty()
{
    m_dirty = true;
}

auto Texture_graph::consume_forced_full() -> bool
{
    const bool forced_full = m_dirty;
    m_dirty = false;
    return forced_full;
}

auto Texture_graph::is_evaluation_needed() const -> bool
{
    if (m_dirty) {
        return true;
    }
    for (erhe::graph::Node* node : m_nodes) {
        const Texture_graph_node* texture_graph_node = dynamic_cast<const Texture_graph_node*>(node);
        if ((texture_graph_node != nullptr) && texture_graph_node->is_dirty()) {
            return true;
        }
    }
    return false;
}

void Texture_graph::evaluate_if_dirty()
{
    if (!is_evaluation_needed()) {
        return;
    }
    evaluate();
}

void Texture_graph::evaluate()
{
    sort();
    const bool evaluate_all = m_dirty;
    for (erhe::graph::Node* node : m_nodes) {
        Texture_graph_node* texture_graph_node = dynamic_cast<Texture_graph_node*>(node);
        if (texture_graph_node == nullptr) {
            continue;
        }
        if (evaluate_all) {
            texture_graph_node->mark_dirty();
        }
        if (!texture_graph_node->is_dirty()) {
            continue; // clean node keeps its cached output payloads
        }
        log_graph_editor->trace("Texture_graph: evaluating node '{}' {}", texture_graph_node->get_name(), texture_graph_node->get_id());
        texture_graph_node->evaluate(*this);
        texture_graph_node->clear_dirty();
        // Nodes are visited in topological order, so marking link sinks dirty
        // here reaches every downstream node in this same pass.
        for (erhe::graph::Pin& pin : texture_graph_node->get_output_pins()) {
            for (erhe::graph::Link* link : pin.get_links()) {
                Texture_graph_node* sink_node = dynamic_cast<Texture_graph_node*>(link->get_sink()->get_owner_node());
                if (sink_node != nullptr) {
                    sink_node->mark_dirty();
                }
            }
        }
    }
    m_dirty = false;
}

} // namespace editor
