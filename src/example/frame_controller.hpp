#pragma once

#include "erhe_math/input_axis.hpp"

#include "erhe_scene/transform_observer.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace example {

enum class Control : unsigned int {
    translate_x = 0,
    translate_y = 1,
    translate_z = 2,
    rotate_x    = 3,
    rotate_y    = 4,
    rotate_z    = 5
};

// The example's camera controller: a plain object owned by the application,
// naming its node by weak reference and following that node's transform
// through a transform observer token (D7 of
// doc/plans/node_attachments_to_properties.md).
class Frame_controller
{
public:
    Frame_controller();
    ~Frame_controller() noexcept;
    Frame_controller(const Frame_controller&)            = delete;
    Frame_controller& operator=(const Frame_controller&) = delete;

    void set_node(const std::shared_ptr<erhe::scene::Node>& node);
    void set_node(erhe::scene::Node* node);
    [[nodiscard]] auto get_node() const -> erhe::scene::Node*;

    // Public API
    void reset                  ();
    void update_transform       ();
    void update_fixed_step      ();
    void set_position           (glm::vec3 position);
    void set_elevation          (float value);
    void set_heading            (float value);
    void get_transform_from_node(erhe::scene::Node* node);

    [[nodiscard]] auto get_position  () const -> glm::vec3;
    [[nodiscard]] auto get_elevation () const -> float;
    [[nodiscard]] auto get_heading   () const -> float;
    [[nodiscard]] auto get_axis_x    () const -> glm::vec3;
    [[nodiscard]] auto get_axis_y    () const -> glm::vec3;
    [[nodiscard]] auto get_axis_z    () const -> glm::vec3;
    [[nodiscard]] auto get_controller(Control control) -> erhe::math::Input_axis&;

    erhe::math::Input_axis rotate_x;
    erhe::math::Input_axis rotate_y;
    erhe::math::Input_axis rotate_z;
    erhe::math::Input_axis translate_x;
    erhe::math::Input_axis translate_y;
    erhe::math::Input_axis translate_z;
    erhe::math::Input_axis speed_modifier;

private:
    std::weak_ptr<erhe::scene::Node>      m_node;
    erhe::scene::Transform_observer_token m_transform_observer;
    float     m_elevation       {0.0f};
    float     m_heading         {0.0f};
    glm::mat4 m_heading_matrix  {1.0f};
    glm::mat4 m_rotation_matrix {1.0f};
    glm::vec3 m_position        {0.0f};
    bool      m_transform_update{false};
};

} // namespace example
