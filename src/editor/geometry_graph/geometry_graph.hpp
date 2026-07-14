#pragma once

#include "erhe_graph/graph.hpp"

#include <cstddef>

namespace editor {

class Geometry_graph_node;

// Geometry node graph: erhe::graph::Graph with eager, dirty-flag driven
// evaluation. Geometry flows through nodes as
// std::shared_ptr<erhe::geometry::Geometry>; nodes that modify geometry
// allocate a new Geometry (no copy-on-write yet).
//
// Evaluation is incremental: only dirty nodes and their downstream
// dependents re-run; clean nodes keep their cached output payloads.
// Structural edits mark the affected nodes dirty at the edit site
// (Geometry_graph_window insert / erase / connect / disconnect).
class Geometry_graph : public erhe::graph::Graph
{
public:
    // Re-evaluates dirty nodes and their downstream dependents if any
    // node is dirty. Geometry operations are too expensive to run every
    // frame, so unlike Shader_graph this must only do work when
    // something changed.
    void evaluate_if_dirty();

    // Forces the next evaluation to re-run every node regardless of
    // per-node dirty state.
    void mark_dirty();

    // True when any node (or the whole graph) is marked dirty; drives
    // the background evaluation launched by Geometry_graph_window.
    [[nodiscard]] auto is_evaluation_needed() const -> bool;

    // Background evaluation support: the window moves the live graph's
    // forced-full request onto the shadow graph it is about to evaluate.
    // Returns whether a full re-evaluation was requested and clears the
    // request.
    [[nodiscard]] auto consume_forced_full() -> bool;

    // Houdini-style display / ghost designation (one node each, 0 = none).
    // The display node's geometry replaces the output node's wired input in
    // the bake; the ghost node's geometry is additionally baked as an
    // edge-lines-only scene mesh. Ids are node ids in the live graph; shadow
    // clones resolve them through get_log_id() (which mirrors the live id),
    // so the same id works in both graphs. The setters mark every
    // scene-output node dirty on change so the next background run re-bakes.
    void set_display_node_id(std::size_t id);
    void set_ghost_node_id  (std::size_t id);
    [[nodiscard]] auto get_display_node_id() const -> std::size_t;
    [[nodiscard]] auto get_ghost_node_id  () const -> std::size_t;

    // Raw designation copy for the background-evaluation snapshot: copies
    // the ids without touching any dirty flags (the snapshot's per-node
    // dirty flags are authoritative).
    void copy_display_designation_from(const Geometry_graph& source);

    // A designated node was erased: keep the id (undo of the erase restores
    // the same node object, self-healing the designation) but re-bake so the
    // scene falls back to the wired input meanwhile.
    void handle_designated_node_removed(std::size_t id);

    // Resolves a designation id against this graph's nodes via get_log_id()
    // (live nodes: own id; shadow clones: mirrored live id). Null when the
    // id is 0 or no longer present.
    [[nodiscard]] auto find_node_by_log_id(std::size_t id) const -> Geometry_graph_node*;

private:
    void evaluate();
    void mark_scene_output_nodes_dirty();

    bool        m_dirty{true};
    std::size_t m_display_node_id{0};
    std::size_t m_ghost_node_id{0};
};

}
