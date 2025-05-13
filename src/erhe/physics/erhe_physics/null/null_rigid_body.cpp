#include "erhe_physics/null/null_rigid_body.hpp"
#include "erhe_physics/null/null_collision_shape.hpp"
#include "erhe_physics/null/null_world.hpp"

#include <glm/glm.hpp>

namespace erhe::physics {

Null_rigid_body::Null_rigid_body(
    Null_world&                    world,
    const IRigid_body_create_info& create_info,
    glm::vec3                      ,
    glm::quat                      
)
    : m_world          {world}
    , m_collision_shape{create_info.collision_shape}
    , m_mass           {create_info.mass}
    , m_motion_mode    {create_info.motion_mode}
    , m_debug_label    {create_info.debug_label}
{
    if (create_info.inertia_override.has_value()) {
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

auto Null_rigid_body::get_world_transform() const -> glm::mat4
{
    return {};
}

auto Null_rigid_body::is_active() const -> bool
{
    return false;
}

auto Null_rigid_body::get_allow_sleeping() const -> bool
{
    return true;
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

void Null_rigid_body::set_world_transform(const Transform& transform)
{
    m_transform = transform;
}

void Null_rigid_body::set_linear_velocity(const glm::vec3& velocity)
{
    m_linear_velocity = velocity;
}

void Null_rigid_body::set_angular_velocity(const glm::vec3& velocity)
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

auto Null_rigid_body::get_center_of_mass() const -> glm::vec3
{
    return glm::vec3{0.0f, 0.0f, 0.0f};
}

auto Null_rigid_body::get_center_of_mass_transform() const -> Transform
{
    return Transform{};
}

auto Null_rigid_body::get_mass() const -> float
{
    return m_mass.has_value() ? m_mass.value() : 0.0f;
}

void Null_rigid_body::set_mass_properties(const float mass, const glm::mat4& local_inertia)
{
    m_mass = mass;
    m_local_inertia = local_inertia;
}

auto Null_rigid_body::get_debug_label() const -> const char*
{
    return m_debug_label.c_str();
}

void Null_rigid_body::set_allow_sleeping(bool)
{
}

void Null_rigid_body::set_owner(void*)
{
}

auto Null_rigid_body::get_owner() const -> void*
{
    return nullptr;
}

} // namespace erhe::physics

