#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace editor {

// Identity node for tidying graph wiring - the geometry graph counterpart of
// the texture graph's Reroute: one geometry input, one geometry output,
// evaluate() forwards the input payload unchanged. The node preview shows the
// geometry flowing through, so it doubles as a wire probe.
class Passthrough_node : public Geometry_graph_node
{
public:
    Passthrough_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
};

}
