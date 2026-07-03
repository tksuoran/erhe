#pragma once

#include "erhe_graph/graph.hpp"

namespace editor {

// Texture node graph: erhe::graph::Graph with eager, dirty-flag driven,
// synchronous evaluation.
//
// Unlike the geometry graph, texture evaluation is cheap - each node only
// records how it composes into GLSL (the heavy shader compile / render happens
// at sinks during the editor frame), so there is no async shadow-clone engine
// here (doc/texture-graph-plan.md decision 8). evaluate_if_dirty() runs
// directly on the live graph.
//
// Evaluation is incremental: only dirty nodes and their downstream dependents
// re-run; clean nodes keep their cached output payloads. Structural edits mark
// the affected nodes dirty at the edit site (Texture_graph_window insert /
// erase / connect / disconnect).
class Texture_graph : public erhe::graph::Graph
{
public:
    // Re-evaluates dirty nodes and their downstream dependents if any node is
    // dirty.
    void evaluate_if_dirty();

    // Forces the next evaluation to re-run every node regardless of per-node
    // dirty state.
    void mark_dirty();

    // True when any node (or the whole graph) is marked dirty.
    [[nodiscard]] auto is_evaluation_needed() const -> bool;

    // Returns whether a full re-evaluation was requested and clears the
    // request (kept for parity with the geometry graph and for serialization
    // load, which forces a full pass).
    [[nodiscard]] auto consume_forced_full() -> bool;

private:
    void evaluate();

    bool m_dirty{true};
};

} // namespace editor
