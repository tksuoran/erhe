#pragma once

#include "erhe_math/input_axis.hpp"

#include "erhe_scene/node_attachment.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace example {

enum class Control : unsigned int {
    translate_x = 0,
    translate_y = 1,
    translate_z = 2,
    rotate_x    = 3,
    rotate_y    = 4,
    rotate_z    = 5
};

class Frame_controller : public erhe::scene::Node_attachment
{
public:
    Frame_controller();

    static constexpr std::string_view static_type_name{"Frame_controller"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;

    // Implements Item_base
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    // Implements / overrides Node_attachment
    void handle_node_update          (erhe::scene::Node* old_node, erhe::scene::Node* new_node) override;
    void handle_node_transform_update()                                                         override;

    // Public API
    void reset                  ();
    void update_transform       ();
    void tick                   (std::chrono::steady_clock::time_point timestamp);
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
    [[nodiscard]] auto get_controller(const Control control) -> erhe::math::Input_axis&;

    erhe::math::Input_axis rotate_x;
    erhe::math::Input_axis rotate_y;
    erhe::math::Input_axis rotate_z;
    erhe::math::Input_axis translate_x;
    erhe::math::Input_axis translate_y;
    erhe::math::Input_axis translate_z;
    erhe::math::Input_axis speed_modifier;

private:
    float     m_elevation       {0.0f};
    float     m_heading         {0.0f};
    glm::mat4 m_heading_matrix  {1.0f};
    glm::mat4 m_rotation_matrix {1.0f};
    glm::vec3 m_position        {0.0f};
    bool      m_transform_update{false};
};

auto is_frame_controller(const erhe::Item_base* item) -> bool;
auto is_frame_controller(const std::shared_ptr<erhe::Item_base>& item) -> bool;
auto get_frame_controller(const erhe::scene::Node* node) -> std::shared_ptr<Frame_controller>;

} // namespace example
