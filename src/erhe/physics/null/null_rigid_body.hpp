#pragma once

#include "erhe/physics/irigid_body.hpp"

namespace erhe::physics
{

class Null_rigid_body
    : public IRigid_body
{
public:
    Null_rigid_body(
        IRigid_body_create_info& create_info,
        IMotion_state*           motion_state
    );
    ~Null_rigid_body() override;

    // Implements IRigid_body
    void set_motion_mode             (const Motion_mode motion_mode)                           override;
    auto get_motion_mode             () const -> Motion_mode                                   override;
    void set_collision_shape         (const std::shared_ptr<ICollision_shape>& collision_shape)override;
    auto get_collision_shape         () const -> std::shared_ptr<ICollision_shape>             override;
    auto get_friction                () const -> float                                         override;
    void set_friction                (const float friction)                                    override;
    auto get_rolling_friction        () const -> float                                         override;
    void set_rolling_friction        (const float rolling_friction)                            override;
    auto get_restitution             () const -> float                                         override;
    void set_restitution             (const float restitution)                                 override;
    void set_center_of_mass_transform(const Transform transform)                               override;
    void set_world_transform         (const Transform transform)                               override;
    void set_linear_velocity         (const glm::vec3 velocity)                                override;
    void set_angular_velocity        (const glm::vec3 velocity)                                override;
    auto get_linear_damping          () const -> float                                         override;
    auto get_angular_damping         () const -> float                                         override;
    void set_damping                 (const float linear_damping, const float angular_damping) override;
    auto get_local_inertia           () const -> glm::vec3                                     override;
    auto get_mass                    () const -> float                                         override;
    void set_mass_properties         (const float mass, const glm::vec3 local_inertia)         override;
    void begin_move                  ()                                                        override; // Disables deactivation
    void end_move                    ()                                                        override; // Sets active, clears disable deactivation

private:
    std::shared_ptr<ICollision_shape> m_collision_shape;
    Transform                         m_transform;
    glm::vec3                         m_linear_velocity;
    glm::vec3                         m_angular_velocity;
    float                             m_mass{1.0f};
    glm::vec3                         m_local_inertia{0.0f, 0.0f, 0.0f};
    Motion_mode                       m_motion_mode{Motion_mode::e_kinematic};
    float                             m_linear_damping{0.02f};
    float                             m_angular_damping{0.02f};
    float                             m_friction{0.5f};
    float                             m_rolling_friction{0.1f};
    float                             m_restitution{1.0f};
};

} // namespace erhe::physics
