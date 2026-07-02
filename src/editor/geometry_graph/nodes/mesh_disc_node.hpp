#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace editor {

class Mesh_disc_node : public Geometry_graph_node
{
public:
    Mesh_disc_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;

private:
    float m_outer_radius{1.0f};
    float m_inner_radius{0.0f};
    int   m_slice_count {32};
    int   m_stack_count {1};
};

}
