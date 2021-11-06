#include "erhe/physics/null/null_world.hpp"
#include "erhe/physics/null/null_constraint.hpp"
#include "erhe/physics/null/null_rigid_body.hpp"
#include "erhe/physics/idebug_draw.hpp"
#include "erhe/physics/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include "erhe/toolkit/tracy_client.hpp"

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

void Null_world::enable_physics_updates()
{
    m_physics_enabled = true;
}

void Null_world::disable_physics_updates()
{
    m_physics_enabled = false;
}

auto Null_world::is_physics_updates_enabled() const -> bool
{
    return m_physics_enabled;
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

void Null_world::set_gravity(const glm::vec3 gravity)
{
    m_gravity = gravity;
}

auto Null_world::get_gravity() const -> glm::vec3
{
    return m_gravity;
}

void Null_world::set_debug_drawer(IDebug_draw* debug_draw)
{
    static_cast<void>(debug_draw);
}

void Null_world::debug_draw()
{
}


} // namespace erhe::physics
