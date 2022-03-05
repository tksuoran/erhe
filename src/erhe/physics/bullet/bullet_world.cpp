#include "erhe/physics/bullet/bullet_world.hpp"
#include "erhe/physics/bullet/bullet_constraint.hpp"
#include "erhe/physics/bullet/bullet_rigid_body.hpp"
#include "erhe/physics/bullet/glm_conversions.hpp"
#include "erhe/physics/idebug_draw.hpp"
#include "erhe/physics/log.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

#include <btBulletDynamicsCommon.h>

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
	bullet_colors.m_activeObject               = to_bullet(colors.active_object               );
	bullet_colors.m_deactivatedObject          = to_bullet(colors.deactivated_object          );
	bullet_colors.m_wantsDeactivationObject    = to_bullet(colors.wants_deactivation_object   );
	bullet_colors.m_disabledDeactivationObject = to_bullet(colors.disabled_deactivation_object);
	bullet_colors.m_disabledSimulationObject   = to_bullet(colors.disabled_simulation_object  );
	bullet_colors.m_aabb                       = to_bullet(colors.aabb                        );
	bullet_colors.m_contactPoint               = to_bullet(colors.contact_point               );

    return bullet_colors;
}

void Debug_draw_adapter::setDefaultColors(const DefaultColors& bullet_colors)
{
    if (m_debug_draw == nullptr)
    {
        return;
    }
    IDebug_draw::Colors colors;
	colors.active_object                = from_bullet(bullet_colors.m_activeObject              );
	colors.deactivated_object           = from_bullet(bullet_colors.m_deactivatedObject         );
	colors.wants_deactivation_object    = from_bullet(bullet_colors.m_wantsDeactivationObject   );
	colors.disabled_deactivation_object = from_bullet(bullet_colors.m_disabledDeactivationObject);
	colors.disabled_simulation_object   = from_bullet(bullet_colors.m_disabledSimulationObject  );
	colors.aabb                         = from_bullet(bullet_colors.m_aabb                      );
	colors.contact_point                = from_bullet(bullet_colors.m_contactPoint              );
    m_debug_draw->set_colors(colors);
}

Debug_draw_adapter::Debug_draw_adapter() = default;

void Debug_draw_adapter::set_debug_draw(IDebug_draw* debug_draw)
{
    m_debug_draw = debug_draw;
}

// Implements btIDebugDraw
void Debug_draw_adapter::drawLine(
    const btVector3& from,
    const btVector3& to,
    const btVector3& color
)
{
    if (m_debug_draw != nullptr)
    {
        m_debug_draw->draw_line(
            from_bullet(from),
            from_bullet(to),
            from_bullet(color)
        );
    }
}

void Debug_draw_adapter::draw3dText(
    const btVector3& location,
    const char*      textString
)
{
    if (m_debug_draw != nullptr)
    {
        m_debug_draw->draw_3d_text(from_bullet(location), textString);
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
            from_bullet(PointOnB),
            from_bullet(normalOnB),
            static_cast<float>(distance),
            lifeTime,
            from_bullet(color)
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

IWorld::~IWorld()
{
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

auto IWorld::create_rigid_body(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
) -> IRigid_body*
{
    return new Bullet_rigid_body(create_info, motion_state);
}

auto IWorld::create_rigid_body_shared(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
) -> std::shared_ptr<IRigid_body>
{
    return std::make_shared<Bullet_rigid_body>(create_info, motion_state);
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

Bullet_world::~Bullet_world()
{
}

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
    ERHE_PROFILE_FUNCTION

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
    ERHE_VERIFY(rigid_body != nullptr);
    auto* bullet_rigid_body = dynamic_cast<Bullet_rigid_body*>(rigid_body);
    ERHE_VERIFY(bullet_rigid_body != nullptr);
    m_bullet_dynamics_world.addRigidBody(bullet_rigid_body->get_bullet_rigid_body());
}

void Bullet_world::remove_rigid_body(IRigid_body* rigid_body)
{
    ERHE_VERIFY(rigid_body != nullptr);
    auto* bullet_rigid_body = dynamic_cast<Bullet_rigid_body*>(rigid_body);
    ERHE_VERIFY(bullet_rigid_body != nullptr);
    m_bullet_dynamics_world.removeRigidBody(bullet_rigid_body->get_bullet_rigid_body());
}

void Bullet_world::add_constraint(IConstraint* constraint)
{
    // log_physics.info("add_constraint()\n");
    ERHE_VERIFY(constraint != nullptr);
    auto* bullet_constraint = dynamic_cast<Bullet_constraint*>(constraint);
    ERHE_VERIFY(bullet_constraint != nullptr);
    m_bullet_dynamics_world.addConstraint(bullet_constraint->get_bullet_constraint());
}

void Bullet_world::remove_constraint(IConstraint* constraint)
{
    // log_physics.info("remove_constraint()\n");
    ERHE_VERIFY(constraint != nullptr);
    auto* bullet_constraint = dynamic_cast<Bullet_constraint*>(constraint);
    ERHE_VERIFY(bullet_constraint != nullptr);
    m_bullet_dynamics_world.removeConstraint(bullet_constraint->get_bullet_constraint());
}

void Bullet_world::set_gravity(const glm::vec3 gravity)
{
    m_bullet_dynamics_world.setGravity(to_bullet(gravity));
}

auto Bullet_world::get_gravity() const -> glm::vec3
{
    return from_bullet(m_bullet_dynamics_world.getGravity());
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
