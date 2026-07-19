#pragma once

#include "erhe_physics/box3d/box3d_collision_filter_table.hpp"
#include "erhe_physics/iworld.hpp"

#include <box3d/box3d.h>

#include <unordered_map>
#include <utility>
#include <vector>

namespace erhe::physics {

class Box3d_rigid_body;

// Box3D backend for erhe::physics.
//
// Deferred / unsupported, all logged where they are hit:
//
// | Feature                              | Status                    | Why
// | ------------------------------------ | ------------------------- | ---
// | IWorld::debug_draw                   | no-op                     | the signature names erhe::renderer::Jolt_debug_renderer; neutralizing it is deferred
// | save_state / restore_state           | not implemented, warns    | Box3D exposes no world snapshot API
// | static friction                      | ignored, dynamic used     | b3SurfaceMaterial carries a single friction
// | independent 6-DOF joints             | approximated              | Box3D has no generic six-DOF joint
// | universal joints (2 rotational DOF)  | approximated by spherical | no equivalent; the third axis stays free
// | multi-axis translation limits        | unsupported, warns        | no equivalent
// | rotation limits beyond +/-0.99 pi    | clamped                   | Box3D range limit
// | nested offset-center-of-mass         | ignored, errors           | Box3D carries the center of mass on the body
// | rotated mesh inside a compound       | rotation ignored, warns   | b3CreateMeshShape takes a scale but no transform
// | mesh on a dynamic body               | forced static, errors     | Box3D has no b3ComputeMeshMass
// | penetration tolerance versus meshes  | ignored (boolean overlap) | no public hull-versus-mesh manifold
// | non-uniform scale on sphere/capsule  | inscribed hull            | an ellipsoid is not representable
// | non-uniform scale over a rotation    | approximated, warns       | the exact result is a shear; same as Jolt
// | collision systems per world          | 64                        | interned into a uint64 bitset
// | body woken without moving            | reported on first move    | synthesized from b3BodyMoveEvent
// | height fields                        | not exposed               | erhe has no height field shape type
class Box3d_world : public IWorld
{
public:
    Box3d_world();
    ~Box3d_world() noexcept override;

    Box3d_world           (const Box3d_world&) = delete;
    Box3d_world& operator=(const Box3d_world&) = delete;

    // Implements IWorld
    auto create_rigid_body       (const IRigid_body_create_info& create_info) -> IRigid_body*                 override;
    auto create_rigid_body_shared(const IRigid_body_create_info& create_info) -> std::shared_ptr<IRigid_body> override;

    auto get_gravity         () const -> glm::vec3                override;
    auto get_rigid_body_count() const -> std::size_t              override;
    auto get_constraint_count() const -> std::size_t              override;
    auto describe            () const -> std::vector<std::string> override;

    void update_fixed_step      (double dt)                                             override;
    void add_rigid_body         (IRigid_body* rigid_body)                               override;
    void remove_rigid_body      (IRigid_body* rigid_body)                               override;
    void add_constraint         (IConstraint* constraint)                               override;
    void remove_constraint      (IConstraint* constraint)                               override;
    void set_gravity            (const glm::vec3& gravity)                              override;
    void debug_draw             (erhe::renderer::Jolt_debug_renderer& debug_renderer)   override;
    void sanity_check           ()                                                      override;
    void set_on_body_activated  (std::function<void(IRigid_body*)> callback)            override;
    void set_on_body_deactivated(std::function<void(IRigid_body*)> callback)            override;
    void for_each_active_body   (std::function<void(IRigid_body*)> callback)            override;
    void set_on_trigger_enter   (std::function<void(const Trigger_event&)> callback)    override;
    void set_on_trigger_exit    (std::function<void(const Trigger_event&)> callback)    override;
    void set_collision_enabled  (IRigid_body* rigid_body_a, IRigid_body* rigid_body_b, bool enabled) override;

    auto save_state   () -> std::unique_ptr<State> override;
    void restore_state(State& state)               override;

    auto would_bodies_intersect(
        const IRigid_body& body_a, const Transform& transform_a,
        const IRigid_body& body_b, const Transform& transform_b,
        float penetration_tolerance
    ) const -> bool override;

    auto would_body_intersect_world(
        const IRigid_body& body, const Transform& transform,
        float penetration_tolerance
    ) const -> bool override;

    // Box3D specific
    [[nodiscard]] auto get_box3d_world  () const -> b3WorldId                    { return m_world; }
    [[nodiscard]] auto get_filter_table ()       -> Box3d_collision_filter_table& { return m_filter_table; }

    // Box3D joints require two valid bodies (src/joint.c rejects b3_nullBodyId),
    // so a six-DOF constraint with rigid_body_b == nullptr ("constrain to
    // world") is anchored to this lazily created static, shapeless body at the
    // origin. Joint frames are body-origin relative, so a world-space frame
    // passes through it unchanged.
    [[nodiscard]] auto get_or_create_world_anchor_body() -> b3BodyId;

    // Drops any filter joints referencing this body. Box3D destroys a body's
    // joints along with the body, so the bookkeeping must be purged BEFORE
    // b3DestroyBody or a later re-enable would use a dangling b3JointId.
    void forget_filter_joints_for_body(const Box3d_rigid_body* rigid_body);

private:
    // Box3D's contact filter hook. Fires when either shape has custom
    // filtering enabled, and unlike the friction / restitution callbacks it
    // does take a context pointer.
    [[nodiscard]] static auto custom_filter_callback(b3ShapeId shape_id_a, b3ShapeId shape_id_b, void* context) -> bool;

    // Ordered pair of body pointers, so a pair has one canonical key.
    using Body_pair_key = std::pair<const Box3d_rigid_body*, const Box3d_rigid_body*>;

    class Body_pair_hash
    {
    public:
        [[nodiscard]] auto operator()(const Body_pair_key& key) const -> std::size_t;
    };

    b3WorldId                    m_world      {};
    b3BodyId                     m_world_anchor_body{};
    bool                         m_has_world_anchor_body{false};
    std::vector<IRigid_body*>    m_rigid_bodies;
    std::vector<IConstraint*>    m_constraints;
    Box3d_collision_filter_table m_filter_table;

    // Per-pair collision exclusion (joint enableCollision = false) uses Box3D
    // filter joints, which exist precisely to disable collision between two
    // bodies. Indexed by body as well so removal can purge them.
    std::unordered_map<Body_pair_key, b3JointId, Body_pair_hash>                 m_filter_joints;
    std::unordered_map<const Box3d_rigid_body*, std::vector<Body_pair_key>>      m_filter_joints_by_body;

    std::function<void(IRigid_body*)>         m_on_body_activated_callback;
    std::function<void(IRigid_body*)>         m_on_body_deactivated_callback;
    std::function<void(const Trigger_event&)> m_on_trigger_enter_callback;
    std::function<void(const Trigger_event&)> m_on_trigger_exit_callback;

    bool m_save_state_warning_logged{false};
};

} // namespace erhe::physics
