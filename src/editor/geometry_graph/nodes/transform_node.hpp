#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include <glm/glm.hpp>

namespace editor {

class Transform_node : public Geometry_graph_node
{
public:
    enum class Rotation_mode : int {
        euler_degrees = 0,
        quaternion    = 1
    };

    Transform_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    glm::vec3     m_translation        {0.0f, 0.0f, 0.0f};
    Rotation_mode m_rotation_mode      {Rotation_mode::euler_degrees};
    glm::vec3     m_rotation_degrees   {0.0f, 0.0f, 0.0f};
    glm::vec4     m_rotation_quaternion{0.0f, 0.0f, 0.0f, 1.0f}; // [x, y, z, w]
    glm::vec3     m_scale              {1.0f, 1.0f, 1.0f};
};

}
