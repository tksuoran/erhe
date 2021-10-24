#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene
{
    class Node;
}

namespace erhe::physics
{

enum class Collision_mode : unsigned int
{
    e_static,    // zero mass, immovable
    e_kinematic, // zero mass, can move non-physically
    e_dynamic    // non-zero mass, physical movement
};

static constexpr const char* c_collision_mode_strings[] =
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
    static auto create(
        IRigid_body_create_info& create_info,
        IMotion_state*           motion_state
    ) -> IRigid_body*;

    static auto create_shared(
        IRigid_body_create_info& create_info,
        IMotion_state*           motion_state
    ) -> std::shared_ptr<IRigid_body>;

    //virtual auto get_node_transform() const -> glm::mat4 = 0;
    virtual void set_collision_mode  (Collision_mode collision_mode)                           = 0;
    virtual auto get_collision_mode  () const -> Collision_mode                                = 0;
    virtual auto get_collision_shape () const -> ICollision_shape*                             = 0;
    virtual void set_static          ()                                                        = 0;
    virtual void set_dynamic         ()                                                        = 0;
    virtual void set_kinematic       ()                                                        = 0;
    virtual auto get_friction        () -> float                                               = 0;
    virtual void set_friction        (float friction)                                          = 0;
    virtual auto get_rolling_friction() -> float                                               = 0;
    virtual void set_rolling_friction(float friction)                                          = 0;
    virtual auto get_restitution     () -> float                                               = 0;
    virtual void set_restitution     (float restitution)                                       = 0;
    virtual void set_world_transform (const glm::mat3 basis, const glm::vec3 origin)           = 0;
    virtual void set_linear_velocity (const glm::vec3 velocity)                                = 0;
    virtual void set_angular_velocity(const glm::vec3 velocity)                                = 0;
    virtual auto get_linear_damping  () const -> float                                         = 0;
    virtual auto get_angular_damping () const -> float                                         = 0;
    virtual void set_damping         (const float linear_damping, const float angular_damping) = 0;
    virtual auto get_local_inertia   () const -> glm::vec3                                     = 0;
    virtual auto get_mass            () const -> float                                         = 0;
    virtual void set_mass_properties (const float mass, const glm::vec3 local_inertia)         = 0;
    virtual void begin_move          ()                                                        = 0;
    virtual void end_move            ()                                                        = 0;
};

} // namespace erhe::physics
