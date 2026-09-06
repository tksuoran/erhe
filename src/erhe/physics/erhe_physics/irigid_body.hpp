#pragma once

#include "erhe_property/enum_info.hpp"

#include "erhe_physics/transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <memory>
#include <optional>
#include <string>

namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace erhe::physics {

enum class Motion_mode : unsigned int {
    e_invalid                = 0,
    e_static                 = 1, // immovable
    e_kinematic_non_physical = 2, // movable from scene graph (instant, does not create kinetic energy), not movable from physics simulation
    e_kinematic_physical     = 3, // movable from scene graph (creates kinetic energy), "not" movable from physics simulation
    e_dynamic                = 4  // not movable from scene graph, movable from physics simulation
};

static constexpr const char* c_motion_mode_strings[] = {
    "Invalid",
    "Static",
    "Kinematic Non-Physical",
    "Kinematic Physical",
    "Dynamic"
};

[[nodiscard]] inline auto c_str(Motion_mode motion_mode) -> const char*
{
    return c_motion_mode_strings[static_cast<int>(motion_mode)];
}

// The authorable motion modes (e_invalid is not one), for a registered
// property of Motion_mode type (doc/property-system.md D2a).
extern const erhe::property::Enum_info c_motion_mode_enum_info;

class Collision_filter;
class ICollision_shape;
class IWorld;
class Physics_material;

// What a body is created from. Damping, wind receptivity and density are
// not here: the physics material carries them (Physics_material), and a
// body without a material uses the material defaults. Without an explicit
// mass the body's mass is its shape volume times the material density.
class IRigid_body_create_info
{
public:
    std::shared_ptr<ICollision_shape> collision_shape  {};
    std::optional<float>              mass             {};
    std::optional<glm::mat4>          inertia_override {};
    std::string                       debug_label      {};
    bool                              enable_collisions{true};
    erhe::physics::Motion_mode        motion_mode      {Motion_mode::e_dynamic};
    glm::vec3                         position         {0.0f, 0.0f, 0.0f};
    glm::quat                         orientation      {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3                         linear_velocity  {0.0f, 0.0f, 0.0f}; // world space, applied at creation
    glm::vec3                         angular_velocity {0.0f, 0.0f, 0.0f}; // world space, applied at creation
    float                             gravity_factor   {1.0f};
    bool                              is_sensor        {false};
    std::shared_ptr<Physics_material> physics_material {}; // shared material, the carrier of friction, restitution, damping, wind receptivity and density; none = the material defaults
    std::shared_ptr<Collision_filter> collision_filter {}; // shared collision-system filter
};

class IRigid_body
{
public:
    virtual ~IRigid_body() noexcept;

    [[nodiscard]] virtual auto get_angular_damping         () const -> float                             = 0;
    [[nodiscard]] virtual auto get_angular_velocity        () const -> glm::vec3                         = 0;
    [[nodiscard]] virtual auto get_center_of_mass          () const -> glm::vec3                         = 0;
    [[nodiscard]] virtual auto get_center_of_mass_transform() const -> Transform                         = 0;
    [[nodiscard]] virtual auto get_collision_shape         () const -> std::shared_ptr<ICollision_shape> = 0;
    [[nodiscard]] virtual auto get_debug_label             () const -> const char*                       = 0;
    [[nodiscard]] virtual auto get_friction                () const -> float                             = 0;
    [[nodiscard]] virtual auto get_gravity_factor          () const -> float                             = 0;
    [[nodiscard]] virtual auto get_linear_damping          () const -> float                             = 0;
    [[nodiscard]] virtual auto get_linear_velocity         () const -> glm::vec3                         = 0;
    [[nodiscard]] virtual auto get_local_inertia           () const -> glm::mat4                         = 0;
    [[nodiscard]] virtual auto get_mass                    () const -> float                             = 0;
    [[nodiscard]] virtual auto get_motion_mode             () const -> Motion_mode                       = 0;
    [[nodiscard]] virtual auto get_restitution             () const -> float                             = 0;
    [[nodiscard]] virtual auto get_world_transform         () const -> glm::mat4                         = 0;
    [[nodiscard]] virtual auto is_active                   () const -> bool                              = 0;
    [[nodiscard]] virtual auto get_allow_sleeping          () const -> bool                              = 0;
    [[nodiscard]] virtual auto get_physics_material        () const -> std::shared_ptr<Physics_material> = 0;
    [[nodiscard]] virtual auto get_collision_filter        () const -> std::shared_ptr<Collision_filter> = 0;
    virtual void begin_move          ()                                             = 0;
    virtual void end_move            ()                                             = 0;
    // External force application, world space. Forces and torques accumulate
    // for the next simulation step and are cleared after it (call every fixed
    // step for a sustained push, e.g. wind); impulses change velocity
    // immediately. Overloads without a point act at the center of mass.
    // The body is activated (woken) so the input takes effect; applying to a
    // non-dynamic body is a no-op.
    virtual void apply_force         (const glm::vec3& force)                            = 0;
    virtual void apply_force_at      (const glm::vec3& force, const glm::vec3& point)    = 0;
    virtual void apply_torque        (const glm::vec3& torque)                           = 0;
    virtual void apply_impulse       (const glm::vec3& impulse)                          = 0;
    virtual void apply_impulse_at    (const glm::vec3& impulse, const glm::vec3& point)  = 0;
    virtual void set_angular_velocity(const glm::vec3& velocity)                    = 0;
    virtual void set_damping         (float linear_damping, float angular_damping)  = 0;
    virtual void set_friction        (float friction)                               = 0;
    virtual void set_gravity_factor  (float gravity_factor)                         = 0;
    virtual void set_linear_velocity (const glm::vec3& velocity)                    = 0;
    virtual void set_mass_properties (float mass, const glm::mat4& local_inertia)   = 0;
    virtual void set_motion_mode     (Motion_mode motion_mode)                      = 0;
    virtual void set_restitution     (float restitution)                            = 0;
    virtual void set_world_transform (const Transform& transform)                   = 0;
    // Instantaneous, motion-mode-independent placement that induces no velocity.
    // Unlike set_world_transform(), which for e_kinematic_physical bodies uses
    // MoveKinematic() (converting the position delta into a velocity), teleport()
    // always sets the pose directly without activation. Use it to snap a body to a
    // new authored pose (e.g. after a joint create/flip or an editor move) so the
    // simulation does not react to the jump with a corrective impulse.
    virtual void teleport            (const Transform& transform)                   = 0;
    virtual void set_allow_sleeping  (bool value)                                   = 0;
    virtual void set_owner           (void* owner)                                  = 0;
    [[nodiscard]] virtual auto get_owner() const -> void*                           = 0;

    // Material / filter assignment. Must be called outside
    // IWorld::update_fixed_step(): the backend snapshot is read without locks
    // by contact callbacks running on physics worker threads during update.
    // Setting the material also applies its damping to the body and, while
    // the body has no explicit mass, re-derives the mass from its density.
    virtual void set_physics_material(const std::shared_ptr<Physics_material>& material) = 0;
    virtual void set_collision_filter(const std::shared_ptr<Collision_filter>& filter)   = 0;
};

} // namespace erhe::physics
