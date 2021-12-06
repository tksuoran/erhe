#pragma once

#include "scene/controller.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene
{
    class Node;
}

namespace editor
{

class Frame_controller
{
public:
    Frame_controller();

    void set_frame        (erhe::scene::Node* node);
    auto node             () const -> erhe::scene::Node*;
    void clear            ();
    void update           ();
    void update_fixed_step();
    void set_position     (const glm::vec3 position);
    void set_elevation    (const float value);
    void set_heading      (const float value);
    auto position         () const -> glm::vec3;
    auto elevation        () const -> float;
    auto heading          () const -> float;
    auto right            () const -> glm::vec3;
    auto up               () const -> glm::vec3;
    auto back             () const -> glm::vec3;

    Controller rotate_x;
    Controller rotate_y;
    Controller rotate_z;
    Controller translate_x;
    Controller translate_y;
    Controller translate_z;
    Controller speed_modifier;

private:
    erhe::scene::Node* m_node           {nullptr};
    float              m_elevation      {0.0f};
    float              m_heading        {0.0f};
    glm::mat4          m_heading_matrix {1.0f};
    glm::mat4          m_rotation_matrix{1.0f};
    glm::vec3          m_position       {0.0f};
};

} // namespace editor
