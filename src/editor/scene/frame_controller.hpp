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

    void set_frame(erhe::scene::Node* value);

    auto node() -> erhe::scene::Node*;

    void clear();

    void update();

    void update_fixed_step();

    void set_position(glm::vec3 position);

    void set_elevation(float value);

    void set_heading(float value);

    auto position() const -> glm::vec3;

    auto elevation() const -> float;

    auto heading() const -> float;

    auto right() const -> glm::vec3;

    auto up() const -> glm::vec3;

    auto back() const -> glm::vec3;
    //glm::mat4 const   &parent_from_local() const { return m_parent_from_local; }
    //glm::mat4 const   &local_from_parent() const { return m_local_from_parent; }

    Controller rotate_x;
    Controller rotate_y;
    Controller rotate_z;
    Controller translate_x;
    Controller translate_y;
    Controller translate_z;
    Controller speed_modifier;

private:
    erhe::scene::Node* m_frame          {nullptr};
    float              m_elevation      {0.0f};
    float              m_heading        {0.0f};
    glm::mat4          m_heading_matrix {1.0f};
    glm::mat4          m_rotation_matrix{1.0f};
    glm::vec3          m_position       {0.0f};
};

} // namespace editor
