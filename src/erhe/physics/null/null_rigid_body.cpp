#include "erhe/physics/null/null_rigid_body.hpp"
#include "erhe/physics/null/null_collision_shape.hpp"
#include "erhe/physics/imotion_state.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/scene/node.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace erhe::physics
{

auto IRigid_body::create_rigid_body(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
) -> IRigid_body*
{
    return new Null_rigid_body(create_info, motion_state);
}

auto IRigid_body::create_rigid_body_shared(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
) -> std::shared_ptr<IRigid_body>
{
    return std::make_shared<Null_rigid_body>(create_info, motion_state);
}

Null_rigid_body::Null_rigid_body(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 /*motion_state*/
)
    : m_collision_shape{create_info.collision_shape}
    , m_mass           {create_info.mass}
    , m_motion_mode{
        (create_info.mass.has_value())
            ? Motion_mode::e_dynamic
            : Motion_mode::e_static
    }
    , m_debug_label{create_info.debug_label}
{
    if (create_info.inertia_override.has_value())
    {
        m_local_inertia = create_info.inertia_override.value();
    }
}

IRigid_body::~IRigid_body() noexcept
{
}

Null_rigid_body::~Null_rigid_body() noexcept
{
}

auto Null_rigid_body::get_motion_mode() const -> Motion_mode
{
    return m_motion_mode;
}

auto Null_rigid_body::get_collision_shape() const -> std::shared_ptr<ICollision_shape>
{
    return m_collision_shape;
}

auto Null_rigid_body::get_friction() const -> float
{
    return m_friction;
}

void Null_rigid_body::set_friction(const float friction)
{
    m_friction = friction;
}

void Null_rigid_body::set_gravity_factor(float gravity_factor)
{
    m_gravity_factor = gravity_factor;
}

auto Null_rigid_body::get_restitution() const -> float
{
    return m_restitution;
}

auto Null_rigid_body::get_world_transform() const -> Transform
{
    return m_transform;
}

void Null_rigid_body::set_restitution(float restitution)
{
    m_restitution = restitution;
}

void Null_rigid_body::begin_move()
{
}

void Null_rigid_body::end_move()
{
}

void Null_rigid_body::set_motion_mode(const Motion_mode motion_mode)
{
    m_motion_mode = motion_mode;
}

void Null_rigid_body::set_center_of_mass_transform(const Transform transform)
{
    // TODO
    static_cast<void>(transform);
}

void Null_rigid_body::set_world_transform(const Transform transform)
{
    m_transform = transform;
}

void Null_rigid_body::move_world_transform(const Transform transform, float delta_time)
{
    static_cast<void>(delta_time);
    m_transform = transform;
}

void Null_rigid_body::set_linear_velocity(const glm::vec3 velocity)
{
    m_linear_velocity = velocity;
}

void Null_rigid_body::set_angular_velocity(const glm::vec3 velocity)
{
    m_angular_velocity = velocity;
}

auto Null_rigid_body::get_gravity_factor() const -> float
{
    return m_gravity_factor;
}

auto Null_rigid_body::get_linear_damping() const -> float
{
    return m_linear_damping;
}

auto Null_rigid_body::get_linear_velocity() const -> glm::vec3
{
    return m_linear_velocity;
}

void Null_rigid_body::set_damping(const float linear_damping, const float angular_damping)
{
    m_linear_damping = linear_damping;
    m_angular_damping = angular_damping;
}

auto Null_rigid_body::get_angular_damping () const -> float
{
    return m_linear_damping;
}

auto Null_rigid_body::get_angular_velocity() const -> glm::vec3
{
    return m_angular_velocity;
}

auto Null_rigid_body::get_local_inertia() const -> glm::mat4
{
    return m_local_inertia;
}

auto Null_rigid_body::get_center_of_mass_transform() const -> Transform
{
    return Transform{};
}

auto Null_rigid_body::get_mass() const -> float
{
    return m_mass.has_value() ? m_mass.value() : 0.0f;
}

void Null_rigid_body::set_mass_properties(const float mass, const glm::mat4 local_inertia)
{
    m_mass = mass;
    m_local_inertia = local_inertia;
}

auto Null_rigid_body::get_debug_label() const -> const char*
{
    return m_debug_label.c_str();
}


} // namespace erhe::physics

