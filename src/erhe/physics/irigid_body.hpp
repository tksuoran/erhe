#pragma once

#include "erhe/physics/transform.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene
{
    class Node;
}

namespace erhe::physics
{

enum class Motion_mode : unsigned int
{
    e_static,    // zero mass, immovable
    e_kinematic, // zero mass, can move non-physically
    e_dynamic    // non-zero mass, physical movement
};

static constexpr const char* c_motion_mode_strings[] =
{
    "Static",
    "Kinematic",
    "Dynamic"
};

class ICollision_shape;
class IMotion_state;

class IRigid_body_create_info
{
public:
    float                             mass{1.0f};
    std::shared_ptr<ICollision_shape> collision_shape;
    glm::vec3                         local_inertia{0.0f, 0.0f, 0.0f};
};

class IRigid_body
{
public:
    [[nodiscard]]
    static auto create(
        const IRigid_body_create_info& create_info,
        IMotion_state*                 motion_state
    ) -> IRigid_body*;

    [[nodiscard]]
    static auto create_shared(
        const IRigid_body_create_info& create_info,
        IMotion_state*                 motion_state
    ) -> std::shared_ptr<IRigid_body>;

    virtual ~IRigid_body(){}

    virtual [[nodiscard]] auto get_collision_shape () const -> std::shared_ptr<ICollision_shape> = 0;
    virtual [[nodiscard]] auto get_motion_mode     () const -> Motion_mode                       = 0;
    virtual [[nodiscard]] auto get_friction        () const -> float                             = 0;
    virtual [[nodiscard]] auto get_rolling_friction() const -> float                             = 0;
    virtual [[nodiscard]] auto get_restitution     () const -> float                             = 0;
    virtual [[nodiscard]] auto get_linear_damping  () const -> float                             = 0;
    virtual [[nodiscard]] auto get_angular_damping () const -> float                             = 0;
    virtual [[nodiscard]] auto get_local_inertia   () const -> glm::vec3                         = 0;
    virtual [[nodiscard]] auto get_mass            () const -> float                             = 0;
    virtual void set_collision_shape         (const std::shared_ptr<ICollision_shape>& collision_shape) = 0;
    virtual void set_motion_mode             (const Motion_mode motion_mode)                            = 0;
    virtual void set_friction                (float friction)                                           = 0;
    virtual void set_rolling_friction        (float friction)                                           = 0;
    virtual void set_restitution             (float restitution)                                        = 0;
    virtual void set_center_of_mass_transform(const Transform transform)                                = 0;
    virtual void set_world_transform         (const Transform transform)                                = 0;
    virtual void set_linear_velocity         (const glm::vec3 velocity)                                 = 0;
    virtual void set_angular_velocity        (const glm::vec3 velocity)                                 = 0;
    virtual void set_damping                 (const float linear_damping, const float angular_damping)  = 0;
    virtual void set_mass_properties         (const float mass, const glm::vec3 local_inertia)          = 0;
    virtual void begin_move                  ()                                                         = 0;
    virtual void end_move                    ()                                                         = 0;
};

} // namespace erhe::physics
