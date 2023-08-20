#pragma once

#include "erhe_physics/irigid_body.hpp"

#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <string>

namespace erhe::physics
{

class Motion_state_adapter
    : public btMotionState
{
public:
    explicit Motion_state_adapter(IMotion_state* motion_state);

    void getWorldTransform(btTransform& worldTrans) const override;
    void setWorldTransform(const btTransform& worldTrans) override;

private:
    IMotion_state* m_motion_state{nullptr};
};

class Bullet_rigid_body
    : public IRigid_body
{
public:
    Bullet_rigid_body(
        const IRigid_body_create_info& create_info,
        IMotion_state*                 motion_state
    );
    ~Bullet_rigid_body() noexcept override;

    // Implements IRigid_body
    [[nodiscard]] auto get_angular_damping         () const -> float                             override;
    [[nodiscard]] auto get_angular_velocity        () const -> glm::vec3                         override;
    [[nodiscard]] auto get_center_of_mass_transform() const -> Transform                         override;
    [[nodiscard]] auto get_collision_shape         () const -> std::shared_ptr<ICollision_shape> override;
    [[nodiscard]] auto get_debug_label             () const -> const char*                       override;
    [[nodiscard]] auto get_friction                () const -> float                             override;
    [[nodiscard]] auto get_gravity_factor          () const -> float                             override;
    [[nodiscard]] auto get_linear_damping          () const -> float                             override;
    [[nodiscard]] auto get_linear_velocity         () const -> glm::vec3                         override;
    [[nodiscard]] auto get_local_inertia           () const -> glm::mat4                         override;
    [[nodiscard]] auto get_mass                    () const -> float                             override;
    [[nodiscard]] auto get_motion_mode             () const -> Motion_mode                       override;
    [[nodiscard]] auto get_restitution             () const -> float                             override;
    [[nodiscard]] auto get_world_transform         () const -> Transform                         override;

    void begin_move                  ()                                                         override; // Disables deactivation
    void end_move                    ()                                                         override; // Sets active, clears disable deactivation
    void move_world_transform        (const Transform transform, float delta_time)              override;
    void set_angular_velocity        (const glm::vec3 velocity)                                 override;
    void set_center_of_mass_transform(Transform transform)                                      override;
    void set_collision_shape         (const std::shared_ptr<ICollision_shape>& collision_shape) override;
    void set_damping                 (float linear_damping, float angular_damping)              override;
    void set_friction                (float friction)                                           override;
    void set_gravity_factor          (float gravity_factor)                                     override;
    void set_linear_velocity         (const glm::vec3 velocity)                                 override;
    void set_mass_properties         (float mass, const glm::mat4 local_inertia)                override;
    void set_motion_mode             (const Motion_mode motion_mode)                            override;
    void set_restitution             (float restitution)                                        override;
    void set_world_transform         (Transform transform)                                      override;

    // Public API
    [[nodiscard]] auto get_bullet_rigid_body() -> btRigidBody*;

private:
    Motion_state_adapter              m_motion_state_adapter;
    std::shared_ptr<ICollision_shape> m_collision_shape;
    btRigidBody                       m_bullet_rigid_body;
    Motion_mode                       m_motion_mode{Motion_mode::e_static};
    float                             m_gravity_factor{1.0};
    std::string                       m_debug_label;
};

} // namespace erhe::physics
