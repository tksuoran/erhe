#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/physics/imotion_state.hpp"

#include <functional>

namespace editor
{

class Node_physics
    : public erhe::scene::Node
    , public erhe::physics::IMotion_state

{
public:
    explicit Node_physics(erhe::physics::IRigid_body_create_info& create_info);
    ~Node_physics() override;

    auto node_type() const -> const char* override;

	// Implements Node
    void on_attached_to      (erhe::scene::Node& node) override;
    void on_transform_changed()                        override;

    // Implements IMotion_state

    // Physics backend asks what is the current motion state
    void get_world_transform(glm::mat3& basis, glm::vec3& origin)           override;

    // Physics backend has moved the object
    void set_world_transform(const glm::mat3 basis, const glm::vec3 origin) override;

    void on_node_updated();

    auto rigid_body()       ->       erhe::physics::IRigid_body*;
    auto rigid_body() const -> const erhe::physics::IRigid_body*;

private:
    std::shared_ptr<erhe::physics::IRigid_body>      m_rigid_body;
    std::shared_ptr<erhe::physics::ICollision_shape> m_collision_shape;
    bool                                             m_transform_change_from_physics{false};
};

auto is_physics(const erhe::scene::Node* const node) -> bool;
auto is_physics(const std::shared_ptr<erhe::scene::Node>& node) -> bool;
auto as_physics(erhe::scene::Node* node) -> Node_physics*;
auto as_physics(const std::shared_ptr<erhe::scene::Node>& node) -> std::shared_ptr<Node_physics>;

auto get_physics_node(erhe::scene::Node* node) -> std::shared_ptr<Node_physics>;

} // namespace editor
