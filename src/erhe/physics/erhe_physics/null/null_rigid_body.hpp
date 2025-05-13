#pragma once

#include "erhe_physics/null/null_world.hpp"
#include "erhe_physics/irigid_body.hpp"

#include <string>

namespace erhe::physics {

class Null_rigid_body
    : public IRigid_body
{
public:
    Null_rigid_body(
        Null_world&                    world,
        const IRigid_body_create_info& create_info,
        glm::vec3                      position    = glm::vec3{0.0f, 0.0f, 0.0f},
        glm::quat                      orientation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f}
    );
    ~Null_rigid_body() noexcept override;

    // Implements IRigid_body
    auto get_angular_damping         () const -> float                             override;
    auto get_angular_velocity        () const -> glm::vec3                         override;
    auto get_center_of_mass          () const -> glm::vec3                         override;
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
    auto get_world_transform         () const -> glm::mat4                         override;
    auto is_active                   () const -> bool                              override;
    auto get_allow_sleeping          () const -> bool                              override;

    void begin_move                  ()                                             override; // Disables deactivation
    void end_move                    ()                                             override; // Sets active, clears disable deactivation
    void set_angular_velocity        (const glm::vec3& velocity)                    override;
    void set_damping                 (float linear_damping, float angular_damping)  override;
    void set_friction                (float friction)                               override;
    void set_gravity_factor          (float gravity_factor)                         override;
    void set_linear_velocity         (const glm::vec3& velocity)                    override;
    void set_mass_properties         (float mass, const glm::mat4& local_inertia)   override;
    void set_motion_mode             (Motion_mode motion_mode)                      override;
    void set_restitution             (float restitution)                            override;
    void set_world_transform         (const Transform& transform)                   override;
    void set_allow_sleeping          (bool value)                                   override;
    void set_owner                   (void* owner)                                  override;
    auto get_owner                   () const -> void*                              override;

private:
    Null_world&                       m_world;
    std::shared_ptr<ICollision_shape> m_collision_shape;
    Transform                         m_transform;
    glm::vec3                         m_linear_velocity;
    glm::vec3                         m_angular_velocity;
    std::optional<float>              m_mass;
    Motion_mode                       m_motion_mode     {Motion_mode::e_static};
    float                             m_linear_damping  {0.05f};
    glm::mat4                         m_local_inertia   {0.0f};
    float                             m_angular_damping {0.05f};
    float                             m_friction        {0.5f};
    float                             m_gravity_factor  {1.0f};
    float                             m_restitution     {0.5f};
    std::string                       m_debug_label;
};

} // namespace erhe::physics
