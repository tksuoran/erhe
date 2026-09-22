#pragma once

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_scene/imageable.hpp"

#include <memory>

namespace erhe::physics {
    class IConstraint;
    class IRigid_body;
    class Physics_joint_settings;
}
namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class Joint_constraint_state;
class Joint_system;

// A joint prim (doc/erhe/property_system.md section 4.17): the erhe
// class of the UsdPhysics joint prims, which derive `UsdGeomImageable`, so it
// carries `visible` and `purpose` and no transform of its own. It sits
// anywhere in the hierarchy; the importers place it below the prim whose body
// is its first party, and the Create menu places it below the active item.
//
// `body_0` and `body_1` name the two FRAME nodes, not the two bodies: the
// party of a frame node is the nearest self-or-ancestor rigid body of it, and
// the joint frame is that node's world transform expressed in the body node's
// space. `body_1` unset (or naming a node with no body on its chain) anchors
// the joint to the world at `body_1`'s frame, or at `body_0`'s frame when no
// second node is named. That is the two-node model the glTF carrier (joint
// node + `connectedNode`) and the USD reader (`<joint>_frame0` /
// `_frame1`) already use, and any number of joints may name one body.
//
// The live constraint is not held here: it is runtime state of the scene,
// owned by `Joint_system` and keyed by this prim
// (doc/editor/scene.md "Node systems", D2).
class Joint
    : public erhe::Item<
        erhe::Item_base,
        erhe::scene::Imageable,
        Joint,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Joint();
    explicit Joint(const Joint& src);
    Joint& operator=(const Joint& src);
    explicit Joint(std::string_view name);
    Joint(
        std::string_view                                              name,
        const std::shared_ptr<erhe::scene::Node>&                     body_0,
        const std::shared_ptr<erhe::scene::Node>&                     body_1,
        const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings,
        bool                                                          enable_collision
    );
    Joint(const Joint& src, erhe::for_clone);
    ~Joint() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Joint"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::scene::Imageable::get_static_type() | erhe::Item_type::joint;
    }

    // Overrides Typed: the class fixes the USD typeName token. erhe simulates
    // every joint as the general six-dof `PhysicsJoint`; the subclasses a file
    // states are read into the same prim with their limits.
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "PhysicsJoint"; }

    // Overrides Item_base: a joint whose derived active bit changed leaves or
    // enters the simulation.
    void handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits) override;

    // The four values the constraint is built from. `body_0` / `body_1` are
    // weak object references (D28): a joint keeps no strong reference to a
    // prim of the scene, so a joint naming a node in its own subtree forms no
    // cycle a scene close cannot break. They are per instance and do not
    // inherit. `joint_settings` and `enable_collision` inherit from the prim
    // chain (D30), so a scope or a style states them for the joints below it.
    static const erhe::property::Property<erhe::property::Weak_object_reference> body_0_property;
    static const erhe::property::Property<erhe::property::Weak_object_reference> body_1_property;
    static const erhe::property::Property<erhe::property::Object_reference>      joint_settings_property;
    static const erhe::property::Property<bool>                                  enable_collision_property;

    // Implements Dependency_object: a change of one of the four rebuilds the
    // constraint. The properties inherited from Item_base (name, visible,
    // ...) do not: a rebuild re-captures the joint frames from the current
    // poses and settles both bodies, so renaming a joint or hiding it would
    // otherwise stop a swinging body dead.
    void on_property_changed(const erhe::property::Property_changed_args& args) override;

    [[nodiscard]] auto get_body_0          () const -> std::shared_ptr<erhe::scene::Node>;
    void               set_body_0          (const std::shared_ptr<erhe::scene::Node>& node);
    [[nodiscard]] auto get_body_1          () const -> std::shared_ptr<erhe::scene::Node>;
    void               set_body_1          (const std::shared_ptr<erhe::scene::Node>& node);
    [[nodiscard]] auto get_settings        () const -> std::shared_ptr<erhe::physics::Physics_joint_settings>;
    void               set_settings        (const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings);
    [[nodiscard]] auto get_enable_collision() const -> bool;
    void               set_enable_collision(bool enable_collision);

    // The live constraint of this joint through its scene's joint system, and
    // what it was built from; null while the joint has none (no scene, no
    // body yet, inactive).
    [[nodiscard]] auto get_constraint      () const -> erhe::physics::IConstraint*;
    [[nodiscard]] auto get_constraint_state() const -> const Joint_constraint_state*;

    // Tears the constraint down and builds it again, re-capturing the joint
    // frames from the current node transforms. Called after moving one of the
    // frame nodes; an edit of one of the four values, or of the shared
    // settings item, rebuilds on its own.
    void rebuild();
};

// The joint system of the scene holding `joint`, or nullptr when the joint is
// in no scene.
[[nodiscard]] auto find_joint_system(const Joint& joint) -> Joint_system*;

} // namespace editor
