#include "erhe/physics/jolt/jolt_world.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/physics/jolt/jolt_constraint.hpp"
#include "erhe/physics/jolt/jolt_debug_renderer.hpp"
#include "erhe/physics/jolt/jolt_rigid_body.hpp"
#include "erhe/physics/jolt/glm_conversions.hpp"
#include "erhe/physics/idebug_draw.hpp"
#include "erhe/physics/physics_log.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>

namespace erhe::physics
{

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
static auto AssertFailedImpl(
    const char*  inExpression,
    const char*  inMessage,
    const char*  inFile,
    unsigned int inLine
) -> bool
{
    log_physics->error(
        "{}:{} ({}) {}",
        inFile,
        inLine,
        inExpression,
        (inMessage != nullptr) ? inMessage : ""
    );
    // Breakpoint
    return true;
};

#endif // JPH_ENABLE_ASSERTS

namespace Layers {

[[nodiscard]] auto get_layer(const Motion_mode motion_mode) -> uint8_t
{
    switch (motion_mode)
    {
        case Motion_mode::e_static:    return Layers::NON_MOVING;
        case Motion_mode::e_kinematic: return Layers::MOVING;
        case Motion_mode::e_dynamic:   return Layers::MOVING;
        default:                       return Layers::MOVING;
    }
}

} // namespace Layers

// Function that determines if two object layers can collide
static bool object_can_collide(
    JPH::ObjectLayer inObject1,
    JPH::ObjectLayer inObject2
)
{
    switch (inObject1)
    {
        case Layers::NON_MOVING: return inObject2 == Layers::MOVING; // Non moving only collides with moving
        case Layers::MOVING:     return true; // Moving collides with everything
        default:                 ERHE_VERIFY(false); return false;
    }
};

// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING{0};
    static constexpr JPH::BroadPhaseLayer MOVING    {1};
    static constexpr unsigned int         NUM_LAYERS{2};
};

// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class Broad_phase_layer_interface_impl final
    : public JPH::BroadPhaseLayerInterface
{
public:
    Broad_phase_layer_interface_impl()
    {
        // Create a mapping table from object to broad phase layer
        m_object_to_broad_phase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        m_object_to_broad_phase[Layers::MOVING    ] = BroadPhaseLayers::MOVING;
    }

    auto GetNumBroadPhaseLayers() const -> unsigned int override
    {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    auto GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const -> JPH::BroadPhaseLayer override
    {
        ERHE_VERIFY(inLayer < Layers::NUM_LAYERS);
        return m_object_to_broad_phase[inLayer];
    }

//#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    auto GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const -> const char* override
    {
        switch ((JPH::BroadPhaseLayer::Type)inLayer)
        {
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
            default:                                                       ERHE_VERIFY(false); return "INVALID";
        }
    }
//#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
    JPH::BroadPhaseLayer m_object_to_broad_phase[Layers::NUM_LAYERS];
};

// Function that determines if two broadphase layers can collide
static bool broad_phase_can_collide(
    JPH::ObjectLayer     inLayer1,
    JPH::BroadPhaseLayer inLayer2
)
{
    switch (inLayer1)
    {
        case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING:     return true;
        default:                 ERHE_VERIFY(false); return false;
    }
}

IWorld::~IWorld()
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

Jolt_world::Initialize_first::Initialize_first()
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
    , m_job_system{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 10}
{
    //m_debug_renderer              = std::make_unique<Jolt_debug_renderer             >();
    m_broad_phase_layer_interface = std::make_unique<Broad_phase_layer_interface_impl>();
    m_physics_system.Init(
        cMaxBodies,
        cNumBodyMutexes,
        cMaxBodyPairs,
        cMaxContactConstraints,
        *m_broad_phase_layer_interface.get(),
        broad_phase_can_collide,
        object_can_collide
    );

    m_physics_system.SetBodyActivationListener(this);
    m_physics_system.SetContactListener(this);
}

Jolt_world::~Jolt_world() = default;

void Jolt_world::enable_physics_updates()
{
    m_physics_enabled = true;
    log_physics->trace("physics updates enabled");
}

void Jolt_world::disable_physics_updates()
{
    m_physics_enabled = false;
    log_physics->trace("physics updates disabled");
}

auto Jolt_world::is_physics_updates_enabled() const -> bool
{
    return m_physics_enabled;
}

void Jolt_world::update_fixed_step(const double dt)
{
    if (!m_physics_enabled)
    {
        //log_physics_frame.info("Physics is disabled\n");
        return;
    }

    // If you take larger steps than 1 / 60th of a second you need to do
    // multiple collision steps in order to keep the simulation stable.
    // Do 1 collision step per 1 / 60th of a second (round up).
    const int cCollisionSteps = 1;

    // If you want more accurate step results you can do multiple sub steps
    // within a collision step. Usually you would set this to 1.
    const int cIntegrationSubSteps = 1;

    for (auto* rigid_body : m_rigid_bodies)
    {
        rigid_body->pre_update_motion_state();
    }

    m_physics_system.Update(
        static_cast<float>(dt),
        cCollisionSteps,
        cIntegrationSubSteps,
        &m_temp_allocator,
        &m_job_system
    );

    for (auto* rigid_body : m_rigid_bodies)
    {
        rigid_body->update_motion_state();
    }

    //const auto num_active_bodies = m_physics_system.GetNumActiveBodies();
    //const auto num_bodies        = m_physics_system.GetNumBodies();
    const auto body_stats = m_physics_system.GetBodyStats();
    //log_physics_frame.info("num active bodies = {}", num_active_bodies);
    //log_physics_frame.info("num bodies = {}", num_bodies);
    //// info_fmt(log_physics_frame, "num active dynamic = {}\n",   body_stats.mNumActiveBodiesDynamic);
    //// info_fmt(log_physics_frame, "num dynamic = {}\n",          body_stats.mNumBodiesDynamic);
    //// info_fmt(log_physics_frame, "num active kinematic = {}\n", body_stats.mNumActiveBodiesKinematic);
    //// info_fmt(log_physics_frame, "num kinematic = {}\n",        body_stats.mNumBodiesKinematic);
    //// info_fmt(log_physics_frame, "num static = {}\n",           body_stats.mNumBodiesStatic);
    //// info_fmt(log_physics_frame, "num bodies = {}\n",           body_stats.mNumBodies);
}

void Jolt_world::add_rigid_body(IRigid_body* rigid_body)
{
    log_physics->trace("add rigid body {}", rigid_body->get_debug_label());
    m_rigid_bodies.push_back(reinterpret_cast<Jolt_rigid_body*>(rigid_body));
    auto& body_interface  = m_physics_system.GetBodyInterface();
    auto* jolt_rigid_body = reinterpret_cast<Jolt_rigid_body*>(rigid_body);
    auto* jolt_body       = jolt_rigid_body->get_jolt_body();
    body_interface.AddBody(
        jolt_body->GetID(),
        JPH::EActivation::DontActivate
    );
}

void Jolt_world::remove_rigid_body(IRigid_body* rigid_body)
{
    log_physics->trace("remove rigid body {}", rigid_body->get_debug_label());
    m_rigid_bodies.erase(
        std::remove(
            m_rigid_bodies.begin(),
            m_rigid_bodies.end(),
            reinterpret_cast<Jolt_rigid_body*>(rigid_body)
        ),
        m_rigid_bodies.end()
    );
    auto& body_interface  = m_physics_system.GetBodyInterface();
    auto* jolt_rigid_body = reinterpret_cast<Jolt_rigid_body*>(rigid_body);
    auto* jolt_body       = jolt_rigid_body->get_jolt_body();
    body_interface.RemoveBody(jolt_body->GetID());
}

void Jolt_world::add_constraint(IConstraint* constraint)
{
    log_physics->error("add constraint not implemented");
    //m_constraints.push_back(constraint);
    // TODO
    static_cast<void>(constraint);
}

void Jolt_world::remove_constraint(IConstraint* constraint)
{
    log_physics->error("remove constraint not implemented");
    //m_constraints.erase(
    //    std::remove(
    //        m_constraints.begin(),
    //        m_constraints.end(),
    //        constraint
    //    ),
    //    m_constraints.end()
    //);
    static_cast<void>(constraint);
}

void Jolt_world::set_gravity(const glm::vec3 gravity)
{
    log_physics->trace("set gravity {}", gravity);
    m_gravity = gravity;
    m_physics_system.SetGravity(to_jolt(gravity));
}

auto Jolt_world::get_gravity() const -> glm::vec3
{
    return m_gravity;
}

void Jolt_world::set_debug_drawer(IDebug_draw* debug_draw)
{
    static_cast<void>(debug_draw);
    // TODO m_debug_renderer->set_debug_draw(debug_draw);
}

void Jolt_world::debug_draw()
{
    // TODO
}

auto Jolt_world::get_physics_system() -> JPH::PhysicsSystem&
{
    return m_physics_system;
}

void Jolt_world::OnBodyActivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData)
{
    //const auto userdata = m_physics_system.GetBodyInterface().GetUserData(inBodyID);
    auto* body = reinterpret_cast<Jolt_rigid_body*>(inBodyUserData);

    log_physics->trace(
        "Body activated ID = {}, name = {}",
        inBodyID.GetIndex(),
        (body != nullptr)
            ? body->get_debug_label()
            : "()"
    );
}

void Jolt_world::OnBodyDeactivated(
    const JPH::BodyID& inBodyID,
    JPH::uint64        inBodyUserData
)
{
    auto* body = reinterpret_cast<Jolt_rigid_body*>(inBodyUserData);

    log_physics->trace(
        "Body deactivated ID = {}, name = {}",
        inBodyID.GetIndex(),
        (body != nullptr)
            ? body->get_debug_label()
            : "()"
    );
}

void Jolt_world::OnContactAdded(
    const JPH::Body&            inBody1,
    const JPH::Body&            inBody2,
    const JPH::ContactManifold& inManifold,
    JPH::ContactSettings&       ioSettings
)
{
    log_physics->trace("contact added");
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
    //log_physics.info("contact persisted\n");
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

void Jolt_world::OnContactRemoved(
    const JPH::SubShapeIDPair& inSubShapePair
)
{
    static_cast<void>(inSubShapePair);
    log_physics->trace("contact removed");
    //auto& body_interface = m_physics_system.GetBodyInterface();
    //auto* jolt_body1 = reinterpret_cast<Jolt_rigid_body*>(body_interface.GetUserData(inSubShapePair.GetBody1ID()));
    //auto* jolt_body2 = reinterpret_cast<Jolt_rigid_body*>(body_interface.GetUserData(inSubShapePair.GetBody2ID()));
    //log_physics.info(
    //    "contact removed {} - {}",
    //    (jolt_body1 != nullptr) ? jolt_body1->get_debug_label() : "()",
    //    (jolt_body2 != nullptr) ? jolt_body2->get_debug_label() : "()"
    //);
}

} // namespace erhe::physics
