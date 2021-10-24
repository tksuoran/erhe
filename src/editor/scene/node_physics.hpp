#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/physics/imotion_state.hpp"

#include <functional>

namespace editor
{

class Node_physics
    : public erhe::scene::INode_attachment
    , public erhe::physics::IMotion_state

{
public:
    explicit Node_physics(erhe::physics::IRigid_body_create_info& create_info);
    ~Node_physics() override;

	// Implements INode_attachment
    auto name     () const -> const std::string& override;
    void on_attach(erhe::scene::Node& node)      override;
    void on_detach(erhe::scene::Node& node)      override;

    // Implements IMotion_state
    void get_world_transform(glm::mat3& basis, glm::vec3& origin)           override;
    void set_world_transform(const glm::mat3 basis, const glm::vec3 origin) override;

    void on_node_updated();

    auto rigid_body() -> erhe::physics::IRigid_body*;
    auto rigid_body() const -> const erhe::physics::IRigid_body*;

private:
    std::shared_ptr<erhe::scene::Node>               m_node;
    std::shared_ptr<erhe::physics::IRigid_body>      m_rigid_body;
    std::shared_ptr<erhe::physics::ICollision_shape> m_collision_shape;
};

} // namespace editor
