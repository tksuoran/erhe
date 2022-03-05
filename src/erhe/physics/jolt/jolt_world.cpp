#include "erhe/physics/jolt/jolt_world.hpp"
#include "erhe/physics/jolt/jolt_constraint.hpp"
#include "erhe/physics/jolt/jolt_rigid_body.hpp"
#include "erhe/physics/idebug_draw.hpp"
#include "erhe/physics/log.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

#include "Core/Factory.h"
#include "Core/RTTI.h"
#include "RegisterTypes.h"

namespace erhe::physics
{

// Callback for traces, connect this to your own trace function if you have one
static void TraceImpl(const char* inFMT, ...)
{
    va_list list;
    va_start(list, inFMT);
    std::array<char, 1024> buffer{};
    vsnprintf(buffer.data(), buffer.size(), inFMT, list);

    log_physics.info("{}", buffer.data());
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
    log_physics.error(
        "{}:{} ({}) {}", inFile, inLine, inExpression, (inMessage != nullptr) ? inMessage : ""
    );
	// Breakpoint
	return true;
};

#endif // JPH_ENABLE_ASSERTS

// Layer that objects can be in, determines which other objects it can collide with
// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
// layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics simulation
// but only if you do collision testing).
namespace Layers
{
    static constexpr uint8_t NON_MOVING = 0u;
    static constexpr uint8_t MOVING     = 1u;
    static constexpr uint8_t NUM_LAYERS = 2u;
};

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
        default:                 JPH_ASSERT(false); return false;
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
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return m_object_to_broad_phase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    auto GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const -> const char* override
    {
        switch ((BroadPhaseLayer::Type)inLayer)
        {
            case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
            default:                                                  JPH_ASSERT(false); return "INVALID";
        }
    }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

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
        default:                 JPH_ASSERT(false); return false;
    }
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

Jolt_world::Jolt_world()
{
    // Install callbacks
    JPH::Trace = TraceImpl;
	JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

    JPH::RegisterTypes();

	const unsigned int cMaxBodies             = 1024;
	const unsigned int cNumBodyMutexes        = 0;
	const unsigned int cMaxBodyPairs          = 1024;
	const unsigned int cMaxContactConstraints = 1024;

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

    // SetBodyActivationListener()
    // SetContactListener()
    auto& body_interface = m_physics_system.GetBodyInterface();
}

Jolt_world::~Jolt_world() = default;

void Jolt_world::enable_physics_updates()
{
    m_physics_enabled = true;
}

void Jolt_world::disable_physics_updates()
{
    m_physics_enabled = false;
}

auto Jolt_world::is_physics_updates_enabled() const -> bool
{
    return m_physics_enabled;
}

void Jolt_world::update_fixed_step(const double dt)
{
    static_cast<void>(dt);
}

void Jolt_world::add_rigid_body(IRigid_body* rigid_body)
{
    m_rigid_bodies.push_back(rigid_body);
}

void Jolt_world::remove_rigid_body(IRigid_body* rigid_body)
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

void Jolt_world::add_constraint(IConstraint* constraint)
{
    m_constraints.push_back(constraint);
}

void Jolt_world::remove_constraint(IConstraint* constraint)
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

void Jolt_world::set_gravity(const glm::vec3 gravity)
{
    m_gravity = gravity;
}

auto Jolt_world::get_gravity() const -> glm::vec3
{
    return m_gravity;
}

void Jolt_world::set_debug_drawer(IDebug_draw* debug_draw)
{
    static_cast<void>(debug_draw);
}

void Jolt_world::debug_draw()
{
}


} // namespace erhe::physics
