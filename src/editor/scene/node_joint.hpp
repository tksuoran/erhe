#pragma once

#include "erhe_scene/node_attachment.hpp"

#include <memory>

namespace erhe          { class Item_host; }
namespace erhe::physics {
    class IConstraint;
    class IRigid_body;
    class IWorld;
    class Physics_joint_settings;
}
namespace erhe::scene   { class Node; }

namespace editor {

// Node attachment that joins the nearest self-or-ancestor rigid body of its
// node (body A) to the nearest self-or-ancestor rigid body of a connected
// node (body B; no connected node or no body found = world) with a six-dof
// constraint built from shared Physics_joint_settings
// (KHR_physics_rigid_bodies joint).
//
// The joint frames are the world transforms of the joint node and the
// connected node expressed in the respective body node spaces; they are
// captured when the constraint is created (rebuild() re-captures them).
//
// Constraint creation is deferred when a needed rigid body does not exist
// yet (scene load / paste order): Scene_root keeps all attached Node_joints
// registered and retries pending ones from register_node_physics(), and
// tears down constraints referencing a body that is about to leave the world
// from unregister_node_physics().
class Node_joint
    : public erhe::Item<
        erhe::Item_base,
        erhe::scene::Node_attachment,
        Node_joint,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Node_joint();
    Node_joint(
        const std::shared_ptr<erhe::scene::Node>&                     connected_node,
        const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings,
        bool                                                          enable_collision
    );
    Node_joint(const Node_joint& src, erhe::for_clone);

    explicit Node_joint(const Node_joint&);
    Node_joint& operator=(const Node_joint&);
    ~Node_joint() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Node_joint"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::physics; }

    // Implements / overrides Node_attachment
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // Public API
    [[nodiscard]] auto get_connected_node  () const -> std::shared_ptr<erhe::scene::Node>;
    void               set_connected_node  (const std::shared_ptr<erhe::scene::Node>& node);
    [[nodiscard]] auto get_settings        () const -> const std::shared_ptr<erhe::physics::Physics_joint_settings>&;
    void               set_settings        (const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings);
    [[nodiscard]] auto get_enable_collision() const -> bool;
    void               set_enable_collision(bool enable_collision);
    [[nodiscard]] auto get_constraint      () const -> erhe::physics::IConstraint*;

    // Tears down and recreates the constraint, re-capturing the joint frames
    // from the current node transforms. Call after editing the shared
    // settings or moving the joint / connected nodes.
    void rebuild();

    void set_physics_world(erhe::physics::IWorld* value);

    // Called by Scene_root::register_node_physics(): creates the constraint
    // when all needed rigid bodies are available; keeps the joint pending
    // (returns false) otherwise. Returns true when a constraint exists.
    auto try_create_constraint() -> bool;

    // Called by Scene_root::unregister_node_physics() before the rigid body
    // leaves the world: tears down the constraint if it references the body,
    // returning the joint to the pending state.
    void handle_rigid_body_removed(erhe::physics::IRigid_body* rigid_body);

private:
    void destroy_constraint();

    std::weak_ptr<erhe::scene::Node>                       m_connected_node;
    std::shared_ptr<erhe::physics::Physics_joint_settings> m_settings;
    bool                                                   m_enable_collision{false};

    erhe::physics::IWorld*                       m_physics_world{nullptr};
    std::shared_ptr<erhe::physics::IConstraint>  m_constraint;
    // Bodies the live constraint references; valid only while m_constraint
    // exists (used for collision pair restore and teardown matching).
    erhe::physics::IRigid_body*                  m_rigid_body_a{nullptr};
    erhe::physics::IRigid_body*                  m_rigid_body_b{nullptr}; // nullptr = world
    bool                                         m_collision_pair_disabled{false};
};

}
