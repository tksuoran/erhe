#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include <glm/glm.hpp>

namespace editor {

class Mesh_box_node : public Geometry_graph_node
{
public:
    Mesh_box_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;

private:
    glm::vec3  m_size {1.0f, 1.0f, 1.0f};
    glm::ivec3 m_steps{1, 1, 1};
    float      m_power{1.0f};
};

}
