#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace editor {

class Subdivide_node : public Geometry_graph_node
{
public:
    enum class Mode : int {
        catmull_clark = 0,
        sqrt3         = 1
    };

    Subdivide_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;

private:
    Mode m_mode      {Mode::catmull_clark};
    int  m_iterations{1};
};

}
