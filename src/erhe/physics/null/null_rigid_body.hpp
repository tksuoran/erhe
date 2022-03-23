#pragma once

#include "erhe/physics/irigid_body.hpp"

#include <string>

namespace erhe::physics
{

class Null_rigid_body
    : public IRigid_body
{
public:
    Null_rigid_body(
        const IRigid_body_create_info& create_info,
        IMotion_state*                 motion_state
    );
    ~Null_rigid_body() noexcept override;

    // Implements IRigid_body
    [[nodiscard]] auto get_motion_mode             () const -> Motion_mode                       override;
    [[nodiscard]] auto get_collision_shape         () const -> std::shared_ptr<ICollision_shape> override;
    [[nodiscard]] auto get_friction                () const -> float                             override;
    [[nodiscard]] auto get_rolling_friction        () const -> float                             override;
    [[nodiscard]] auto get_restitution             () const -> float                             override;
    [[nodiscard]] auto get_linear_damping          () const -> float                             override;
    [[nodiscard]] auto get_angular_damping         () const -> float                             override;
    [[nodiscard]] auto get_local_inertia           () const -> glm::mat4                         override;
    [[nodiscard]] auto get_center_of_mass_transform() const -> Transform                         override;
    [[nodiscard]] auto get_mass                    () const -> float                             override;
    [[nodiscard]] auto get_debug_label             () const -> const char*                       override;

    void set_motion_mode             (const Motion_mode motion_mode)                           override;
    void set_collision_shape         (const std::shared_ptr<ICollision_shape>& collision_shape)override;
    void set_friction                (const float friction)                                    override;
    void set_rolling_friction        (const float rolling_friction)                            override;
    void set_restitution             (const float restitution)                                 override;
    void set_center_of_mass_transform(const Transform transform)                               override;
    void set_world_transform         (const Transform transform)                               override;
    void set_linear_velocity         (const glm::vec3 velocity)                                override;
    void set_angular_velocity        (const glm::vec3 velocity)                                override;
    void set_damping                 (const float linear_damping, const float angular_damping) override;
    void set_mass_properties         (const float mass, const glm::mat4 local_inertia)         override;
    void begin_move                  ()                                                        override; // Disables deactivation
    void end_move                    ()                                                        override; // Sets active, clears disable deactivation

private:
    std::shared_ptr<ICollision_shape> m_collision_shape;
    Transform                         m_transform;
    glm::vec3                         m_linear_velocity;
    glm::vec3                         m_angular_velocity;
    float                             m_mass            {1.0f};
    glm::mat4                         m_local_inertia   {0.0f};
    Motion_mode                       m_motion_mode     {Motion_mode::e_kinematic};
    float                             m_linear_damping  {0.05f};
    float                             m_angular_damping {0.05f};
    float                             m_friction        {0.5f};
    float                             m_rolling_friction{0.1f};
    float                             m_restitution     {0.5f};
    std::string                       m_debug_label;
};

} // namespace erhe::physics
