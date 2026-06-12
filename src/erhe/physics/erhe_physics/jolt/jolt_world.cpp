#include "erhe_physics/jolt/jolt_world.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/jolt/jolt_constraint.hpp"
#include "erhe_physics/jolt/jolt_rigid_body.hpp"
#include "erhe_physics/jolt/glm_conversions.hpp"
#include "erhe_physics/idebug_draw.hpp"
#include "erhe_physics/physics_log.hpp"
#include "erhe_renderer/jolt_debug_renderer.hpp"
#include "erhe_verify/verify.hpp"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/Body.h>

#include <algorithm>
#include <cstdarg>

namespace erhe::physics {

// Callback for traces, connect this to your own trace function if you have one
static void TraceImpl(const char* inFMT, ...)
{
    va_list list;
    va_start(list, inFMT);
    std::array<char, 1024> buffer{};
    vsnprintf(buffer.data(), buffer.size(), inFMT, list);

    log_physics->info("{}", buffer.data());
}

#ifdef JPH_ENABLE_ASSERTS

// Callback for asserts, connect this to your own assert handler if you have one
static auto AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, unsigned int inLine) -> bool
{
    log_physics->error("{}:{} ({}) {}", inFile, inLine, inExpression, (inMessage != nullptr) ? inMessage : "");
    // Breakpoint
    return true;
};

#endif // JPH_ENABLE_ASSERTS

namespace Layers {

[[nodiscard]] auto get_layer(const Motion_mode motion_mode) -> uint8_t
{
    switch (motion_mode) {
        case Motion_mode::e_static:                 return Layers::NON_MOVING;
        case Motion_mode::e_kinematic_non_physical: return Layers::MOVING;
        case Motion_mode::e_kinematic_physical:     return Layers::MOVING;
        case Motion_mode::e_dynamic:                return Layers::MOVING;
        default:                                    return Layers::MOVING;
    }
}

} // namespace Layers

// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING   {0};
    static constexpr JPH::BroadPhaseLayer MOVING       {1};
    static constexpr JPH::BroadPhaseLayer NON_COLLIDING{2};
    static constexpr unsigned int         NUM_LAYERS   {3};
};

// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class Broad_phase_layer_interface_impl final : public JPH::BroadPhaseLayerInterface
{
public:
    Broad_phase_layer_interface_impl()
    {
        // Create a mapping table from object to broad phase layer
        m_object_to_broad_phase[Layers::NON_MOVING   ] = BroadPhaseLayers::NON_MOVING;
        m_object_to_broad_phase[Layers::MOVING       ] = BroadPhaseLayers::MOVING;
        m_object_to_broad_phase[Layers::NON_COLLIDING] = BroadPhaseLayers::NON_COLLIDING;
    }

    auto GetNumBroadPhaseLayers() const -> unsigned int override
    {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    auto GetBroadPhaseLayer(const JPH::ObjectLayer inLayer) const -> JPH::BroadPhaseLayer override
    {
        ERHE_VERIFY(inLayer < Layers::NUM_LAYERS);
        return m_object_to_broad_phase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    auto GetBroadPhaseLayerName(const JPH::BroadPhaseLayer inLayer) const -> const char* override
    {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:    return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:        return "MOVING";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_COLLIDING: return "NON_COLLIDING";
            default:                                                          return "?";
        }
    }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
    JPH::BroadPhaseLayer m_object_to_broad_phase[Layers::NUM_LAYERS];
};

auto Jolt_collision_filter::ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const -> bool
{
    switch (inLayer1) {
        case Layers::NON_MOVING:    return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING:        return inLayer2 != BroadPhaseLayers::NON_COLLIDING;
        case Layers::NON_COLLIDING: return false;
        default: {
            return false;
        }
    }
}

auto Jolt_collision_filter::ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const -> bool
{
    switch (inLayer1) {
        case Layers::NON_MOVING:    return inLayer2 == Layers::MOVING;        // Non moving only collides with moving
        case Layers::MOVING:        return inLayer2 != Layers::NON_COLLIDING;
        case Layers::NON_COLLIDING: return false; // Does not collide with anything
        default:                    ERHE_VERIFY(false); return false;
    }
}

namespace {

// Packs an ordered SubGroupID pair into a single 64-bit pair-exclusion key.
[[nodiscard]] auto pack_subgroup_pair(const JPH::CollisionGroup::SubGroupID a, const JPH::CollisionGroup::SubGroupID b) -> uint64_t
{
    const uint64_t lo = static_cast<uint64_t>(std::min(a, b));
    const uint64_t hi = static_cast<uint64_t>(std::max(a, b));
    return (hi << 32) | lo;
}

// Packs a (body1, body2) BodyID pair into a sensor-contact map key. Jolt
// orders body 1 ID < body 2 ID in both OnContactAdded and OnContactRemoved,
// but the ids are min/max-ordered here anyway for robustness.
[[nodiscard]] auto pack_body_pair(const JPH::BodyID body_id_1, const JPH::BodyID body_id_2) -> uint64_t
{
    const uint32_t raw1 = body_id_1.GetIndexAndSequenceNumber();
    const uint32_t raw2 = body_id_2.GetIndexAndSequenceNumber();
    const uint64_t lo   = static_cast<uint64_t>(std::min(raw1, raw2));
    const uint64_t hi   = static_cast<uint64_t>(std::max(raw1, raw2));
    return (hi << 32) | lo;
}

} // anonymous namespace

auto Jolt_group_filter::CanCollide(const JPH::CollisionGroup& group1, const JPH::CollisionGroup& group2) const -> bool
{
    // Read-only during PhysicsSystem::Update(); mutations happen only outside
    // update_fixed_step() - see class comment.

    // 1. Pair exclusion (IWorld::set_collision_enabled(), joint enableCollision = false)
    if (!m_excluded_pairs.empty()) {
        const uint64_t key = pack_subgroup_pair(group1.GetSubGroupID(), group2.GetSubGroupID());
        if (m_excluded_pairs.find(key) != m_excluded_pairs.end()) {
            return false;
        }
    }

    // 2. Bidirectional KHR_physics_rigid_bodies collision-system test
    return
        does_collide(group1.GetGroupID(), group2.GetGroupID()) &&
        does_collide(group2.GetGroupID(), group1.GetGroupID());
}

auto Jolt_group_filter::does_collide(const JPH::CollisionGroup::GroupID a, const JPH::CollisionGroup::GroupID b) const -> bool
{
    if (a == JPH::CollisionGroup::cInvalidGroup) {
        return true; // no filter - collides with everything
    }
    const Compiled_collision_filter& filter = m_compiled_filters[a];
    const uint64_t other_membership = (b == JPH::CollisionGroup::cInvalidGroup)
        ? uint64_t{0}
        : m_compiled_filters[b].membership;
    return filter.is_allow_list
        ? ((filter.mask & other_membership) != 0)
        : ((filter.mask & other_membership) == 0);
}

auto Jolt_group_filter::intern_system(const std::string& name) -> int
{
    for (std::size_t i = 0, end = m_system_names.size(); i < end; ++i) {
        if (m_system_names[i] == name) {
            return static_cast<int>(i);
        }
    }
    if (m_system_names.size() >= 64) {
        log_physics->error("collision system limit (64) exceeded; system '{}' ignored", name);
        return -1;
    }
    m_system_names.push_back(name);
    return static_cast<int>(m_system_names.size() - 1);
}

auto Jolt_group_filter::make_bitset(const std::vector<std::string>& names) -> uint64_t
{
    uint64_t bits = 0;
    for (const std::string& name : names) {
        const int index = intern_system(name);
        if (index >= 0) {
            bits = bits | (uint64_t{1} << static_cast<unsigned int>(index));
        }
    }
    return bits;
}

auto Jolt_group_filter::get_or_compile(const std::shared_ptr<Collision_filter>& filter) -> JPH::CollisionGroup::GroupID
{
    const auto existing = m_filter_to_compiled.find(filter.get());
    if (existing != m_filter_to_compiled.end()) {
        return existing->second;
    }
    Compiled_collision_filter compiled{};
    compiled.membership    = make_bitset(filter->collision_systems);
    compiled.is_allow_list = !filter->collide_with_systems.empty();
    compiled.mask          = compiled.is_allow_list
        ? make_bitset(filter->collide_with_systems)
        : make_bitset(filter->not_collide_with_systems);
    const JPH::CollisionGroup::GroupID group_id = static_cast<JPH::CollisionGroup::GroupID>(m_compiled_filters.size());
    m_compiled_filters.push_back(compiled);
    m_filter_to_compiled.emplace(filter.get(), group_id);
    return group_id;
}

void Jolt_group_filter::set_pair_collision_enabled(
    const JPH::CollisionGroup::SubGroupID a,
    const JPH::CollisionGroup::SubGroupID b,
    const bool                            enabled
)
{
    const uint64_t key = pack_subgroup_pair(a, b);
    if (enabled) {
        m_excluded_pairs.erase(key);
    } else {
        m_excluded_pairs.insert(key);
    }
}

IWorld::~IWorld() noexcept
{
}

auto IWorld::create() -> IWorld*
{
    return new Jolt_world();
}

auto IWorld::create_shared() -> std::shared_ptr<IWorld>
{
    return std::make_shared<Jolt_world>();
}

auto IWorld::create_unique() -> std::unique_ptr<IWorld>
{
    return std::make_unique<Jolt_world>();
}

void initialize_physics_system()
{
    // Register allocation hook
    JPH::RegisterDefaultAllocator();

    // Install callbacks
    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

    // Create a factory
    JPH::Factory::sInstance = new JPH::Factory();

    JPH::RegisterTypes();
}

Jolt_world::Jolt_world()
    : m_temp_allocator{10 * 1024 * 1024}
    , m_job_system    {JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 10}
    , m_group_filter  {new Jolt_group_filter()}
{
    //m_debug_renderer              = std::make_unique<Jolt_debug_renderer             >();
    m_broad_phase_layer_interface = std::make_unique<Broad_phase_layer_interface_impl>();
    m_physics_system.Init(
        cMaxBodies,
        cNumBodyMutexes,
        cMaxBodyPairs,
        cMaxContactConstraints,
        *m_broad_phase_layer_interface.get(),
        m_collision_filter,
        m_collision_filter
    );

    m_physics_system.SetBodyActivationListener(this);
    m_physics_system.SetContactListener(this);
}

Jolt_world::~Jolt_world() noexcept = default;

void Jolt_world::update_fixed_step(const double dt)
{
    // log_physics_frame->trace("update_fixed_step()");
    // If you take larger steps than 1 / 60th of a second you need to do
    // multiple collision steps in order to keep the simulation stable.
    // Do 1 collision step per 1 / 60th of a second (round up).
    const int cCollisionSteps = 1;

    // If you want more accurate step results you can do multiple sub steps
    // within a collision step. Usually you would set this to 1.
    //const int cIntegrationSubSteps = 1;

    //float inDeltaTime, 
    // int inCollisionSteps, 
    // TempAllocator *inTempAllocator, 
    // JobSystem *inJobSystem
    m_physics_system.Update(
        static_cast<float>(dt),
        cCollisionSteps,
        //cIntegrationSubSteps,
        &m_temp_allocator,
        &m_job_system
    );

    dispatch_trigger_events();
}

void Jolt_world::dispatch_trigger_events()
{
    // Move pending events (queued by contact callbacks on Jolt worker
    // threads) to the dispatch buffer and invoke the callbacks on this
    // (caller) thread. Both vectors keep their high-water capacity: swap
    // exchanges buffers and clear() does not deallocate.
    {
        const std::lock_guard<std::mutex> lock{m_trigger_mutex};
        std::swap(m_pending_trigger_events, m_dispatch_trigger_events);
    }
    for (const Pending_trigger_event& pending : m_dispatch_trigger_events) {
        if (pending.is_enter) {
            if (m_on_trigger_enter_callback) {
                m_on_trigger_enter_callback(pending.event);
            }
        } else {
            if (m_on_trigger_exit_callback) {
                m_on_trigger_exit_callback(pending.event);
            }
        }
    }
    m_dispatch_trigger_events.clear();
}

auto Jolt_world::describe() const -> std::vector<std::string>
{
    std::vector<std::string> out;
    const auto num_constraints         = m_physics_system.GetConstraints().size();
    const auto num_active_rigid_bodies = m_physics_system.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    const auto num_bodies              = m_physics_system.GetNumBodies();
    const auto body_stats              = m_physics_system.GetBodyStats();
    out.push_back(fmt::format("num constraints = {}",      num_constraints));
    out.push_back(fmt::format("num active bodies = {}",    num_active_rigid_bodies));
    out.push_back(fmt::format("num bodies = {}",           num_bodies));
    out.push_back(fmt::format("num active dynamic = {}",   body_stats.mNumActiveBodiesDynamic));
    out.push_back(fmt::format("num dynamic = {}",          body_stats.mNumBodiesDynamic));
    out.push_back(fmt::format("num active kinematic = {}", body_stats.mNumActiveBodiesKinematic));
    out.push_back(fmt::format("num kinematic = {}",        body_stats.mNumBodiesKinematic));
    out.push_back(fmt::format("num static = {}",           body_stats.mNumBodiesStatic));
    out.push_back(fmt::format("num bodies = {}",           body_stats.mNumBodies));
    return out;
}

void Jolt_world::add_rigid_body(IRigid_body* rigid_body)
{
    auto& body_interface  = m_physics_system.GetBodyInterface();
    auto* jolt_rigid_body = reinterpret_cast<Jolt_rigid_body*>(rigid_body);

    ERHE_VERIFY(jolt_rigid_body != nullptr);

    auto* jolt_body = jolt_rigid_body->get_jolt_body();
    ERHE_VERIFY(jolt_body != nullptr);
    if (jolt_body == &JPH::Body::sFixedToWorld) {
        return;
    }


#ifndef NDEBUG
    const auto i = std::find(m_rigid_bodies.begin(), m_rigid_bodies.end(), jolt_rigid_body);
    if (i != m_rigid_bodies.end()) {
        log_physics->error("rigid body {} already in world", rigid_body->get_debug_label());
    } else
#endif
    {
        body_interface.AddBody(
            jolt_body->GetID(),
            JPH::EActivation::DontActivate
        );
        m_rigid_bodies.push_back(jolt_rigid_body);
    }

    log_physics->trace(
        "added rigid body {} id = {} (total {})",
        rigid_body->get_debug_label(),
        jolt_body->GetID().GetIndex(),
        m_physics_system.GetNumBodies()
    );
}

void Jolt_world::remove_rigid_body(IRigid_body* rigid_body)
{
    auto& body_interface  = m_physics_system.GetBodyInterface();
    auto* jolt_rigid_body = reinterpret_cast<Jolt_rigid_body*>(rigid_body);

    ERHE_VERIFY(jolt_rigid_body != nullptr);

    auto* jolt_body = jolt_rigid_body->get_jolt_body();
    ERHE_VERIFY(jolt_body != nullptr);
    if (jolt_body == &JPH::Body::sFixedToWorld) {
        return;
    }

    log_physics->trace("remove rigid body {} id = {}", rigid_body->get_debug_label(), jolt_body->GetID().GetIndex());

    const auto i = std::remove(
        m_rigid_bodies.begin(),
        m_rigid_bodies.end(),
        jolt_rigid_body
    );
    if (i == m_rigid_bodies.end()) {
        log_physics->error("rigid body {} not in world", rigid_body->get_debug_label());
    } else {
        body_interface.RemoveBody(jolt_body->GetID());
        m_rigid_bodies.erase(i, m_rigid_bodies.end());
    }

    // Synthesize trigger exit events for sensor pairs involving the removed
    // body. Jolt reports OnContactRemoved for these pairs only in the NEXT
    // update, when the removed body (and its Jolt_rigid_body wrapper) may
    // already be destroyed and its BodyID reused; the map entries are erased
    // here so that those late callbacks find nothing.
    {
        const std::lock_guard<std::mutex> lock{m_trigger_mutex};
        for (auto entry = m_sensor_contacts.begin(); entry != m_sensor_contacts.end();) {
            const Sensor_pair& pair = entry->second;
            if ((pair.sensor == jolt_rigid_body) || (pair.other == jolt_rigid_body)) {
                m_dispatch_trigger_events.push_back(
                    Pending_trigger_event{
                        .event    = Trigger_event{.sensor = pair.sensor, .other = pair.other},
                        .is_enter = false
                    }
                );
                entry = m_sensor_contacts.erase(entry);
            } else {
                ++entry;
            }
        }
    }
    // Dispatch outside the lock; remove_rigid_body is never called during
    // update_fixed_step, so the dispatch buffer is free here.
    for (const Pending_trigger_event& pending : m_dispatch_trigger_events) {
        if (m_on_trigger_exit_callback) {
            m_on_trigger_exit_callback(pending.event);
        }
    }
    m_dispatch_trigger_events.clear();
}

void Jolt_world::add_constraint(IConstraint* constraint)
{
    log_physics->trace("add constraint");
    auto* jolt_constraint = reinterpret_cast<Jolt_constraint*>(constraint);

#ifndef NDEBUG
    const auto i = std::find(m_constraints.begin(), m_constraints.end(), jolt_constraint);
    if (i != m_constraints.end()) {
        log_physics->error("constraint is already in world");
    } else
#endif
    {
        m_physics_system.AddConstraint(jolt_constraint->get_jolt_constraint());
        m_constraints.push_back(jolt_constraint);
    }
}

void Jolt_world::remove_constraint(IConstraint* constraint)
{
    log_physics->trace("remove constraint");
    auto* jolt_constraint = reinterpret_cast<Jolt_constraint*>(constraint);
    const auto i = std::remove(
        m_constraints.begin(),
        m_constraints.end(),
        jolt_constraint
    );
    if (i != m_constraints.end()) {
        m_physics_system.RemoveConstraint(jolt_constraint->get_jolt_constraint());
        m_constraints.erase(i, m_constraints.end());
    } else {
        log_physics->error("constraint is not in world");
    }
}

void Jolt_world::set_gravity(const glm::vec3& gravity)
{
    log_physics->trace("set gravity from {} to {}", m_gravity, gravity);
    m_gravity = gravity;
    m_physics_system.SetGravity(to_jolt(gravity));
}

auto Jolt_world::get_gravity() const -> glm::vec3
{
    return m_gravity;
}

auto Jolt_world::get_rigid_body_count() const -> std::size_t
{
    return m_physics_system.GetNumBodies();
}

auto Jolt_world::get_constraint_count() const -> std::size_t
{
    return m_physics_system.GetConstraints().size();
}

void Jolt_world::debug_draw(erhe::renderer::Jolt_debug_renderer& debug_renderer)
{
    static_cast<void>(debug_renderer);
#ifdef JPH_DEBUG_RENDERER
    m_physics_system.DrawBodies(JPH::BodyManager::DrawSettings{}, &debug_renderer);
    m_physics_system.DrawConstraintLimits(&debug_renderer);
    m_physics_system.DrawConstraintReferenceFrame(&debug_renderer);
    m_physics_system.DrawConstraints(&debug_renderer);
#endif
}

auto Jolt_world::get_physics_system() -> JPH::PhysicsSystem&
{
    return m_physics_system;
}

void Jolt_world::set_on_body_activated(std::function<void(IRigid_body*)> callback)
{
    m_on_body_activated_callback = callback;
}

void Jolt_world::set_on_body_deactivated(std::function<void(IRigid_body*)> callback)
{
    m_on_body_deactivated_callback = callback;
}

void Jolt_world::for_each_active_body(std::function<void(IRigid_body*)> callback)
{
    const JPH::BodyID*  active_rigid_bodies     = m_physics_system.GetActiveBodiesUnsafe(JPH::EBodyType::RigidBody);
    const JPH::uint32   active_rigid_body_count = m_physics_system.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    JPH::BodyInterface& body_interface    = m_physics_system.GetBodyInterfaceNoLock();
    for (JPH::uint32 i = 0; i < active_rigid_body_count; ++i) {
        JPH::BodyID      body_id         = active_rigid_bodies[i];
        JPH::uint64      user_data       = body_interface.GetUserData(body_id);
        Jolt_rigid_body* jolt_rigid_body = reinterpret_cast<Jolt_rigid_body*>(user_data);
        if (jolt_rigid_body == nullptr) {
            continue;
        }
        callback(jolt_rigid_body);
    }
}

void Jolt_world::OnBodyActivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData)
{
    if (!m_on_body_activated_callback) {
        return;
    }
    //const auto userdata = m_physics_system.GetBodyInterface().GetUserData(inBodyID);
    auto* body = reinterpret_cast<Jolt_rigid_body*>(inBodyUserData);
    ERHE_VERIFY(body != nullptr);
    m_on_body_activated_callback(body);

    log_physics->trace(
        "Body activated ID = {}, name = {}",
        inBodyID.GetIndex(),
        body->get_debug_label()
    );
}

void Jolt_world::OnBodyDeactivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData)
{
    if (!m_on_body_deactivated_callback) {
        return;
    }
    auto* body = reinterpret_cast<Jolt_rigid_body*>(inBodyUserData);
    ERHE_VERIFY(body != nullptr);
    m_on_body_deactivated_callback(body);
    log_physics->trace(
        "Body deactivated ID = {}, name = {}",
        inBodyID.GetIndex(),
        body->get_debug_label()
    );
}

namespace {

// Computes per-contact combined friction / restitution from the bodies'
// Physics_material_snapshots per KHR_physics_rigid_bodies (combine mode
// precedence: average > minimum > maximum > multiply). Only overrides
// ioSettings when at least one body has a material; a material-less body
// contributes its legacy scalar friction / restitution values (as both
// static and dynamic friction) with e_average combine mode. Friction uses
// dynamic friction only for now (documented approximation; static friction
// selection by tangential velocity is a follow-up).
//
// Runs on Jolt worker threads with all bodies locked: allocation-free,
// lock-free, reads POD snapshots only.
void combine_contact_material(const JPH::Body& body1, const JPH::Body& body2, JPH::ContactSettings& settings)
{
    const Jolt_rigid_body* rigid_body1 = reinterpret_cast<const Jolt_rigid_body*>(body1.GetUserData());
    const Jolt_rigid_body* rigid_body2 = reinterpret_cast<const Jolt_rigid_body*>(body2.GetUserData());
    if ((rigid_body1 == nullptr) || (rigid_body2 == nullptr)) {
        return;
    }
    const Physics_material_snapshot& snapshot1 = rigid_body1->get_material_snapshot();
    const Physics_material_snapshot& snapshot2 = rigid_body2->get_material_snapshot();
    if (!snapshot1.has_material && !snapshot2.has_material) {
        return; // legacy scalar path: keep Jolt's default combine result
    }

    const float friction1    = snapshot1.has_material ? snapshot1.dynamic_friction : body1.GetFriction();
    const float friction2    = snapshot2.has_material ? snapshot2.dynamic_friction : body2.GetFriction();
    const float restitution1 = snapshot1.has_material ? snapshot1.restitution      : body1.GetRestitution();
    const float restitution2 = snapshot2.has_material ? snapshot2.restitution      : body2.GetRestitution();

    const Combine_mode friction_combine1    = snapshot1.has_material ? snapshot1.friction_combine    : Combine_mode::e_average;
    const Combine_mode friction_combine2    = snapshot2.has_material ? snapshot2.friction_combine    : Combine_mode::e_average;
    const Combine_mode restitution_combine1 = snapshot1.has_material ? snapshot1.restitution_combine : Combine_mode::e_average;
    const Combine_mode restitution_combine2 = snapshot2.has_material ? snapshot2.restitution_combine : Combine_mode::e_average;

    const Combine_mode friction_mode    = combine(friction_combine1,    friction_combine2);
    const Combine_mode restitution_mode = combine(restitution_combine1, restitution_combine2);

    settings.mCombinedFriction    = combine_values(friction_mode,    friction1,    friction2);
    settings.mCombinedRestitution = combine_values(restitution_mode, restitution1, restitution2);
}

} // anonymous namespace

void Jolt_world::OnContactAdded(
    const JPH::Body&            inBody1,
    const JPH::Body&            inBody2,
    const JPH::ContactManifold& inManifold,
    JPH::ContactSettings&       ioSettings
)
{
    static_cast<void>(inManifold);

    combine_contact_material(inBody1, inBody2, ioSettings);

    const bool is_sensor1 = inBody1.IsSensor();
    const bool is_sensor2 = inBody2.IsSensor();
    if (is_sensor1 || is_sensor2) {
        // Cache the wrapper pointers now: bodies cannot be accessed during
        // OnContactRemoved. Trigger enter fires on the first sub-shape
        // contact of a body pair; further manifolds only bump the count.
        Jolt_rigid_body* rigid_body1 = reinterpret_cast<Jolt_rigid_body*>(inBody1.GetUserData());
        Jolt_rigid_body* rigid_body2 = reinterpret_cast<Jolt_rigid_body*>(inBody2.GetUserData());
        const uint64_t key = pack_body_pair(inBody1.GetID(), inBody2.GetID());
        const std::lock_guard<std::mutex> lock{m_trigger_mutex};
        Sensor_pair& pair = m_sensor_contacts[key];
        if (pair.contact_count == 0) {
            pair.sensor = is_sensor1 ? rigid_body1 : rigid_body2;
            pair.other  = is_sensor1 ? rigid_body2 : rigid_body1;
            m_pending_trigger_events.push_back(
                Pending_trigger_event{
                    .event    = Trigger_event{.sensor = pair.sensor, .other = pair.other},
                    .is_enter = true
                }
            );
        }
        ++pair.contact_count;
    }
}

void Jolt_world::OnContactPersisted(
    const JPH::Body&            inBody1,
    const JPH::Body&            inBody2,
    const JPH::ContactManifold& inManifold,
    JPH::ContactSettings&       ioSettings
)
{
    static_cast<void>(inManifold);

    combine_contact_material(inBody1, inBody2, ioSettings);
}

void Jolt_world::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair)
{
    // Bodies must NOT be accessed here (they may already be destroyed);
    // everything needed was cached in OnContactAdded.
    const uint64_t key = pack_body_pair(inSubShapePair.GetBody1ID(), inSubShapePair.GetBody2ID());
    const std::lock_guard<std::mutex> lock{m_trigger_mutex};
    const auto entry = m_sensor_contacts.find(key);
    if (entry == m_sensor_contacts.end()) {
        return; // not a sensor pair (or purged by remove_rigid_body)
    }
    Sensor_pair& pair = entry->second;
    --pair.contact_count;
    if (pair.contact_count <= 0) {
        m_pending_trigger_events.push_back(
            Pending_trigger_event{
                .event    = Trigger_event{.sensor = pair.sensor, .other = pair.other},
                .is_enter = false
            }
        );
        m_sensor_contacts.erase(entry);
    }
}

void Jolt_world::sanity_check()
{
}

void Jolt_world::set_on_trigger_enter(std::function<void(const Trigger_event&)> callback)
{
    m_on_trigger_enter_callback = callback;
}

void Jolt_world::set_on_trigger_exit(std::function<void(const Trigger_event&)> callback)
{
    m_on_trigger_exit_callback = callback;
}

void Jolt_world::set_collision_enabled(IRigid_body* rigid_body_a, IRigid_body* rigid_body_b, const bool enabled)
{
    auto* jolt_rigid_body_a = reinterpret_cast<Jolt_rigid_body*>(rigid_body_a);
    auto* jolt_rigid_body_b = reinterpret_cast<Jolt_rigid_body*>(rigid_body_b);
    ERHE_VERIFY(jolt_rigid_body_a != nullptr);
    ERHE_VERIFY(jolt_rigid_body_b != nullptr);

    JPH::Body* jolt_body_a = jolt_rigid_body_a->get_jolt_body();
    JPH::Body* jolt_body_b = jolt_rigid_body_b->get_jolt_body();
    if ((jolt_body_a == &JPH::Body::sFixedToWorld) || (jolt_body_b == &JPH::Body::sFixedToWorld)) {
        return; // the fixed-to-world placeholder never goes through group filters
    }

    JPH::CollisionGroup& group_a = jolt_body_a->GetCollisionGroup();
    JPH::CollisionGroup& group_b = jolt_body_b->GetCollisionGroup();
    if (!enabled) {
        // Pair exclusion requires the group filter to be consulted for this
        // pair; assign it lazily to bodies that did not have a Collision_filter.
        if (group_a.GetGroupFilter() == nullptr) {
            group_a.SetGroupFilter(m_group_filter.GetPtr());
        }
        if (group_b.GetGroupFilter() == nullptr) {
            group_b.SetGroupFilter(m_group_filter.GetPtr());
        }
    }
    m_group_filter->set_pair_collision_enabled(group_a.GetSubGroupID(), group_b.GetSubGroupID(), enabled);
}

auto Jolt_world::make_collision_group(const std::shared_ptr<Collision_filter>& filter) -> JPH::CollisionGroup
{
    const JPH::CollisionGroup::SubGroupID serial = m_next_body_serial++;
    if (filter) {
        const JPH::CollisionGroup::GroupID group_id = m_group_filter->get_or_compile(filter);
        return JPH::CollisionGroup{m_group_filter.GetPtr(), group_id, serial};
    }
    return JPH::CollisionGroup{nullptr, JPH::CollisionGroup::cInvalidGroup, serial};
}

void Jolt_world::update_collision_group(JPH::Body& body, const std::shared_ptr<Collision_filter>& filter)
{
    JPH::CollisionGroup& group = body.GetCollisionGroup();
    if (filter) {
        group.SetGroupID(m_group_filter->get_or_compile(filter));
        group.SetGroupFilter(m_group_filter.GetPtr());
    } else {
        // Keep the group filter pointer: it may still be needed for pair
        // exclusion; with an invalid GroupID and no excluded pairs
        // CanCollide() returns true.
        group.SetGroupID(JPH::CollisionGroup::cInvalidGroup);
    }
}

auto Jolt_world::create_rigid_body(const IRigid_body_create_info& create_info) -> IRigid_body*
{
    return new Jolt_rigid_body(*this, create_info);
}

auto Jolt_world::create_rigid_body_shared(const IRigid_body_create_info& create_info) -> std::shared_ptr<IRigid_body>
{
    return std::make_shared<Jolt_rigid_body>(*this, create_info);
}

} // namespace erhe::physics
