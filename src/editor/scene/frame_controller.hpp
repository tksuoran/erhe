#pragma once

#include "scene/camera_roll_monitor.hpp"

#include "erhe_math/input_axis.hpp"

#include "erhe_scene/node_attachment.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene { class Node; }

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
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::frame_controller; }

    // TODO disallow cloning
    auto clone() const -> std::shared_ptr<erhe::Item_base> override;
    void handle_node_update          (erhe::scene::Node* old_node, erhe::scene::Node* new_node) override;
    void handle_node_transform_update()                                                         override;

    // Public API
    void reset                  ();
    void update                 ();
    void update_fixed_step      ();
    void set_position           (glm::vec3 position);
    void set_orientation        (const glm::quat& orientation);
    void set_orientation        (const glm::mat4& orientation);
    // source names the caller for the unwanted-roll diagnostics (this readback is
    // where transforms written by anyone else enter the controller) and is unused
    // unless ERHE_CAMERA_ROLL_DIAGNOSTICS is on.
    void get_transform_from_node(erhe::scene::Node* node, const char* source = "Frame_controller::get_transform_from_node");

    void apply_rotation          (float rx, float ry, float rz);
    void apply_tumble            (glm::vec3 pivot, float rx, float ry, float rz);
    void set_active_control_value(Variable variable, float value);

    [[nodiscard]] auto get_position            () const -> glm::vec3;
    [[nodiscard]] auto get_orientation         () const -> glm::quat;
    [[nodiscard]] auto get_orientation_matrix  () const -> glm::mat4;
    [[nodiscard]] auto get_axis_x              () const -> glm::vec3;
    [[nodiscard]] auto get_axis_y              () const -> glm::vec3;
    [[nodiscard]] auto get_axis_z              () const -> glm::vec3;
    [[nodiscard]] auto get_variable            (Variable variable) -> erhe::math::Input_axis&;
    [[nodiscard]] auto get_active_control_value(Variable variable) const -> float;

#if ERHE_CAMERA_ROLL_DIAGNOSTICS
    // Unwanted-camera-roll diagnostics: every orientation write below is wrapped
    // in a Camera_roll_scope so a roll change can be attributed to its source.
    [[nodiscard]] auto get_roll_monitor() -> Camera_roll_monitor& { return m_roll_monitor; }
    [[nodiscard]] auto get_roll_monitor() const -> const Camera_roll_monitor& { return m_roll_monitor; }
    // Rotate the orientation back to zero roll around the current view direction.
    void level_roll();
#endif

    erhe::math::Input_axis rotate_x;
    erhe::math::Input_axis rotate_y;
    erhe::math::Input_axis rotate_z;
    erhe::math::Input_axis translate_x;
    erhe::math::Input_axis translate_y;
    erhe::math::Input_axis translate_z;
    float                  active_rotate_x{0.0f};
    float                  active_rotate_y{0.0f};
    float                  active_rotate_z{0.0f};
    float                  active_translate_x{0.0f};
    float                  active_translate_y{0.0f};
    float                  active_translate_z{0.0f};
    erhe::math::Input_axis speed_modifier;
    float move_speed{0.2f};

private:
    // The orientation is a quaternion, normalized after every composition, and it
    // is the only representation the controller keeps. A matrix member used to
    // hold it, which made a non-rotation basis expressible: create_rotation() was
    // fed the raw (unnormalized) matrix column as its axis, and neither the
    // write nor the readback through the node transform (mat4 -> quat_cast ->
    // toMat4) normalizes. In a captured session the deviation grew by ~1.5x per
    // fixed step until the camera basis had a determinant of 1.68 and tens of
    // degrees of roll.
    //
    // glm does not default-initialize, and both members are read by update()
    // before the first node is attached.
    glm::vec3 m_position{0.0f, 0.0f, 0.0f};
    glm::quat m_orientation{1.0f, 0.0f, 0.0f, 0.0f};
    bool      m_transform_update{false};
#if ERHE_CAMERA_ROLL_DIAGNOSTICS
    Camera_roll_monitor m_roll_monitor;
    // Drift watchdog. With a normalized quaternion this should never fire; it
    // stays as a regression guard for the failure mode described above.
    float               m_orthonormality_warn_threshold{1.0e-5f};
#endif
};

}
