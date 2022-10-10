#pragma once

#include "erhe/physics/irigid_body.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>

namespace JPH
{
    class Body;
    class BodyInterface;
}

namespace erhe::physics
{

class Jolt_collision_shape;

class Jolt_rigid_body
    : public IRigid_body
{
public:
    Jolt_rigid_body(
        const IRigid_body_create_info& create_info,
        IMotion_state*                 motion_state
    );
    ~Jolt_rigid_body() noexcept override;

    // Implements IRigid_body
    auto get_angular_damping         () const -> float                             override;
    auto get_angular_velocity        () const -> glm::vec3                         override;
    auto get_center_of_mass_transform() const -> Transform                         override;
    auto get_collision_shape         () const -> std::shared_ptr<ICollision_shape> override;
    auto get_debug_label             () const -> const char*                       override;
    auto get_friction                () const -> float                             override;
    auto get_gravity_factor          () const -> float                             override;
    auto get_linear_damping          () const -> float                             override;
    auto get_linear_velocity         () const -> glm::vec3                         override;
    auto get_local_inertia           () const -> glm::mat4                         override;
    auto get_mass                    () const -> float                             override;
    auto get_motion_mode             () const -> Motion_mode                       override;
    auto get_restitution             () const -> float                             override;
    auto get_world_transform         () const -> Transform                         override;

    void begin_move                  ()                                                        override; // Disables deactivation
    void end_move                    ()                                                        override; // Sets active, clears disable deactivation
    void move_world_transform        (const Transform transform, float delta_time)             override;
    void set_angular_velocity        (const glm::vec3 velocity)                                override;
    void set_center_of_mass_transform(const Transform transform)                               override;
    void set_collision_shape         (const std::shared_ptr<ICollision_shape>& collision_shape)override;
    void set_damping                 (const float linear_damping, const float angular_damping) override;
    void set_friction                (const float friction)                                    override;
    void set_gravity_factor          (const float gravity_factor)                              override;
    void set_linear_velocity         (const glm::vec3 velocity)                                override;
    void set_mass_properties         (const float mass, const glm::mat4 local_inertia)         override;
    void set_motion_mode             (const Motion_mode motion_mode)                           override;
    void set_restitution             (const float restitution)                                 override;
    void set_world_transform         (const Transform transform)                               override;

    // Public API
    auto get_jolt_body          () const -> JPH::Body*;
    void pre_update_motion_state() const;
    void update_motion_state    () const;

private:
    JPH::Body*                            m_body            {nullptr};
    IMotion_state*                        m_motion_state    {nullptr};
    JPH::BodyInterface&                   m_body_interface;
    std::shared_ptr<Jolt_collision_shape> m_collision_shape;
    float                                 m_mass            {1.0f};
    glm::mat4                             m_local_inertia   {0.0f};
    Motion_mode                           m_motion_mode     {Motion_mode::e_kinematic_non_physical};
    float                                 m_friction        {0.5f};
    float                                 m_gravity_factor  {1.0f};
    float                                 m_restitution     {1.0f};
    float                                 m_linear_damping  {0.05f};
    float                                 m_angular_damping {0.05f};
    std::string                           m_debug_label;
};

} // namespace erhe::physics
