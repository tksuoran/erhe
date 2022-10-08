#pragma once

#include "erhe/physics/transform.hpp"
#include "erhe/physics/imotion_state.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene
{
    class Node;
}

namespace erhe::physics
{

class ICollision_shape;
class IMotion_state;
class IWorld;

class IRigid_body_create_info
{
public:
    IWorld&                           world;
    float                             mass           {1.0f};
    float                             friction       {0.5f};
    float                             restitution    {0.5f};
    float                             linear_damping {0.05f};
    float                             angular_damping{0.05f};
    std::shared_ptr<ICollision_shape> collision_shape;
    glm::mat4                         local_inertia  {0.0f};
    const char*                       debug_label    {nullptr};
};

class IRigid_body
{
public:
    virtual ~IRigid_body() noexcept;

    [[nodiscard]] static auto create_rigid_body(
        const IRigid_body_create_info& create_info,
        IMotion_state*                 motion_state
    ) -> IRigid_body*;

    [[nodiscard]] static auto create_rigid_body_shared(
        const IRigid_body_create_info& create_info,
        IMotion_state*                 motion_state
    ) -> std::shared_ptr<IRigid_body>;

    [[nodiscard]] virtual auto get_motion_mode             () const -> Motion_mode                       = 0;
    [[nodiscard]] virtual auto get_collision_shape         () const -> std::shared_ptr<ICollision_shape> = 0;
    [[nodiscard]] virtual auto get_friction                () const -> float                             = 0;
    [[nodiscard]] virtual auto get_rolling_friction        () const -> float                             = 0;
    [[nodiscard]] virtual auto get_restitution             () const -> float                             = 0;
    [[nodiscard]] virtual auto get_linear_damping          () const -> float                             = 0;
    [[nodiscard]] virtual auto get_angular_damping         () const -> float                             = 0;
    [[nodiscard]] virtual auto get_local_inertia           () const -> glm::mat4                         = 0;
    [[nodiscard]] virtual auto get_center_of_mass_transform() const -> Transform                         = 0;
    [[nodiscard]] virtual auto get_world_transform         () const -> Transform                         = 0;
    [[nodiscard]] virtual auto get_mass                    () const -> float                             = 0;
    [[nodiscard]] virtual auto get_debug_label             () const -> const char*                       = 0;
    virtual void set_motion_mode             (const Motion_mode motion_mode)                            = 0;
    virtual void set_collision_shape         (const std::shared_ptr<ICollision_shape>& collision_shape) = 0;
    virtual void set_friction                (float friction)                                           = 0;
    virtual void set_rolling_friction        (float friction)                                           = 0;
    virtual void set_restitution             (float restitution)                                        = 0;
    virtual void set_center_of_mass_transform(const Transform transform)                                = 0;
    virtual void set_world_transform         (const Transform transform)                                = 0;
    virtual void move_world_transform        (const Transform transform, float delta_time)              = 0;
    virtual void set_linear_velocity         (const glm::vec3 velocity)                                 = 0;
    virtual void set_angular_velocity        (const glm::vec3 velocity)                                 = 0;
    virtual void set_damping                 (const float linear_damping, const float angular_damping)  = 0;
    virtual void set_mass_properties         (const float mass, const glm::mat4 local_inertia)          = 0;
    virtual void begin_move                  ()                                                         = 0;
    virtual void end_move                    ()                                                         = 0;
};

} // namespace erhe::physics
