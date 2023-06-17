#pragma once

#include "erhe/application/controller.hpp"

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
    : public erhe::scene::Node_attachment
{
public:
    Frame_controller();

    // Implements Node_attachment
    [[nodiscard]] static auto static_type     () -> uint64_t;
    [[nodiscard]] static auto static_type_name() -> const char*;
    [[nodiscard]] auto get_type () const -> uint64_t override;
    [[nodiscard]] auto type_name() const -> const char* override;
    void handle_node_update          (erhe::scene::Node* old_node, erhe::scene::Node* new_node);
    void handle_node_transform_update() override;

    // Public API
    void reset                  ();
    void update                 ();
    void update_fixed_step      ();
    void set_position           (const glm::vec3 position);
    void set_elevation          (const float value);
    void set_heading            (const float value);
    void get_transform_from_node(erhe::scene::Node* node);

    [[nodiscard]] auto get_position  () const -> glm::vec3;
    [[nodiscard]] auto get_elevation () const -> float;
    [[nodiscard]] auto get_heading   () const -> float;
    [[nodiscard]] auto get_axis_x    () const -> glm::vec3;
    [[nodiscard]] auto get_axis_y    () const -> glm::vec3;
    [[nodiscard]] auto get_axis_z    () const -> glm::vec3;
    [[nodiscard]] auto get_controller(const Control control) -> erhe::application::Controller&;

    erhe::application::Controller rotate_x;
    erhe::application::Controller rotate_y;
    erhe::application::Controller rotate_z;
    erhe::application::Controller translate_x;
    erhe::application::Controller translate_y;
    erhe::application::Controller translate_z;
    erhe::application::Controller speed_modifier;

private:
    float     m_elevation       {0.0f};
    float     m_heading         {0.0f};
    glm::mat4 m_heading_matrix  {1.0f};
    glm::mat4 m_rotation_matrix {1.0f};
    glm::vec3 m_position        {0.0f};
    bool      m_transform_update{false};
};

auto is_frame_controller(const erhe::scene::Item* item) -> bool;
auto is_frame_controller(const std::shared_ptr<erhe::scene::Item>& item) -> bool;
auto as_frame_controller(erhe::scene::Item* item) -> Frame_controller*;
auto as_frame_controller(const std::shared_ptr<erhe::scene::Item>& item) -> std::shared_ptr<Frame_controller>;

auto get_frame_controller(const erhe::scene::Node* node) -> std::shared_ptr<Frame_controller>;

} // namespace editor
