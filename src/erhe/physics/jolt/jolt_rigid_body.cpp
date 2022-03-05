#include "erhe/physics/jolt/jolt_rigid_body.hpp"
#include "erhe/physics/jolt/jolt_collision_shape.hpp"
#include "erhe/physics/imotion_state.hpp"
#include "erhe/scene/node.hpp"

#include <Jolt.h>
#include <Physics/Body/Body.h>

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace erhe::physics
{

auto IRigid_body::create(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
) -> IRigid_body*
{
    return new Jolt_rigid_body(create_info, motion_state);
}

auto IRigid_body::create_shared(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
) -> std::shared_ptr<IRigid_body>
{
    return std::make_shared<Jolt_rigid_body>(create_info, motion_state);
}

Jolt_rigid_body::Jolt_rigid_body(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 /*motion_state*/
)
    : m_collision_shape{create_info.collision_shape}
    , m_mass           {create_info.mass}
    , m_local_inertia  {create_info.local_inertia}
    , m_motion_mode{
        (create_info.mass > 0.0f)
            ? Motion_mode::e_dynamic
            : Motion_mode::e_static
    }
{
}

Jolt_rigid_body::~Jolt_rigid_body() = default;

auto Jolt_rigid_body::get_motion_mode() const -> Motion_mode
{
    return m_motion_mode;
}

void Jolt_rigid_body::set_collision_shape(const std::shared_ptr<ICollision_shape>& collision_shape)
{
    m_collision_shape = collision_shape;
}

auto Jolt_rigid_body::get_collision_shape() const -> std::shared_ptr<ICollision_shape>
{
    return m_collision_shape;
}

auto Jolt_rigid_body::get_friction() const -> float
{
    return m_friction;
}

void Jolt_rigid_body::set_friction(const float friction)
{
    m_friction = friction;
}

auto Jolt_rigid_body::get_rolling_friction() const -> float
{
    return m_rolling_friction;
}

void Jolt_rigid_body::set_rolling_friction(const float rolling_friction)
{
    m_rolling_friction = rolling_friction;
}

auto Jolt_rigid_body::get_restitution() const -> float
{
    return m_restitution;
}

void Jolt_rigid_body::set_restitution(float restitution)
{
    m_restitution = restitution;
}

void Jolt_rigid_body::begin_move()
{
}

void Jolt_rigid_body::end_move()
{
}

void Jolt_rigid_body::set_motion_mode(const Motion_mode motion_mode)
{
    m_motion_mode = motion_mode;
}

void Jolt_rigid_body::set_center_of_mass_transform(const Transform transform)
{
    // TODO
    static_cast<void>(transform);
}

void Jolt_rigid_body::set_world_transform(const Transform transform)
{
    m_transform = transform;
}

void Jolt_rigid_body::set_linear_velocity(const glm::vec3 velocity)
{
    m_linear_velocity = velocity;
}

void Jolt_rigid_body::set_angular_velocity(const glm::vec3 velocity)
{
    m_angular_velocity = velocity;
}

auto Jolt_rigid_body::get_linear_damping() const -> float
{
    return m_linear_damping;
}

void Jolt_rigid_body::set_damping(const float linear_damping, const float angular_damping)
{
    m_linear_damping = linear_damping;
    m_angular_damping = angular_damping;
}

auto Jolt_rigid_body::get_angular_damping () const -> float
{
    return m_linear_damping;
}

auto Jolt_rigid_body::get_local_inertia() const -> glm::vec3
{
    return m_local_inertia;
}

auto Jolt_rigid_body::get_mass() const -> float
{
    return m_mass;
}

void Jolt_rigid_body::set_mass_properties(const float mass, const glm::vec3 local_inertia)
{
    m_mass = mass;
    m_local_inertia = local_inertia;
}


} // namespace erhe::physics

