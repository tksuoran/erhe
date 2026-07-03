#pragma once

#include "erhe_graph/graph.hpp"

namespace editor {

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

private:
    void evaluate();

    bool m_dirty{true};
};

}
