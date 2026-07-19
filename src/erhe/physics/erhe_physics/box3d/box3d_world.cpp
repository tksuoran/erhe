#include "erhe_physics/box3d/box3d_world.hpp"
#include "erhe_physics/box3d/box3d_rigid_body.hpp"
#include "erhe_physics/box3d/glm_conversions.hpp"
#include "erhe_physics/physics_log.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace erhe::physics {

namespace {

// Box3D's "Soft Step" solver takes relaxation sub-steps within one step. Four
// is Box2D v3's documented default and gives stable stacking at 60 Hz.
constexpr int world_sub_step_count = 4;

} // anonymous namespace

void initialize_physics_system()
{
    // Box3D needs no process-wide initialization: there is no global allocator
    // registration, factory or type registry to set up (unlike Jolt). Worlds
    // are independent and created on demand.
    log_physics->info("box3d physics backend initialized");
}

Box3d_world::Box3d_world()
{
    b3WorldDef world_def = b3DefaultWorldDef();
    world_def.gravity     = b3Vec3{0.0f, -9.81f, 0.0f};
    world_def.enableSleep = true;
    // Single threaded: driving Box3D's task callbacks would need a scheduler
    // shared with erhe's, and the editor steps physics on the main thread.
    world_def.workerCount = 1;
    world_def.userData    = this;

    m_world = b3CreateWorld(&world_def);
}

Box3d_world::~Box3d_world() noexcept
{
    // Bodies are owned externally and must already be gone; destroying the
    // world releases anything Box3D still holds.
    b3DestroyWorld(m_world);
}

auto IWorld::create() -> IWorld*
{
    return new Box3d_world();
}

auto IWorld::create_shared() -> std::shared_ptr<IWorld>
{
    return std::make_shared<Box3d_world>();
}

auto IWorld::create_unique() -> std::unique_ptr<IWorld>
{
    return std::make_unique<Box3d_world>();
}

IWorld::~IWorld() noexcept
{
}

IWorld::State::~State() noexcept
{
}

auto Box3d_world::create_rigid_body(const IRigid_body_create_info& create_info) -> IRigid_body*
{
    return new Box3d_rigid_body(*this, create_info);
}

auto Box3d_world::create_rigid_body_shared(const IRigid_body_create_info& create_info) -> std::shared_ptr<IRigid_body>
{
    return std::make_shared<Box3d_rigid_body>(*this, create_info);
}

void Box3d_world::update_fixed_step(const double dt)
{
    b3World_Step(m_world, static_cast<float>(dt), world_sub_step_count);
}

void Box3d_world::add_rigid_body(IRigid_body* rigid_body)
{
    if (rigid_body == nullptr) {
        return;
    }
    m_rigid_bodies.push_back(rigid_body);
    static_cast<Box3d_rigid_body*>(rigid_body)->set_enabled_in_world(true);
}

void Box3d_world::remove_rigid_body(IRigid_body* rigid_body)
{
    if (rigid_body == nullptr) {
        return;
    }
    static_cast<Box3d_rigid_body*>(rigid_body)->set_enabled_in_world(false);
    m_rigid_bodies.erase(
        std::remove(m_rigid_bodies.begin(), m_rigid_bodies.end(), rigid_body),
        m_rigid_bodies.end()
    );
}

void Box3d_world::add_constraint(IConstraint* constraint)
{
    if (constraint == nullptr) {
        return;
    }
    m_constraints.push_back(constraint);
}

void Box3d_world::remove_constraint(IConstraint* constraint)
{
    if (constraint == nullptr) {
        return;
    }
    m_constraints.erase(
        std::remove(m_constraints.begin(), m_constraints.end(), constraint),
        m_constraints.end()
    );
}

void Box3d_world::set_gravity(const glm::vec3& gravity)
{
    b3World_SetGravity(m_world, to_box3d(gravity));
}

auto Box3d_world::get_gravity() const -> glm::vec3
{
    return from_box3d(b3World_GetGravity(m_world));
}

auto Box3d_world::get_rigid_body_count() const -> std::size_t
{
    return m_rigid_bodies.size();
}

auto Box3d_world::get_constraint_count() const -> std::size_t
{
    return m_constraints.size();
}

auto Box3d_world::describe() const -> std::vector<std::string>
{
    const b3Counters counters = b3World_GetCounters(m_world);
    std::vector<std::string> result;
    result.push_back(fmt::format("bodies: {}", counters.bodyCount));
    result.push_back(fmt::format("shapes: {}", counters.shapeCount));
    result.push_back(fmt::format("contacts: {}", counters.contactCount));
    result.push_back(fmt::format("joints: {}", counters.jointCount));
    result.push_back(fmt::format("islands: {}", counters.islandCount));
    return result;
}

void Box3d_world::debug_draw(erhe::renderer::Jolt_debug_renderer&)
{
    // Not implemented: IWorld::debug_draw() names the Jolt debug renderer in
    // its signature, so a backend-neutral path would have to land first. Box3D
    // does have b3World_Draw with its own b3DebugDraw callback struct, so this
    // is a wiring gap, not a capability gap.
}

void Box3d_world::sanity_check()
{
    // Box3D validates its own internal invariants (B3_ASSERT, and the
    // BOX3D_VALIDATE heavy checks) rather than exposing a check entry point.
}

void Box3d_world::set_on_body_activated(std::function<void(IRigid_body*)> callback)
{
    m_on_body_activated_callback = callback;
}

void Box3d_world::set_on_body_deactivated(std::function<void(IRigid_body*)> callback)
{
    m_on_body_deactivated_callback = callback;
}

void Box3d_world::for_each_active_body(std::function<void(IRigid_body*)> callback)
{
    if (!callback) {
        return;
    }
    for (IRigid_body* rigid_body : m_rigid_bodies) {
        Box3d_rigid_body* body = static_cast<Box3d_rigid_body*>(rigid_body);
        if (body->is_valid() && b3Body_IsAwake(body->get_box3d_body())) {
            callback(rigid_body);
        }
    }
}

void Box3d_world::set_on_trigger_enter(std::function<void(const Trigger_event&)> callback)
{
    m_on_trigger_enter_callback = callback;
}

void Box3d_world::set_on_trigger_exit(std::function<void(const Trigger_event&)> callback)
{
    m_on_trigger_exit_callback = callback;
}

void Box3d_world::set_collision_enabled(IRigid_body* rigid_body_a, IRigid_body* rigid_body_b, const bool enabled)
{
    static_cast<void>(rigid_body_a);
    static_cast<void>(rigid_body_b);
    static_cast<void>(enabled);
    log_physics->warn("box3d: set_collision_enabled() is not implemented yet");
}

auto Box3d_world::save_state() -> std::unique_ptr<IWorld::State>
{
    // Box3D has no public world snapshot API (src/world_snapshot.c exports
    // nothing through box3d.h). Returning an empty State that appears to
    // succeed would hand a future caller silent data loss instead of a
    // diagnostic, so this reports the gap and returns nothing.
    if (!m_save_state_warning_logged) {
        log_physics->warn("box3d: save_state() is not supported; Box3D exposes no world snapshot API");
        m_save_state_warning_logged = true;
    }
    return {};
}

void Box3d_world::restore_state(IWorld::State& state)
{
    static_cast<void>(state);
    log_physics->warn("box3d: restore_state() is not supported; Box3D exposes no world snapshot API");
}

auto Box3d_world::would_bodies_intersect(
    const IRigid_body& body_a, const Transform& transform_a,
    const IRigid_body& body_b, const Transform& transform_b,
    const float        penetration_tolerance
) const -> bool
{
    static_cast<void>(body_a);
    static_cast<void>(transform_a);
    static_cast<void>(body_b);
    static_cast<void>(transform_b);
    static_cast<void>(penetration_tolerance);
    log_physics->warn("box3d: would_bodies_intersect() is not implemented yet");
    return false;
}

auto Box3d_world::would_body_intersect_world(
    const IRigid_body& body,
    const Transform&   transform,
    const float        penetration_tolerance
) const -> bool
{
    static_cast<void>(body);
    static_cast<void>(transform);
    static_cast<void>(penetration_tolerance);
    log_physics->warn("box3d: would_body_intersect_world() is not implemented yet");
    return false;
}

} // namespace erhe::physics
