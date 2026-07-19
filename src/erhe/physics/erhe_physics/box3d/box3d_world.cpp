#include "erhe_physics/box3d/box3d_world.hpp"
#include "erhe_physics/box3d/box3d_material_registry.hpp"
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

    // erhe's Combine_mode is a pair property, so friction and restitution must
    // be resolved at contact time rather than precombined per shape.
    Box3d_material_registry::install_callbacks(m_world);

    // erhe's collision-system filters cannot be expressed with Box3D's
    // category/mask bits (see Box3d_collision_filter_table), so they run
    // through the custom filter callback, which does take a context pointer.
    b3World_SetCustomFilterCallback(m_world, &Box3d_world::custom_filter_callback, this);
}

auto Box3d_world::custom_filter_callback(const b3ShapeId shape_id_a, const b3ShapeId shape_id_b, void* context) -> bool
{
    const Box3d_world* world = static_cast<const Box3d_world*>(context);
    if (world == nullptr) {
        return true;
    }
    const Box3d_rigid_body* body_a = static_cast<const Box3d_rigid_body*>(b3Body_GetUserData(b3Shape_GetBody(shape_id_a)));
    const Box3d_rigid_body* body_b = static_cast<const Box3d_rigid_body*>(b3Body_GetUserData(b3Shape_GetBody(shape_id_b)));
    if ((body_a == nullptr) || (body_b == nullptr)) {
        return true;
    }
    return world->m_filter_table.should_collide(body_a->get_filter_index(), body_b->get_filter_index());
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

auto Box3d_world::Body_pair_hash::operator()(const Body_pair_key& key) const -> std::size_t
{
    const std::size_t first  = std::hash<const void*>{}(key.first);
    const std::size_t second = std::hash<const void*>{}(key.second);
    return first ^ (second + 0x9e3779b9u + (first << 6) + (first >> 2));
}

auto Box3d_world::get_or_create_world_anchor_body() -> b3BodyId
{
    if (!m_has_world_anchor_body) {
        b3BodyDef body_def = b3DefaultBodyDef();
        body_def.type = b3_staticBody;
        body_def.name = "erhe world anchor";
        m_world_anchor_body     = b3CreateBody(m_world, &body_def);
        m_has_world_anchor_body = true;
    }
    return m_world_anchor_body;
}

void Box3d_world::forget_filter_joints_for_body(const Box3d_rigid_body* rigid_body)
{
    const auto by_body = m_filter_joints_by_body.find(rigid_body);
    if (by_body == m_filter_joints_by_body.end()) {
        return;
    }
    for (const Body_pair_key& key : by_body->second) {
        // Only erase the bookkeeping: Box3D destroys a body's joints along with
        // the body, so the b3JointId is about to become (or already is) stale.
        m_filter_joints.erase(key);
        const Box3d_rigid_body* other = (key.first == rigid_body) ? key.second : key.first;
        const auto other_entry = m_filter_joints_by_body.find(other);
        if (other_entry != m_filter_joints_by_body.end()) {
            std::vector<Body_pair_key>& keys = other_entry->second;
            keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
        }
    }
    m_filter_joints_by_body.erase(by_body);
}

void Box3d_world::set_collision_enabled(IRigid_body* rigid_body_a, IRigid_body* rigid_body_b, const bool enabled)
{
    Box3d_rigid_body* body_a = static_cast<Box3d_rigid_body*>(rigid_body_a);
    Box3d_rigid_body* body_b = static_cast<Box3d_rigid_body*>(rigid_body_b);
    if ((body_a == nullptr) || (body_b == nullptr) || !body_a->is_valid() || !body_b->is_valid()) {
        return;
    }
    // Order the pair so it has one canonical key regardless of argument order.
    const Body_pair_key key = (body_a < body_b)
        ? Body_pair_key{body_a, body_b}
        : Body_pair_key{body_b, body_a};

    const auto existing = m_filter_joints.find(key);
    if (enabled) {
        if (existing != m_filter_joints.end()) {
            b3DestroyJoint(existing->second, true);
            m_filter_joints.erase(existing);
            for (const Box3d_rigid_body* body : {key.first, key.second}) {
                const auto entry = m_filter_joints_by_body.find(body);
                if (entry != m_filter_joints_by_body.end()) {
                    std::vector<Body_pair_key>& keys = entry->second;
                    keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
                }
            }
        }
        return;
    }

    if (existing != m_filter_joints.end()) {
        return; // already excluded
    }
    // A filter joint exists precisely to disable collision between two bodies,
    // so this is a direct mapping rather than the sub-group hack the Jolt
    // backend needs.
    b3FilterJointDef joint_def = b3DefaultFilterJointDef();
    joint_def.base.bodyIdA = key.first->get_box3d_body();
    joint_def.base.bodyIdB = key.second->get_box3d_body();
    const b3JointId joint = b3CreateFilterJoint(m_world, &joint_def);
    m_filter_joints.emplace(key, joint);
    m_filter_joints_by_body[key.first ].push_back(key);
    m_filter_joints_by_body[key.second].push_back(key);
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
