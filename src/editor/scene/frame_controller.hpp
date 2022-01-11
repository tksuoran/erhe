#pragma once

#include "scene/controller.hpp"

#include "erhe/scene/node.hpp"

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
    : public erhe::scene::INode_attachment
{
public:
    Frame_controller();

    // Implements INode_attachment
    [[nodiscard]] auto node_attachment_type() const -> const char* override;
    void on_node_transform_changed() override;

    // Public API
    void reset            ();
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
    float     m_elevation       {0.0f};
    float     m_heading         {0.0f};
    glm::mat4 m_heading_matrix  {1.0f};
    glm::mat4 m_rotation_matrix {1.0f};
    glm::vec3 m_position        {0.0f};
    bool      m_transform_update{false};
};

auto is_frame_controller(const erhe::scene::INode_attachment* const attachment) -> bool;
auto is_frame_controller(const std::shared_ptr<erhe::scene::INode_attachment>& attachment) -> bool;
auto as_frame_controller(erhe::scene::INode_attachment* attachment) -> Frame_controller*;
auto as_frame_controller(const std::shared_ptr<erhe::scene::INode_attachment>& attachment) -> std::shared_ptr<Frame_controller>;

auto get_frame_controller(const erhe::scene::Node* node) -> std::shared_ptr<Frame_controller>;

} // namespace editor
