#pragma once

#include "erhe_physics/iworld.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace erhe::physics {

enum class Motion_mode : unsigned int;

class Collision_filter;
class ICollision_shape;
class Jolt_rigid_body;
class Jolt_constraint;
//class Jolt_debug_renderer;

// Layer that objects can be in, determines which other objects it can collide with
// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
// layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics simulation
// but only if you do collision testing).
namespace Layers {

static constexpr uint8_t NON_MOVING    = 0u;
static constexpr uint8_t MOVING        = 1u;
static constexpr uint8_t NON_COLLIDING = 2u;
static constexpr uint8_t NUM_LAYERS    = 3u;

[[nodiscard]] auto get_layer(Motion_mode motion_mode) -> uint8_t;

}

class Jolt_collision_filter
    : public JPH::ObjectVsBroadPhaseLayerFilter
    , public JPH::ObjectLayerPairFilter
{
    // Implements JPH::ObjectVsBroadPhaseLayerFilter
    [[nodiscard]] auto ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const -> bool override;

    // Implements JPH::ObjectLayerPairFilter
    [[nodiscard]] auto ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const -> bool override;
};

// Collision_filter compiled into bitsets over interned collision-system
// strings (at most 64 systems per world; see Jolt_group_filter).
class Compiled_collision_filter
{
public:
    uint64_t membership   {0};     // systems this filter's body belongs to
    uint64_t mask         {0};     // collide_with (allow list) or not_collide_with (deny list) systems
    bool     is_allow_list{false};
};

// Single world-owned JPH::GroupFilter implementing both KHR_physics_rigid_bodies
// collision-system filters and per-pair collision exclusion. Consulted by the
// Jolt narrow phase for body pairs where at least one body's CollisionGroup
// carries this filter. Per-body CollisionGroup usage:
// - GroupID:    index into m_compiled_filters, or JPH::CollisionGroup::cInvalidGroup
//               when the body has no Collision_filter (collides with everything).
// - SubGroupID: world-assigned monotonically increasing body serial, used as
//               key for pair exclusion.
//
// THREAD SAFETY: CanCollide() is called concurrently from Jolt worker threads
// during PhysicsSystem::Update() and performs read-only lookups. All mutating
// methods (get_or_compile(), set_pair_collision_enabled()) must only be
// called outside IWorld::update_fixed_step() (body creation, filter
// assignment, constraint add/remove - all cold paths).
class Jolt_group_filter final : public JPH::GroupFilter
{
public:
    // Implements JPH::GroupFilter
    auto CanCollide(const JPH::CollisionGroup& group1, const JPH::CollisionGroup& group2) const -> bool override;

    // Returns the compiled-filter GroupID for the given filter item, compiling
    // it on first use. Identical Collision_filter items (by pointer) share one
    // compiled entry. Compiled entries snapshot the item's current contents;
    // editing a live Collision_filter requires re-assigning it to bodies.
    // (Entries are never removed; the table is bounded by the number of
    // distinct filter items ever assigned.)
    [[nodiscard]] auto get_or_compile(const std::shared_ptr<Collision_filter>& filter) -> JPH::CollisionGroup::GroupID;

    void set_pair_collision_enabled(JPH::CollisionGroup::SubGroupID a, JPH::CollisionGroup::SubGroupID b, bool enabled);

private:
    [[nodiscard]] auto intern_system(const std::string& name) -> int; // bit index, or -1 when over the 64-system cap
    [[nodiscard]] auto make_bitset  (const std::vector<std::string>& names) -> uint64_t;
    [[nodiscard]] auto does_collide (JPH::CollisionGroup::GroupID a, JPH::CollisionGroup::GroupID b) const -> bool;

    std::vector<std::string>                                                  m_system_names;      // interned system strings; bit index = vector index; cap 64
    std::vector<Compiled_collision_filter>                                    m_compiled_filters;
    std::unordered_map<const Collision_filter*, JPH::CollisionGroup::GroupID> m_filter_to_compiled;
    std::unordered_set<uint64_t>                                              m_excluded_pairs;    // packed ordered SubGroupID pairs
};

class Jolt_world
    : public IWorld
    , public JPH::BodyActivationListener
    , public JPH::ContactListener
{
public:
    Jolt_world();
    ~Jolt_world() noexcept override;

    // Implements IWorld
    auto create_rigid_body       (const IRigid_body_create_info& create_info) -> IRigid_body*                 override;
    auto create_rigid_body_shared(const IRigid_body_create_info& create_info) -> std::shared_ptr<IRigid_body> override;

    auto get_gravity         () const -> glm::vec3                override;
    auto get_rigid_body_count() const -> std::size_t              override;
    auto get_constraint_count() const -> std::size_t              override;
    auto describe            () const -> std::vector<std::string> override;
    void update_fixed_step   (double dt)                          override;
    void set_gravity         (const glm::vec3& gravity)           override;
    void add_rigid_body      (IRigid_body* rigid_body)            override;
    void remove_rigid_body   (IRigid_body* rigid_body)            override;
    void add_constraint      (IConstraint* constraint)            override;
    void remove_constraint   (IConstraint* constraint)            override;
    void debug_draw          (erhe::renderer::Jolt_debug_renderer& jolt_debug_renderer)    override;
    void sanity_check        ()                                   override;

    void set_on_body_activated  (std::function<void(IRigid_body*)> callback) override;
    void set_on_body_deactivated(std::function<void(IRigid_body*)> callback) override;
    void for_each_active_body   (std::function<void(IRigid_body*)> callback) override;

    void set_on_trigger_enter   (std::function<void(const Trigger_event&)> callback) override;
    void set_on_trigger_exit    (std::function<void(const Trigger_event&)> callback) override;
    void set_collision_enabled  (IRigid_body* rigid_body_a, IRigid_body* rigid_body_b, bool enabled) override;

    // Implements BodyActivationListener
    void OnBodyActivated  (const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) override;
    void OnBodyDeactivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) override;

    // Implements ContactListener
    //auto OnContactValidate (const JPH::Body& inBody1, const JPH::Body& inBody2, const CollideShapeResult &inCollisionResult) -> JPH::ValidateResult override
    //{
    //    return ValidateResult::AcceptAllContactsForThisBodyPair;
    //}

    void OnContactAdded    (const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override;
    void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override;
    void OnContactRemoved  (const JPH::SubShapeIDPair& inSubShapePair) override;

    // Public API
    [[nodiscard]] auto get_physics_system() -> JPH::PhysicsSystem&;

    // Builds the CollisionGroup for a body being created: assigns a unique
    // SubGroupID serial, and (when filter is non-null) the world group filter
    // plus a compiled GroupID. Must be called outside update_fixed_step().
    [[nodiscard]] auto make_collision_group(const std::shared_ptr<Collision_filter>& filter) -> JPH::CollisionGroup;

    // Re-assigns the compiled filter of a live body, keeping its SubGroupID
    // serial. Must be called outside update_fixed_step().
    void update_collision_group(JPH::Body& body, const std::shared_ptr<Collision_filter>& filter);

private:
    void dispatch_trigger_events();

    // Sensor overlap bookkeeping: one entry per (sensor, other) body pair
    // currently in contact, counting sub-shape contacts so that enter / exit
    // events fire once per body pair even when Jolt reports multiple
    // manifolds. Body wrapper pointers are cached at OnContactAdded time
    // because bodies must not be accessed during OnContactRemoved.
    class Sensor_pair
    {
    public:
        Jolt_rigid_body* sensor       {nullptr};
        Jolt_rigid_body* other        {nullptr};
        int              contact_count{0};
    };

    class Pending_trigger_event
    {
    public:
        Trigger_event event;
        bool          is_enter{false};
    };

    static constexpr unsigned int cMaxBodies             = 1024 * 32;
    static constexpr unsigned int cNumBodyMutexes        = 0;
    static constexpr unsigned int cMaxBodyPairs          = 1024 * 8;
    static constexpr unsigned int cMaxContactConstraints = 1024 * 4;

    glm::vec3                                      m_gravity        {0.0f};
    const Jolt_collision_filter                    m_collision_filter;

    JPH::TempAllocatorImpl                         m_temp_allocator;
    JPH::JobSystemThreadPool                       m_job_system;
    std::unique_ptr<JPH::BroadPhaseLayerInterface> m_broad_phase_layer_interface;
    JPH::PhysicsSystem                             m_physics_system;
    //std::unique_ptr<Jolt_debug_renderer>           m_debug_renderer;

    std::function<void(Jolt_rigid_body*)>          m_on_body_activated_callback;
    std::function<void(Jolt_rigid_body*)>          m_on_body_deactivated_callback;

    std::vector<Jolt_rigid_body*>                  m_rigid_bodies;
    std::vector<Jolt_constraint*>                  m_constraints;

    std::vector<std::shared_ptr<ICollision_shape>> m_collision_shapes;

    // Collision-system filters / pair exclusion. The group filter object is
    // shared with body CollisionGroups via Jolt ref counting; it mutates only
    // outside update_fixed_step() (see Jolt_group_filter).
    JPH::Ref<Jolt_group_filter>                    m_group_filter;
    JPH::CollisionGroup::SubGroupID                m_next_body_serial{0};

    // Trigger (sensor) events. Contact callbacks run concurrently on Jolt
    // worker threads during update_fixed_step(), so the pending containers
    // are mutex guarded. Containers are cleared (capacity kept), never
    // reassigned; the map allocates only when a new sensor pair appears.
    std::function<void(const Trigger_event&)>      m_on_trigger_enter_callback;
    std::function<void(const Trigger_event&)>      m_on_trigger_exit_callback;
    std::mutex                                     m_trigger_mutex;
    std::unordered_map<uint64_t, Sensor_pair>      m_sensor_contacts;        // key: packed (body1, body2) BodyID pair
    std::vector<Pending_trigger_event>             m_pending_trigger_events; // filled by contact callbacks
    std::vector<Pending_trigger_event>             m_dispatch_trigger_events;// drained on the update_fixed_step caller thread
};

} // namespace erhe::physics
