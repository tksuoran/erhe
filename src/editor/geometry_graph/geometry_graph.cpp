#include "geometry_graph/geometry_graph.hpp"
#include "geometry_graph/geometry_graph_node.hpp"

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
    for (erhe::graph::Node* node : m_nodes) {
        Geometry_graph_node* geometry_graph_node = dynamic_cast<Geometry_graph_node*>(node);
        if (geometry_graph_node != nullptr) {
            geometry_graph_node->evaluate(*this);
            geometry_graph_node->clear_dirty();
        }
    }
    m_dirty = false;
}

}
