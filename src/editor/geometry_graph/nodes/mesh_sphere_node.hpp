#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace editor {

class Mesh_sphere_node : public Geometry_graph_node
{
public:
    Mesh_sphere_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    float m_radius        {1.0f};
    int   m_slice_count   {32};
    int   m_stack_division{16};
};

}
