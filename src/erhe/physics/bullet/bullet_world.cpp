#include "erhe/physics/bullet/bullet_world.hpp"
#include "erhe/physics/bullet/bullet_constraint.hpp"
#include "erhe/physics/bullet/bullet_rigid_body.hpp"
#include "erhe/physics/bullet/glm_conversions.hpp"
#include "erhe/physics/idebug_draw.hpp"
#include "erhe/physics/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include "erhe/toolkit/tracy_client.hpp"

#include "btBulletDynamicsCommon.h"

namespace erhe::physics
{

auto Debug_draw_adapter::getDefaultColors() const -> DefaultColors
{
    if (m_debug_draw == nullptr)
    {
        return DefaultColors{};
    }
    const auto colors = m_debug_draw->get_colors();
    DefaultColors bullet_colors;
	bullet_colors.m_activeObject               = from_glm(colors.active_object               );
	bullet_colors.m_deactivatedObject          = from_glm(colors.deactivated_object          );
	bullet_colors.m_wantsDeactivationObject    = from_glm(colors.wants_deactivation_object   );
	bullet_colors.m_disabledDeactivationObject = from_glm(colors.disabled_deactivation_object);
	bullet_colors.m_disabledSimulationObject   = from_glm(colors.disabled_simulation_object  );
	bullet_colors.m_aabb                       = from_glm(colors.aabb                        );
	bullet_colors.m_contactPoint               = from_glm(colors.contact_point               );

    return bullet_colors;
}

void Debug_draw_adapter::setDefaultColors(const DefaultColors& bullet_colors)
{
    if (m_debug_draw == nullptr)
    {
        return;
    }
    IDebug_draw::Colors colors;
	colors.active_object                = to_glm(bullet_colors.m_activeObject              );
	colors.deactivated_object           = to_glm(bullet_colors.m_deactivatedObject         );
	colors.wants_deactivation_object    = to_glm(bullet_colors.m_wantsDeactivationObject   );
	colors.disabled_deactivation_object = to_glm(bullet_colors.m_disabledDeactivationObject);
	colors.disabled_simulation_object   = to_glm(bullet_colors.m_disabledSimulationObject  );
	colors.aabb                         = to_glm(bullet_colors.m_aabb                      );
	colors.contact_point                = to_glm(bullet_colors.m_contactPoint              );
    m_debug_draw->set_colors(colors);
}

Debug_draw_adapter::Debug_draw_adapter() = default;

void Debug_draw_adapter::set_debug_draw(IDebug_draw* debug_draw)
{
    if (m_debug_draw != nullptr)
    {
        m_debug_draw = debug_draw;
    }
}

// Implements btIDebugDraw
void Debug_draw_adapter::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
{
    if (m_debug_draw != nullptr)
    {
        m_debug_draw->draw_line(to_glm(from), to_glm(to), to_glm(color));
    }
}

void Debug_draw_adapter::draw3dText(const btVector3& location, const char* textString)
{
    if (m_debug_draw != nullptr)
    {
        m_debug_draw->draw_3d_text(to_glm(location), textString);
    }
}

void Debug_draw_adapter::setDebugMode(int debugMode)
{
    if (m_debug_draw != nullptr)
    {
        m_debug_draw->set_debug_mode(debugMode);
    }
}

auto Debug_draw_adapter::getDebugMode() const -> int
{
    if (m_debug_draw != nullptr)
    {
        return m_debug_draw->get_debug_mode();
    }
    return DebugDrawModes::DBG_NoDebug;
}

void Debug_draw_adapter::drawContactPoint(
    const btVector3& PointOnB,
    const btVector3& normalOnB,
    btScalar         distance,
    int              lifeTime,
    const btVector3& color)
{
    if (m_debug_draw != nullptr)
    {
        m_debug_draw->draw_contact_point(
            to_glm(PointOnB),
            to_glm(normalOnB),
            static_cast<float>(distance),
            lifeTime,
            to_glm(color)
        );
    }
}

void Debug_draw_adapter::reportErrorWarning(const char* warningString)
{
    if (m_debug_draw != nullptr)
    {
        m_debug_draw->report_error_warning(warningString);
    }
}

auto IWorld::create() -> IWorld*
{
    return new Bullet_world();
}

auto IWorld::create_shared() -> std::shared_ptr<IWorld>
{
    return std::make_shared<Bullet_world>();
}

auto IWorld::create_unique() -> std::unique_ptr<IWorld>
{
    return std::make_unique<Bullet_world>();
}

Bullet_world::Bullet_world()
    : m_bullet_collision_configuration{}
    , m_bullet_collision_dispatcher{&m_bullet_collision_configuration}
    , m_bullet_broadphase_interface{}
    , m_bullet_impulse_constraint_solver{}
    , m_bullet_dynamics_world{
        &m_bullet_collision_dispatcher,
        &m_bullet_broadphase_interface,
        &m_bullet_impulse_constraint_solver,
        &m_bullet_collision_configuration
    }
{
    m_bullet_dynamics_world.setGravity(btVector3(0, -10, 0));
}

Bullet_world::~Bullet_world() = default;

void Bullet_world::enable_physics_updates()
{
    m_physics_enabled = true;
}

void Bullet_world::disable_physics_updates()
{
    m_physics_enabled = false;
}

auto Bullet_world::is_physics_updates_enabled() const -> bool
{
    return m_physics_enabled;
}

void Bullet_world::update_fixed_step(const double dt)
{
    ZoneScoped;

    if (!m_physics_enabled)
    {
        return;
    }

    const auto timeStep = static_cast<btScalar>(dt);
    const int maxSubSteps{1};

    m_bullet_dynamics_world.stepSimulation(timeStep, maxSubSteps, timeStep);
}

void Bullet_world::add_rigid_body(IRigid_body* rigid_body)
{
    VERIFY(rigid_body != nullptr);
    auto* bullet_rigid_body = dynamic_cast<Bullet_rigid_body*>(rigid_body);
    VERIFY(bullet_rigid_body != nullptr);
    m_bullet_dynamics_world.addRigidBody(bullet_rigid_body->get_bullet_rigid_body());
}

void Bullet_world::remove_rigid_body(IRigid_body* rigid_body)
{
    VERIFY(rigid_body != nullptr);
    auto* bullet_rigid_body = dynamic_cast<Bullet_rigid_body*>(rigid_body);
    VERIFY(bullet_rigid_body != nullptr);
    m_bullet_dynamics_world.removeRigidBody(bullet_rigid_body->get_bullet_rigid_body());
}

void Bullet_world::add_constraint(IConstraint* constraint)
{
    VERIFY(constraint != nullptr);
    auto* bullet_constraint = dynamic_cast<Bullet_constraint*>(constraint);
    VERIFY(bullet_constraint != nullptr);
    m_bullet_dynamics_world.addConstraint(bullet_constraint->get_bullet_constraint());
}

void Bullet_world::remove_constraint(IConstraint* constraint)
{
    VERIFY(constraint != nullptr);
    auto* bullet_constraint = dynamic_cast<Bullet_constraint*>(constraint);
    VERIFY(bullet_constraint != nullptr);
    m_bullet_dynamics_world.removeConstraint(bullet_constraint->get_bullet_constraint());
}

void Bullet_world::set_gravity(const glm::vec3 gravity)
{
    m_bullet_dynamics_world.setGravity(from_glm(gravity));
}

auto Bullet_world::get_gravity() const -> glm::vec3
{
    return to_glm(m_bullet_dynamics_world.getGravity());
}

void Bullet_world::set_debug_drawer(IDebug_draw* debug_draw)
{
    m_debug_draw_adapter.set_debug_draw(debug_draw);
    if (debug_draw != nullptr)
    {
        m_bullet_dynamics_world.setDebugDrawer(&m_debug_draw_adapter);
    }
    else
    {
        m_bullet_dynamics_world.setDebugDrawer(nullptr);
    }
}

void Bullet_world::debug_draw()
{
    m_bullet_dynamics_world.debugDrawWorld();
}


} // namespace erhe::physics
