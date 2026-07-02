#pragma once

#include "erhe_graph/graph.hpp"

namespace editor {

// Geometry node graph: erhe::graph::Graph with eager, dirty-flag driven
// evaluation. Geometry flows through nodes as
// std::shared_ptr<erhe::geometry::Geometry>; nodes that modify geometry
// allocate a new Geometry (no copy-on-write yet).
class Geometry_graph : public erhe::graph::Graph
{
public:
    // Re-evaluates all nodes if the graph topology or any node parameter
    // changed since the last evaluation. Geometry operations are too
    // expensive to run every frame, so unlike Shader_graph this must only
    // do work when something changed.
    void evaluate_if_dirty();

    // Call when graph topology changes (node added / removed, link
    // connected / disconnected).
    void mark_dirty();

private:
    [[nodiscard]] auto is_evaluation_needed() const -> bool;
    void evaluate();

    bool m_dirty{true};
};

}
