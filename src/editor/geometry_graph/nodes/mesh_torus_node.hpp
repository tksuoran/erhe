#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace editor {

class Mesh_torus_node : public Geometry_graph_node
{
public:
    Mesh_torus_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    float m_major_radius{1.0f};
    float m_minor_radius{0.25f};
    int   m_major_steps {32};
    int   m_minor_steps {16};
};

}
