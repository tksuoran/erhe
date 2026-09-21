#pragma once

#include "erhe_physics/irigid_body.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_value.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace erhe::physics {
    class Collision_filter;
    class Physics_material;
}
namespace erhe::scene {
    class Mesh;
    class Xformable; using Node = Xformable;
}

namespace editor {

// The effective rigid-body values of one node: what a body is made from. A
// plain record, read with read_node_physics().
class Node_physics_data
{
public:
    erhe::physics::Motion_mode                       motion_mode             {erhe::physics::Motion_mode::e_none};
    bool                                             is_trigger              {false};
    std::optional<float>                             mass                    {};
    float                                            gravity_factor          {1.0f};
    glm::vec3                                        initial_linear_velocity {0.0f};
    glm::vec3                                        initial_angular_velocity{0.0f};
    glm::vec3                                        center_of_mass_offset   {0.0f};
    std::shared_ptr<erhe::physics::Physics_material> physics_material        {};
    std::shared_ptr<erhe::physics::Collision_filter> collision_filter        {};
    // The mesh a convex hull / triangle collision shape was built from. A
    // built shape keeps no reference to its source geometry, so the body
    // remembers it: the mesh is a prim of the body's own subtree, and both
    // exporters state the collider on that mesh's prim - USD's collision
    // schemas belong on the `Mesh` prim, and a glTF collider on a descendant
    // node belongs to the nearest ancestor body - so a reload finds the
    // geometry again. Unset names the body's own mesh, which is the
    // convention of a body whose prim is its geometry.
    std::shared_ptr<erhe::scene::Mesh>               collision_mesh          {};
    // True when a collision mesh was named and that mesh is gone (an undo took
    // it out of the scene while the body stayed). The built shape is kept; an
    // export falls back to the body's own mesh and says so once.
    bool                                             lost_collision_mesh     {false};
};

// The rigid body of a node, as an attached value group of the node itself
// (doc/erhe/property_system.md section 4.26,
// doc/plans/node_attachments_to_properties.md D1). The same shape USD gives
// `PhysicsRigidBodyAPI`, `PhysicsCollisionAPI`, `PhysicsMassAPI` and the
// `PhysicsMaterialAPI` binding: applied API schemas contributing plain
// attributes to the prim.
//
// Node_physics is a registration holder with static members only, not an item
// and not a Dependency_object: it owns the property registration (owner type
// Node_physics, so the qualified name is Node_physics.motion_mode) and the
// holder of the values is an erhe::scene::Node.
//
// Node_physics.motion_mode is the group's KEY property, with Motion_mode::
// e_none as its default: the node carries a rigid body exactly while something
// gives it another mode. The runtime state the group implies - the rigid body,
// the collision shape, the create-info mirror, the world registration and the
// material / filter observers - is owned by Node_physics_system, one per scene
// (doc/editor/scene.md "Node systems").
class Node_physics
{
public:
    Node_physics() = delete;

    // The registering class's owner type id. Node_physics has no instances, so
    // it is allocated directly under the root rather than by Item<>.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // The key property. Per instance, so it does not inherit: a body is a
    // property of the one prim it simulates, and a subtree below a dynamic
    // body is carried by that body, not made of bodies of its own.
    static const erhe::property::Property<erhe::physics::Motion_mode> motion_mode_property;

    // The rest of the group. These inherit, so a node above or a style holds
    // Node_physics.* for the bodies below it.
    static const erhe::property::Property<bool>                            is_trigger_property;
    static const erhe::property::Property<float>                           mass_property;
    static const erhe::property::Property<float>                           gravity_factor_property;
    static const erhe::property::Property<glm::vec3>                       initial_linear_velocity_property;
    static const erhe::property::Property<glm::vec3>                       initial_angular_velocity_property;
    static const erhe::property::Property<glm::vec3>                       center_of_mass_offset_property;
    static const erhe::property::Property<erhe::property::Object_reference> physics_material_property;
    static const erhe::property::Property<erhe::property::Object_reference> collision_filter_property;
    // Weak (D28): a body keeps no strong reference to a prim of the scene.
    static const erhe::property::Property<erhe::property::Weak_object_reference> collision_mesh_property;

    // Every Node_physics.* property, registration order, for generic walks.
    [[nodiscard]] static auto all_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

// True while the node carries the group: the key property's effective value
// differs from its default (erhe::property::carries_attached_group).
[[nodiscard]] auto carries_node_physics(const erhe::scene::Node& node) -> bool;

// The effective values of one node, or nothing when the node carries no body.
[[nodiscard]] auto read_node_physics(const erhe::scene::Node& node) -> std::optional<Node_physics_data>;

// The create info a body of `node` is made from, filled from the effective
// values. The shape, the label and the pose are the system's to fill.
[[nodiscard]] auto make_node_physics_create_info(const erhe::scene::Node& node) -> erhe::physics::IRigid_body_create_info;

// Writes the create info's fields as values of the node, each where it differs
// from the property default, and makes the node carry the group. The collision
// shape is not a value: hand it to the node's Node_physics_system
// (set_node_collision_shape) alongside this call.
void write_node_physics_create_info(erhe::scene::Node& node, const erhe::physics::IRigid_body_create_info& create_info);

// Takes the group off the node: the key property goes back to its default
// layer, so the body is destroyed and Add Property offers the key again.
void clear_node_physics(erhe::scene::Node& node);

} // namespace editor
