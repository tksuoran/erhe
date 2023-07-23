#pragma once

#include "erhe/scene/node_attachment.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/physics/imotion_state.hpp"

#include <functional>

namespace erhe {
    class Item_host;
}
namespace erhe::physics {
    class IWorld;
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

    // Implements Item
    static constexpr std::string_view static_type_name{"Node_physics"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    [[nodiscard]] auto get_type     () const -> uint64_t         override;
    [[nodiscard]] auto get_type_name() const -> std::string_view override;

    // Implements / overrides Node_attachment
    void handle_item_host_update     (erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;
    void handle_node_transform_update() override;

    // Implements IMotion_state
    [[nodiscard]] auto get_world_from_rigidbody() const -> erhe::physics::Transform   override;
    [[nodiscard]] auto get_motion_mode         () const -> erhe::physics::Motion_mode override;
    void set_world_from_rigidbody(const glm::mat4&                 world_from_rigidbody) override;
    void set_world_from_rigidbody(const erhe::physics::Transform   world_from_rigidbody) override;
    void set_motion_mode         (const erhe::physics::Motion_mode motion_mode         ) override;

    // Public API
    [[nodiscard]] auto get_rigid_body     ()       ->       erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_rigid_body     () const -> const erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_world_from_node() const -> erhe::physics::Transform;

    void set_world_from_node    (const glm::mat4& world_from_node);
    void set_world_from_node    (const erhe::physics::Transform world_from_node);
    void set_rigidbody_from_node(const erhe::physics::Transform rigidbody_from_node);

    void set_physics_world(erhe::physics::IWorld* value);
    [[nodiscard]] auto get_physics_world() const -> erhe::physics::IWorld*;

private:
    erhe::physics::IWorld*                           m_physics_world      {nullptr};
    erhe::physics::Transform                         m_rigidbody_from_node{};
    erhe::physics::Transform                         m_node_from_rigidbody{};
    erhe::physics::Motion_mode                       m_motion_mode;
    std::shared_ptr<erhe::physics::IRigid_body>      m_rigid_body;
    std::shared_ptr<erhe::physics::ICollision_shape> m_collision_shape;
    bool                                             m_transform_change_from_physics{false};
    glm::vec3                                        m_scale{1.0f, 1.0f, 1.0f}; // TODO hack
};

auto is_physics(const erhe::Item* item) -> bool;
auto is_physics(const std::shared_ptr<erhe::Item>& item) -> bool;

auto get_node_physics(const erhe::scene::Node* node) -> std::shared_ptr<Node_physics>;

} // namespace editor
