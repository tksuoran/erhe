#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace editor {

// CSG boolean of two geometries (experimental Geogram backend).
class Boolean_node : public Geometry_graph_node
{
public:
    enum class Boolean_operation : int {
        union_operation = 0,
        intersection    = 1,
        difference      = 2
    };

    Boolean_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;

private:
    Boolean_operation m_operation{Boolean_operation::union_operation};
};

}
