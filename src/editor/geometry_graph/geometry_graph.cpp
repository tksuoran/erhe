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

auto Geometry_graph::consume_forced_full() -> bool
{
    const bool forced_full = m_dirty;
    m_dirty = false;
    return forced_full;
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

void Geometry_graph::set_display_node_id(const std::size_t id)
{
    if (m_display_node_id == id) {
        return;
    }
    m_display_node_id = id;
    mark_scene_output_nodes_dirty();
}

void Geometry_graph::set_ghost_node_id(const std::size_t id)
{
    if (m_ghost_node_id == id) {
        return;
    }
    m_ghost_node_id = id;
    mark_scene_output_nodes_dirty();
}

auto Geometry_graph::get_display_node_id() const -> std::size_t
{
    return m_display_node_id;
}

auto Geometry_graph::get_ghost_node_id() const -> std::size_t
{
    return m_ghost_node_id;
}

void Geometry_graph::copy_display_designation_from(const Geometry_graph& source)
{
    m_display_node_id = source.m_display_node_id;
    m_ghost_node_id   = source.m_ghost_node_id;
}

void Geometry_graph::handle_designated_node_removed(const std::size_t id)
{
    if ((id != 0) && ((id == m_display_node_id) || (id == m_ghost_node_id))) {
        mark_scene_output_nodes_dirty();
    }
}

auto Geometry_graph::find_node_by_log_id(const std::size_t id) const -> Geometry_graph_node*
{
    if (id == 0) {
        return nullptr;
    }
    for (erhe::graph::Node* node : m_nodes) {
        Geometry_graph_node* geometry_graph_node = dynamic_cast<Geometry_graph_node*>(node);
        if ((geometry_graph_node != nullptr) && (geometry_graph_node->get_log_id() == id)) {
            return geometry_graph_node;
        }
    }
    return nullptr;
}

void Geometry_graph::set_preview_mesh_memory(erhe::scene_renderer::Mesh_memory* mesh_memory)
{
    m_preview_mesh_memory = mesh_memory;
}

void Geometry_graph::mark_scene_output_nodes_dirty()
{
    for (erhe::graph::Node* node : m_nodes) {
        Geometry_graph_node* geometry_graph_node = dynamic_cast<Geometry_graph_node*>(node);
        if ((geometry_graph_node != nullptr) && geometry_graph_node->is_scene_output()) {
            geometry_graph_node->mark_dirty();
        }
    }
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

    // Phase 1: every node except the scene outputs, in topological order.
    // The scene outputs are deferred to phase 2 because the display / ghost
    // designation lets them read ANOTHER node's cached output payload, and
    // that node may be an unconnected subtree the topological sort placed
    // after them (sort() only constrains linked nodes).
    const Geometry_graph_node* display_node = find_node_by_log_id(m_display_node_id);
    const Geometry_graph_node* ghost_node   = find_node_by_log_id(m_ghost_node_id);
    bool designated_node_evaluated = false;
    for (erhe::graph::Node* node : m_nodes) {
        Geometry_graph_node* geometry_graph_node = dynamic_cast<Geometry_graph_node*>(node);
        if (geometry_graph_node == nullptr) {
            continue;
        }
        if (geometry_graph_node->is_scene_output()) {
            continue; // phase 2
        }
        if (evaluate_all) {
            geometry_graph_node->mark_dirty();
        }
        if (!geometry_graph_node->is_dirty()) {
            continue; // clean node keeps its cached output payloads
        }
        log_graph_editor->trace("Geometry_graph: evaluating node '{}' {}", geometry_graph_node->get_name(), geometry_graph_node->get_log_id());
        geometry_graph_node->evaluate(*this);
        geometry_graph_node->clear_dirty();
        if (m_preview_mesh_memory != nullptr) {
            geometry_graph_node->build_preview_primitive(*m_preview_mesh_memory);
        }
        if ((geometry_graph_node == display_node) || (geometry_graph_node == ghost_node)) {
            designated_node_evaluated = true;
        }
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

    // Phase 2: scene-output nodes. Every phase-1 payload is final now, so a
    // display / ghost designation resolves to fresh data; a re-evaluated
    // designated node forces a re-bake even when the output's wired input
    // did not change.
    for (erhe::graph::Node* node : m_nodes) {
        Geometry_graph_node* geometry_graph_node = dynamic_cast<Geometry_graph_node*>(node);
        if ((geometry_graph_node == nullptr) || !geometry_graph_node->is_scene_output()) {
            continue;
        }
        if (evaluate_all || designated_node_evaluated) {
            geometry_graph_node->mark_dirty();
        }
        if (!geometry_graph_node->is_dirty()) {
            continue;
        }
        log_graph_editor->trace("Geometry_graph: evaluating node '{}' {}", geometry_graph_node->get_name(), geometry_graph_node->get_log_id());
        geometry_graph_node->evaluate(*this);
        geometry_graph_node->clear_dirty();
    }

    m_dirty = false;
}

}
