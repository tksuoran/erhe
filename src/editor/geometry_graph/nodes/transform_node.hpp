#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include <glm/glm.hpp>

namespace editor {

class Transform_node : public Geometry_graph_node
{
public:
    Transform_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;

private:
    glm::vec3 m_translation     {0.0f, 0.0f, 0.0f};
    glm::vec3 m_rotation_degrees{0.0f, 0.0f, 0.0f};
    glm::vec3 m_scale           {1.0f, 1.0f, 1.0f};
};

}
