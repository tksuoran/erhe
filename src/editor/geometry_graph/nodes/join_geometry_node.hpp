#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace editor {

// Joins all geometries connected to the single input pin into one.
// Merging happens in Geometry_payload::operator+=() during input
// accumulation; a single connected geometry passes through unshared.
class Join_geometry_node : public Geometry_graph_node
{
public:
    Join_geometry_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
};

}
