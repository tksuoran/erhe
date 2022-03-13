#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/physics/imotion_state.hpp"

#include <functional>

namespace erhe::physics
{
    class IWorld;
}

namespace editor
{

class Node_physics
    : public erhe::scene::INode_attachment
    , public erhe::physics::IMotion_state
    , public std::enable_shared_from_this<Node_physics>
{
public:
    explicit Node_physics(
        const erhe::physics::IRigid_body_create_info& create_info
    );
    ~Node_physics() noexcept override;

	// Implements INode_attachment
    [[nodiscard]] auto node_attachment_type() const -> const char* override;
    void on_node_transform_changed() override;

    // Implements IMotion_state
    [[nodiscard]] auto get_world_from_rigidbody() const -> erhe::physics::Transform override;
    void set_world_from_rigidbody(const erhe::physics::Transform world_from_rigidbody) override;

    // Public API
    [[nodiscard]] auto rigid_body         ()       ->       erhe::physics::IRigid_body*;
    [[nodiscard]] auto rigid_body         () const -> const erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_world_from_node() const -> erhe::physics::Transform;

    using erhe::scene::INode_attachment::on_attached_to;
    void on_attached_to          (erhe::physics::IWorld* world);

    using erhe::scene::INode_attachment::on_detached_from;
    void on_detached_from        (erhe::physics::IWorld* world);

    void set_world_from_node     (const erhe::physics::Transform world_from_node);
    void set_rigidbody_from_node (const erhe::physics::Transform rigidbody_from_node);

private:
    erhe::physics::IWorld*                           m_physics_world      {nullptr};
    erhe::physics::Transform                         m_rigidbody_from_node{};
    erhe::physics::Transform                         m_node_from_rigidbody{};
    std::shared_ptr<erhe::physics::IRigid_body>      m_rigid_body;
    std::shared_ptr<erhe::physics::ICollision_shape> m_collision_shape;
    bool                                             m_transform_change_from_physics{false};
};

auto is_physics(const erhe::scene::INode_attachment* const attachment) -> bool;
auto is_physics(const std::shared_ptr<erhe::scene::INode_attachment>& attachment) -> bool;
auto as_physics(erhe::scene::INode_attachment* attachment) -> Node_physics*;
auto as_physics(const std::shared_ptr<erhe::scene::INode_attachment>& attachment) -> std::shared_ptr<Node_physics>;

auto get_physics_node(erhe::scene::Node* node) -> std::shared_ptr<Node_physics>;

} // namespace editor
