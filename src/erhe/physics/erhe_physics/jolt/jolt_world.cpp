#include "erhe_physics/jolt/jolt_world.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_physics/jolt/jolt_constraint.hpp"
#include "erhe_physics/jolt/jolt_rigid_body.hpp"
#include "erhe_physics/jolt/glm_conversions.hpp"
#include "erhe_physics/idebug_draw.hpp"
#include "erhe_physics/physics_log.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_verify/verify.hpp"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/Body.h>

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
    log_physics_frame->trace("update_fixed_step()");
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

void Jolt_world::debug_draw(erhe::renderer::Debug_renderer& debug_renderer)
{
    m_physics_system.DrawBodies(JPH::BodyManager::DrawSettings{}, &debug_renderer);
    m_physics_system.DrawConstraintLimits(&debug_renderer);
    m_physics_system.DrawConstraintReferenceFrame(&debug_renderer);
    m_physics_system.DrawConstraints(&debug_renderer);
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

void Jolt_world::OnContactAdded(
    const JPH::Body&            inBody1,
    const JPH::Body&            inBody2,
    const JPH::ContactManifold& inManifold,
    JPH::ContactSettings&       ioSettings
)
{
    //log_physics->trace("contact added");
    static_cast<void>(inBody1);
    static_cast<void>(inBody2);
    static_cast<void>(inManifold);
    static_cast<void>(ioSettings);
    //auto* jolt_body1 = reinterpret_cast<Jolt_rigid_body*>(inBody1.GetUserData());
    //auto* jolt_body2 = reinterpret_cast<Jolt_rigid_body*>(inBody1.GetUserData());
    //log_physics.info(
    //    "contact added {} - {}",
    //    (jolt_body1 != nullptr) ? jolt_body1->get_debug_label() : "()",
    //    (jolt_body2 != nullptr) ? jolt_body2->get_debug_label() : "()"
    //);
}

void Jolt_world::OnContactPersisted(
    const JPH::Body&            inBody1,
    const JPH::Body&            inBody2,
    const JPH::ContactManifold& inManifold,
    JPH::ContactSettings&       ioSettings
)
{
    //log_physics->trace("contact persisted");
    static_cast<void>(inBody1);
    static_cast<void>(inBody2);
    static_cast<void>(inManifold);
    static_cast<void>(ioSettings);
    //auto* jolt_body1 = reinterpret_cast<Jolt_rigid_body*>(inBody1.GetUserData());
    //auto* jolt_body2 = reinterpret_cast<Jolt_rigid_body*>(inBody1.GetUserData());
    //log_physics.info(
    //    "contact persisted {} - {}",
    //    (jolt_body1 != nullptr) ? jolt_body1->get_debug_label() : "()",
    //    (jolt_body2 != nullptr) ? jolt_body2->get_debug_label() : "()"
    //);
}

void Jolt_world::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair)
{
    static_cast<void>(inSubShapePair);
    //log_physics->trace("contact removed");
    //auto& body_interface = m_physics_system.GetBodyInterface();
    //auto* jolt_body1 = reinterpret_cast<Jolt_rigid_body*>(body_interface.GetUserData(inSubShapePair.GetBody1ID()));
    //auto* jolt_body2 = reinterpret_cast<Jolt_rigid_body*>(body_interface.GetUserData(inSubShapePair.GetBody2ID()));
    //log_physics.info(
    //    "contact removed {} - {}",
    //    (jolt_body1 != nullptr) ? jolt_body1->get_debug_label() : "()",
    //    (jolt_body2 != nullptr) ? jolt_body2->get_debug_label() : "()"
    //);
}

void Jolt_world::sanity_check()
{
}

auto Jolt_world::create_rigid_body(
    const IRigid_body_create_info& create_info,
    glm::vec3                      position,
    glm::quat                      orientation
) -> IRigid_body*
{
    return new Jolt_rigid_body(*this, create_info, position, orientation);
}

auto Jolt_world::create_rigid_body_shared(
    const IRigid_body_create_info& create_info,
    glm::vec3                      position,
    glm::quat                      orientation
) -> std::shared_ptr<IRigid_body>
{
    return std::make_shared<Jolt_rigid_body>(*this, create_info, position, orientation);
}

} // namespace erhe::physics
