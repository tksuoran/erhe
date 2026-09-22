#pragma once

#include "scene/camera_roll_monitor.hpp"

#include "erhe_math/input_axis.hpp"

#include "erhe_scene/transform_observer.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>

namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

enum class Variable : unsigned int {
    translate_x = 0,
    translate_y = 1,
    translate_z = 2,
    rotate_x    = 3,
    rotate_y    = 4,
    rotate_z    = 5
};

// The space the zoom glide direction is held in. 'world' keeps the direction
// captured at the wheel step, so turning the camera during a glide does not
// change where the glide goes. 'view' keeps the direction relative to the
// view, so it turns with the camera.
enum class Zoom_direction_space : unsigned int {
    world = 0,
    view  = 1
};

// The 6DOF camera pose a tool drives: input axes, a position and an
// orientation, written into the node it is pointed at and read back from it
// whenever anyone else writes that node's transform
// (doc/erhe/scene.md "Transform observers"). A plain object owned by its
// tool, never an item in the scene: it names its node by weak_ptr, so it
// keeps no camera of a closed scene alive, and follows that node's transform
// through a transform observer token.
class Frame_controller
{
public:
    Frame_controller();
    ~Frame_controller() noexcept;
    Frame_controller(const Frame_controller&)            = delete;
    Frame_controller& operator=(const Frame_controller&) = delete;

    // The node whose transform the controller drives. A null node leaves the
    // controller inert; the controller adopts the node's world transform.
    void set_node(const std::shared_ptr<erhe::scene::Node>& node);
    void set_node(erhe::scene::Node* node);
    [[nodiscard]] auto get_node() const -> erhe::scene::Node*;

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

    // Sets the axis the zoom channel moves the camera along. The vector is
    // given in world space and is normalized here; it is stored in the given
    // space and read back by update_fixed_step().
    void set_zoom_direction      (glm::vec3 direction_in_world, Zoom_direction_space space);

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
    // The mouse wheel channel. It is separate from translate_z so that key /
    // controller motion along the view axis and wheel motion along the zoom
    // direction run at the same time, each along its own axis.
    erhe::math::Input_axis zoom;
    float                  active_rotate_x{0.0f};
    float                  active_rotate_y{0.0f};
    float                  active_rotate_z{0.0f};
    float                  active_translate_x{0.0f};
    float                  active_translate_y{0.0f};
    float                  active_translate_z{0.0f};
    erhe::math::Input_axis speed_modifier;
    float move_speed{0.2f};

private:
    // The node the controller drives, held weakly so it never keeps a camera
    // of a closed scene alive, and the subscription that reads a transform
    // written by anyone else back into the pose.
    std::weak_ptr<erhe::scene::Node>      m_node;
    erhe::scene::Transform_observer_token m_transform_observer;
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
    // before the first node is set.
    glm::vec3 m_position{0.0f, 0.0f, 0.0f};
    glm::quat m_orientation{1.0f, 0.0f, 0.0f, 0.0f};
    // Unit vector in the space named by m_zoom_direction_space. The default is
    // the camera forward axis in view space, so a zoom before any
    // set_zoom_direction() call moves the camera the way the view axis does.
    glm::vec3 m_zoom_direction{0.0f, 0.0f, -1.0f};
    Zoom_direction_space m_zoom_direction_space{Zoom_direction_space::view};
    bool      m_transform_update{false};
#if ERHE_CAMERA_ROLL_DIAGNOSTICS
    Camera_roll_monitor m_roll_monitor;
    // Drift watchdog. With a normalized quaternion this should never fire; it
    // stays as a regression guard for the failure mode described above.
    float               m_orthonormality_warn_threshold{1.0e-5f};
#endif
};

}
