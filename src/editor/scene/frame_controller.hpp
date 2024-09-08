#pragma once

#include "erhe_math/input_axis.hpp"

#include "erhe_scene/node_attachment.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene {
    class Node;
}

namespace editor {

enum class Variable : unsigned int {
    translate_x = 0,
    translate_y = 1,
    translate_z = 2,
    rotate_x    = 3,
    rotate_y    = 4,
    rotate_z    = 5
};

class Frame_controller : public erhe::Item<erhe::Item_base, erhe::scene::Node_attachment, Frame_controller, erhe::Item_kind::not_clonable>
{
public:
    explicit Frame_controller(const Frame_controller&);
    Frame_controller& operator=(const Frame_controller&);
    ~Frame_controller() noexcept override;

    Frame_controller();

    static constexpr std::string_view static_type_name{"Frame_controller"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;

    // TODO disallow cloning
    auto clone() const -> std::shared_ptr<erhe::Item_base> override;

    // Implements Node_attachment
    auto get_type                    () const -> uint64_t                                       override;
    auto get_type_name               () const -> std::string_view                               override;
    void handle_node_update          (erhe::scene::Node* old_node, erhe::scene::Node* new_node) override;
    void handle_node_transform_update()                                                         override;

    // Public API
    void reset                  ();
    void on_frame_begin         ();
    void on_frame_end           ();
    void update                 ();
    void tick                   (std::chrono::steady_clock::time_point timestamp);
    void set_position           (const glm::vec3 position);
    void set_orientation        (const glm::mat4& orientation);
    void get_transform_from_node(erhe::scene::Node* node);

    void apply_rotation(float rx, float ry, float rz);
    void apply_tumble  (glm::vec3 pivot, float rx, float ry, float rz);

    [[nodiscard]] auto get_position   () const -> glm::vec3;
    [[nodiscard]] auto get_orientation() const -> glm::mat4;
    [[nodiscard]] auto get_axis_x     () const -> glm::vec3;
    [[nodiscard]] auto get_axis_y     () const -> glm::vec3;
    [[nodiscard]] auto get_axis_z     () const -> glm::vec3;
    [[nodiscard]] auto get_variable   (const Variable variable) -> erhe::math::Input_axis&;
    [[nodiscard]] auto get_move_speed () const -> float;

    erhe::math::Input_axis rotate_x;
    erhe::math::Input_axis rotate_y;
    erhe::math::Input_axis rotate_z;
    erhe::math::Input_axis translate_x;
    erhe::math::Input_axis translate_y;
    erhe::math::Input_axis translate_z;
    erhe::math::Input_axis speed_modifier;
    float move_speed{1.0f};

private:
    glm::vec3 m_position;
    glm::mat4 m_orientation;
    bool      m_transform_update{false};
};

auto is_frame_controller(const erhe::Item_base* item) -> bool;
auto is_frame_controller(const std::shared_ptr<erhe::Item_base>& item) -> bool;

auto get_frame_controller(const erhe::scene::Node* node) -> std::shared_ptr<Frame_controller>;

} // namespace editor
