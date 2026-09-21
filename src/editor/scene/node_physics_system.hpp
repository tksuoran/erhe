#pragma once

#include "erhe_physics/irigid_body.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_scene/node_system.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace erhe::physics {
    class ICollision_shape;
    class IWorld;
}

namespace editor {

class Scene_root;

// Whether a body created for a node starts simulating at once. Bodies enter
// the world deactivated (quiet scene loading); a body the user just made is
// woken right after it is added, so it needs no separate wake pass.
enum class Wake_on_attach : unsigned int {
    no = 0,
    yes
};

// The runtime state one node's rigid-body values imply: the collision shape
// built for the node, the body made from the shape and the values, the create
// info it was made from and the material / filter subscriptions that keep the
// live body current.
//
// The collision shape is not a value of the group - it is built geometry, not
// an opinion - so the system remembers it for the node. It outlives the body:
// an undo takes the node out of the scene and a redo puts it back, and the
// shape built for it must still be there. An entry whose node is gone is
// purged when the next node registers.
class Node_physics_entry
{
public:
    // The node this entry belongs to. Raw while the node is registered, and
    // the weak reference tells whether the node is still alive after it left.
    erhe::scene::Node*                               node{nullptr};
    std::weak_ptr<erhe::scene::Node>                 node_weak{};
    // The shape as the body uses it: the built shape, wrapped in the
    // offset-center-of-mass wrapper while the node's center_of_mass_offset is
    // non-zero.
    std::shared_ptr<erhe::physics::ICollision_shape> collision_shape{};
    std::shared_ptr<erhe::physics::IRigid_body>      rigid_body{};
    erhe::physics::IRigid_body_create_info           create_info{};
    // The intended motion mode: always the value the node holds, even while
    // the body is temporarily overridden to kinematic for an interaction.
    erhe::physics::Motion_mode                       motion_mode{erhe::physics::Motion_mode::e_none};
    Wake_on_attach                                   wake_on_attach{Wake_on_attach::no};
    bool                                             in_world{false};
    erhe::property::Observer_token                   physics_material_observer{};
    erhe::property::Observer_token                   collision_filter_observer{};
    // Debug visualization points, filled by the physics tool.
    std::vector<glm::vec3>                           markers{};
};

// The per-scene owner of the `Node_physics` value group's runtime state
// (doc/erhe/scene.md "Node systems",
// doc/plans/node_attachments_to_properties.md D2). One of these is owned by
// each Scene_root and added to its scene, which drives it from the three
// change sites.
//
// A node has a live rigid body exactly while it carries the group
// (Node_physics.motion_mode differs from Motion_mode::e_none), is registered
// in this scene and its derived active bit is set.
class Node_physics_system : public erhe::scene::INode_system
{
public:
    explicit Node_physics_system(Scene_root& scene_root);
    ~Node_physics_system() noexcept override;

    // Implements INode_system
    void on_node_registered    (erhe::scene::Node& node) override;
    void on_node_unregistered  (erhe::scene::Node& node) override;
    void on_values_changed     (erhe::scene::Node& node, const erhe::property::Dependency_property& property) override;
    void on_node_active_changed(erhe::scene::Node& node) override;

    // The entry of `node`, or nullptr when this scene has none for it. An
    // entry exists while the node carries the group or a collision shape was
    // built for it.
    [[nodiscard]] auto find      (const erhe::scene::Node& node) const -> const Node_physics_entry*;
    [[nodiscard]] auto find      (const erhe::scene::Node& node)       ->       Node_physics_entry*;
    // The live rigid body of `node`, or nullptr.
    [[nodiscard]] auto get_rigid_body    (const erhe::scene::Node& node) const -> erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_collision_shape(const erhe::scene::Node& node) const -> std::shared_ptr<erhe::physics::ICollision_shape>;

    // Gives the node the collision shape its body is made from. Recreates a
    // live body. The shape is remembered whether or not the node carries the
    // group yet, so a caller may supply it before writing the key property and
    // before the node enters the scene.
    void set_collision_shape(erhe::scene::Node& node, const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape);

    void set_wake_on_attach(erhe::scene::Node& node, Wake_on_attach wake_on_attach);

    // Temporarily overrides the body to kinematic for user interaction; the
    // node's motion mode value is unchanged.
    void begin_interaction(const erhe::scene::Node& node);
    void end_interaction  (const erhe::scene::Node& node);

    // Snaps the body instantly to the node's current world pose with zero
    // velocities (and wakes it if dynamic), so the simulation does not react
    // with a corrective impulse. A no-op for a body that cannot produce such a
    // reaction.
    void teleport_to_node(const erhe::scene::Node& node);

    // The entries with a live body, parents before children, so a body-driven
    // node's transform is written after its ancestor's.
    [[nodiscard]] auto get_bodies() -> const std::vector<Node_physics_entry*>&;

    // Body <-> node transform write-back, called by Scene_root around the
    // simulation steps.
    void before_physics_simulation(Node_physics_entry& entry);
    void after_physics_simulation (Node_physics_entry& entry);

private:
    // The entry of `node`, created when there is none. The caller has already
    // decided the node needs one.
    [[nodiscard]] auto get_or_create(erhe::scene::Node& node) -> Node_physics_entry&;
    // Drops an entry that holds neither a body nor a shape worth remembering.
    void drop_if_empty(erhe::scene::Node& node);
    // Entries whose node died are of no use to anyone.
    void purge_expired();

    // Creates the body and adds it to the world when the node carries the
    // group, is active and this scene has a physics world; a no-op otherwise.
    void create_body (erhe::scene::Node& node, Node_physics_entry& entry);
    // Removes the body from the world and drops it, after tearing down the
    // joint constraints that referenced it.
    void destroy_body(Node_physics_entry& entry);
    // Destroy plus create, for a change only a fresh body realizes.
    void recreate_body(erhe::scene::Node& node, Node_physics_entry& entry);

    // Refreshes the create-info mirror from the node's effective values.
    void refresh_create_info(const erhe::scene::Node& node, Node_physics_entry& entry);
    // The mode the body is created with: the intended one, except that a
    // static trigger body is created kinematic non-physical (Jolt sensors must
    // be non-static to detect static bodies).
    [[nodiscard]] static auto get_effective_motion_mode(const Node_physics_entry& entry) -> erhe::physics::Motion_mode;

    // Subscribes the entry to its current material / filter; the subscription
    // dies with the entry, so the callback never outlives it.
    void observe_physics_material(Node_physics_entry& entry);
    void observe_collision_filter(Node_physics_entry& entry);
    void reapply_physics_material(Node_physics_entry& entry);
    void reapply_collision_filter(Node_physics_entry& entry);

    Scene_root&                                                       m_scene_root;
    std::unordered_map<const erhe::scene::Node*, Node_physics_entry>  m_entries;
    std::vector<Node_physics_entry*>                                  m_bodies;
    bool                                                              m_bodies_sorted{false};
};

// The physics system of the scene holding `node`, or nullptr when the node is
// in no scene.
[[nodiscard]] auto find_node_physics_system(const erhe::scene::Node& node) -> Node_physics_system*;

// The collision shape built for `node`, through its scene's system; null when
// the node is in no scene or has none.
[[nodiscard]] auto get_node_collision_shape(const erhe::scene::Node& node) -> std::shared_ptr<erhe::physics::ICollision_shape>;

// The live rigid body of `node`, through its scene's system; null when there
// is none.
[[nodiscard]] auto get_node_rigid_body(const erhe::scene::Node& node) -> erhe::physics::IRigid_body*;

// Hands `node`'s scene's system the collision shape. A no-op when the node is
// in no scene.
void set_node_collision_shape(erhe::scene::Node& node, const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape);

} // namespace editor
