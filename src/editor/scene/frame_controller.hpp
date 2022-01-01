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

enum class Control : unsigned int
{
    translate_x = 0,
    translate_y = 1,
    translate_z = 2,
    rotate_x    = 3,
    rotate_y    = 4,
    rotate_z    = 5
};

class Frame_controller
{
public:
    Frame_controller();

    void set_node(erhe::scene::Node* node);

    [[nodiscard]] auto get_node() const -> erhe::scene::Node*;

    void clear            ();
    void update           ();
    void update_fixed_step();
    void set_position     (const glm::vec3 position);
    void set_elevation    (const float value);
    void set_heading      (const float value);

    [[nodiscard]] auto position      () const -> glm::vec3;
    [[nodiscard]] auto elevation     () const -> float;
    [[nodiscard]] auto heading       () const -> float;
    [[nodiscard]] auto right         () const -> glm::vec3;
    [[nodiscard]] auto up            () const -> glm::vec3;
    [[nodiscard]] auto back          () const -> glm::vec3;
    [[nodiscard]] auto get_controller(const Control control) -> Controller&;

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
