#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace editor {

class Mesh_cone_node : public Geometry_graph_node
{
public:
    Mesh_cone_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    float m_height        {1.0f};
    float m_bottom_radius {0.5f};
    bool  m_use_bottom    {true};
    int   m_slice_count   {32};
    int   m_stack_division{1};
};

}
