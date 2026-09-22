#pragma once

#include "erhe_physics/iconstraint.hpp"
#include "erhe_property/dependency_object.hpp"

#include <array>
#include <memory>
#include <vector>

namespace erhe::physics {
    class IConstraint;
    class IRigid_body;
    class IWorld;
}

namespace editor {

class Joint;
class Node_physics_entry;
class Scene_root;

// What the live constraint of a `Joint` was built from, read by the
// joint-space projection of Physics_drag_constraint. The physics entry
// pointers stay valid while the constraint lives: the scene's
// Node_physics_system tears the constraint down before a referenced body
// leaves the world.
class Joint_constraint_state
{
public:
    const Node_physics_entry*                           node_physics_a{nullptr};
    const Node_physics_entry*                           node_physics_b{nullptr}; // nullptr = world
    erhe::physics::Transform                            frame_in_a{};            // in body A node space
    erhe::physics::Transform                            frame_in_b{};            // in body B node space, or world space when B is the world
    std::array<erhe::physics::Constraint_axis_limit, 6> limits{};                // 0..2 translation XYZ, 3..5 rotation XYZ
};

// The runtime state one `Joint` prim implies: the six-dof constraint built
// from the joint's four values, the two bodies it references, and the
// subscription that lets an edit of the shared settings item reach it.
//
// The entry keys the prim by raw pointer and holds a weak reference to it, so
// the system never keeps a prim of a closed scene alive
// (doc/editor/scene.md, the scene-close rule).
class Joint_entry
{
public:
    Joint*                                      joint{nullptr};
    std::weak_ptr<Joint>                        joint_weak{};
    std::shared_ptr<erhe::physics::IConstraint> constraint{};
    // Bodies the live constraint references; valid only while constraint is
    // set (used for collision pair restore and teardown matching).
    erhe::physics::IRigid_body*                 rigid_body_a{nullptr};
    erhe::physics::IRigid_body*                 rigid_body_b{nullptr}; // nullptr = world
    bool                                        collision_pair_disabled{false};
    Joint_constraint_state                      state{};               // valid only while constraint is set
    // Any-property observer (D21) on the resolved settings item, with the
    // constraint rebuild as its callback, so an edit of the shared settings
    // from any writer reaches the live constraint
    // (doc/erhe/property_system.md section 4.22).
    erhe::property::Observer_token               settings_observer{};
};

// The per-scene owner of the constraints the scene's `Joint` prims imply
// (doc/erhe/scene.md "Node systems",
// doc/erhe/property_system.md section 4.17). One of these is owned by each
// Scene_root, which reports every `Joint` prim entering and leaving its tree
// (Item_host::register_prim).
//
// A joint has a live constraint exactly while its prim is registered in this
// scene, its derived active bit is set, the scene has a physics world, and
// both frame nodes resolve. A joint that cannot be built yet stays registered
// and pending: Node_physics_system retries the pending ones whenever a rigid
// body is created, and tears down the constraints referencing a body that is
// about to leave the world.
class Joint_system
{
public:
    explicit Joint_system(Scene_root& scene_root);
    ~Joint_system() noexcept;

    // The three change sites the scene drives this system from.
    void register_joint         (const std::shared_ptr<Joint>& joint);
    void unregister_joint       (const std::shared_ptr<Joint>& joint);
    void on_values_changed      (Joint& joint);
    void on_joint_active_changed(Joint& joint);

    // Tears the constraint down and builds it again, re-capturing the joint
    // frames from the current node transforms.
    void rebuild(Joint& joint);

    [[nodiscard]] auto find                (const Joint& joint) const -> const Joint_entry*;
    [[nodiscard]] auto get_constraint      (const Joint& joint) const -> erhe::physics::IConstraint*;
    [[nodiscard]] auto get_constraint_state(const Joint& joint) const -> const Joint_constraint_state*;

    // Every registered joint, live or pending, in registration order (read at
    // drag start by the joint-space projection). The entries are held by
    // pointer so a registration never moves the state a caller is reading.
    [[nodiscard]] auto get_entries() const -> const std::vector<std::unique_ptr<Joint_entry>>&;

    // True while a live constraint of this scene references rigid_body.
    [[nodiscard]] auto is_jointed_rigid_body(const erhe::physics::IRigid_body* rigid_body) const -> bool;

    // Called by Node_physics_system after a rigid body was created: a pending
    // joint may have been waiting for exactly that body.
    void retry_pending_constraints();
    // Called by Node_physics_system before a rigid body leaves the world:
    // tears down every constraint referencing it, returning those joints to
    // the pending state.
    void handle_rigid_body_removed(erhe::physics::IRigid_body* rigid_body);

private:
    [[nodiscard]] auto find      (const Joint& joint)       -> Joint_entry*;
    [[nodiscard]] auto get_world () const -> erhe::physics::IWorld*;

    // Creates the constraint when everything it needs is there; leaves the
    // entry pending (returns false) otherwise. Returns true when a constraint
    // exists afterwards.
    auto try_create_constraint(Joint_entry& entry) -> bool;
    void destroy_constraint   (Joint_entry& entry);
    void observe_settings     (Joint_entry& entry);

    Scene_root&                              m_scene_root;
    std::vector<std::unique_ptr<Joint_entry>> m_entries;
};

} // namespace editor
