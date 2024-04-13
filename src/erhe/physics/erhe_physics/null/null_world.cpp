#include "erhe_physics/null/null_world.hpp"
#include "erhe_physics/null/null_constraint.hpp"
#include "erhe_physics/null/null_rigid_body.hpp"
#include "erhe_physics/idebug_draw.hpp"
#include "erhe_physics/physics_log.hpp"
#include "erhe_verify/verify.hpp"

#include "erhe_profile/profile.hpp"

namespace erhe::physics
{

auto IWorld::create() -> IWorld*
{
    return new Null_world();
}

auto IWorld::create_shared() -> std::shared_ptr<IWorld>
{
    return std::make_shared<Null_world>();
}

auto IWorld::create_unique() -> std::unique_ptr<IWorld>
{
    return std::make_unique<Null_world>();
}

IWorld::~IWorld() noexcept
{
}

Null_world::~Null_world() noexcept
{
}

auto Null_world::create_rigid_body(const IRigid_body_create_info& create_info, glm::vec3, glm::quat) -> IRigid_body*
{
    return new Null_rigid_body(create_info);
}

auto Null_world::create_rigid_body_shared(const IRigid_body_create_info& create_info, glm::vec3, glm::quat) -> std::shared_ptr<IRigid_body>
{
    return std::make_shared<Null_rigid_body>(create_info);
}

void Null_world::update_fixed_step(const double dt)
{
    static_cast<void>(dt);
}

void Null_world::add_rigid_body(IRigid_body* rigid_body)
{
    m_rigid_bodies.push_back(rigid_body);
}

void Null_world::remove_rigid_body(IRigid_body* rigid_body)
{
    m_rigid_bodies.erase(
        std::remove(
            m_rigid_bodies.begin(),
            m_rigid_bodies.end(),
            rigid_body
        ),
        m_rigid_bodies.end()
    );
}

void Null_world::add_constraint(IConstraint* constraint)
{
    m_constraints.push_back(constraint);
}

void Null_world::remove_constraint(IConstraint* constraint)
{
    m_constraints.erase(
        std::remove(
            m_constraints.begin(),
            m_constraints.end(),
            constraint
        ),
        m_constraints.end()
    );
}

void Null_world::set_gravity(const glm::vec3& gravity)
{
    m_gravity = gravity;
}

auto Null_world::get_gravity() const -> glm::vec3
{
    return m_gravity;
}

auto Null_world::get_rigid_body_count() const -> std::size_t
{
    return 0;
}

auto Null_world::get_constraint_count() const -> std::size_t
{
    return 0;
}

auto Null_world::describe() const -> std::vector<std::string>
{
    return {};
}

void Null_world::set_debug_drawer(IDebug_draw*)
{
}

void Null_world::debug_draw()
{
}

void Null_world::sanity_check()
{
}

void Null_world::set_on_body_activated(std::function<void(IRigid_body*)>)
{
}

void Null_world::set_on_body_deactivated(std::function<void(IRigid_body*)>)
{
}

void Null_world::for_each_active_body(std::function<void(IRigid_body*)>)
{
}

} // namespace erhe::physics
