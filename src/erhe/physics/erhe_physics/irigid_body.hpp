#pragma once

#include "erhe_physics/transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <memory>
#include <optional>
#include <string>

namespace erhe::scene {
    class Node;
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

class ICollision_shape;
class IWorld;

class IRigid_body_create_info
{
public:
    float                             friction         {0.5f};
    float                             restitution      {0.2f};
    float                             linear_damping   {0.05f};
    float                             angular_damping  {0.05f};
    std::shared_ptr<ICollision_shape> collision_shape  {};
    std::optional<float>              density          {};
    std::optional<float>              mass             {};
    std::optional<glm::mat4>          inertia_override {};
    std::string                       debug_label      {};
    bool                              enable_collisions{true};
    erhe::physics::Motion_mode        motion_mode      {Motion_mode::e_dynamic};
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
    virtual void begin_move          ()                                             = 0;
    virtual void end_move            ()                                             = 0;
    virtual void set_angular_velocity(const glm::vec3& velocity)                    = 0;
    virtual void set_damping         (float linear_damping, float angular_damping)  = 0;
    virtual void set_friction        (float friction)                               = 0;
    virtual void set_gravity_factor  (float gravity_factor)                         = 0;
    virtual void set_linear_velocity (const glm::vec3& velocity)                    = 0;
    virtual void set_mass_properties (float mass, const glm::mat4& local_inertia)   = 0;
    virtual void set_motion_mode     (Motion_mode motion_mode)                      = 0;
    virtual void set_restitution     (float restitution)                            = 0;
    virtual void set_world_transform (const Transform& transform)                   = 0;
    virtual void set_allow_sleeping  (bool value)                                   = 0;
    virtual void set_owner           (void* owner)                                  = 0;
    virtual auto get_owner           () const -> void*                              = 0;
};

} // namespace erhe::physics
