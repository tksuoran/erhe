#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/physics/imotion_state.hpp"

#include <functional>

namespace erhe::physics
{
    class IWorld;
}

namespace erhe::scene
{
    class Scene_host;
}

namespace editor
{

class Node_physics
    : public erhe::scene::Node_attachment
    , public erhe::physics::IMotion_state
{
public:
    explicit Node_physics(
        const erhe::physics::IRigid_body_create_info& create_info
    );
    ~Node_physics() noexcept override;

    // Implements Scene_item
    [[nodiscard]] auto type_name() const -> const char* override;

    // Implements Node_attachment
    void handle_node_scene_host_update(
        erhe::scene::Scene_host* old_scene_host,
        erhe::scene::Scene_host* new_scene_host
    ) override;
    void handle_node_transform_update() override;

    // Implements IMotion_state
    [[nodiscard]] auto get_world_from_rigidbody() const -> erhe::physics::Transform   override;
    [[nodiscard]] auto get_motion_mode         () const -> erhe::physics::Motion_mode override;
    void set_world_from_rigidbody(const glm::mat4&                 world_from_rigidbody) override;
    void set_world_from_rigidbody(const erhe::physics::Transform   world_from_rigidbody) override;
    void set_motion_mode         (const erhe::physics::Motion_mode motion_mode         ) override;

    // Public API
    [[nodiscard]] auto rigid_body         ()       ->       erhe::physics::IRigid_body*;
    [[nodiscard]] auto rigid_body         () const -> const erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_world_from_node() const -> erhe::physics::Transform;

    void set_world_from_node    (const glm::mat4& world_from_node);
    void set_world_from_node    (const erhe::physics::Transform world_from_node);
    void set_rigidbody_from_node(const erhe::physics::Transform rigidbody_from_node);

private:
    erhe::physics::IWorld*                           m_physics_world      {nullptr};
    erhe::physics::Transform                         m_rigidbody_from_node{};
    erhe::physics::Transform                         m_node_from_rigidbody{};
    erhe::physics::Motion_mode                       m_motion_mode;
    std::shared_ptr<erhe::physics::IRigid_body>      m_rigid_body;
    std::shared_ptr<erhe::physics::ICollision_shape> m_collision_shape;
    bool                                             m_transform_change_from_physics{false};
};

auto is_physics(const erhe::scene::Scene_item* const scene_item) -> bool;
auto is_physics(const std::shared_ptr<erhe::scene::Scene_item>& scene_item) -> bool;
auto as_physics(erhe::scene::Scene_item* scene_item) -> Node_physics*;
auto as_physics(const std::shared_ptr<erhe::scene::Scene_item>& scene_item) -> std::shared_ptr<Node_physics>;

auto get_node_physics(const erhe::scene::Node* node) -> std::shared_ptr<Node_physics>;

} // namespace editor
