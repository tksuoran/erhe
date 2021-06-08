#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/physics/rigid_body.hpp"
#include "LinearMath\btMotionState.h"
#include "BulletCollision/CollisionShapes/btCollisionShape.h"
#include <functional>

namespace editor
{

class Node_physics
    : public erhe::scene::INode_attachment
    , public btMotionState

{
public:
    explicit Node_physics(erhe::physics::Rigid_body::Create_info& create_info);
    ~Node_physics() override;

	// Implements INode_attachment
    auto name     () const -> const std::string& override;
    void on_attach(erhe::scene::Node& node)      override;
    void on_detach(erhe::scene::Node& node)      override;

    // Implements btMotionState
    void getWorldTransform(btTransform& worldTrans) const override;
    void setWorldTransform(const btTransform& worldTrans) override;

    void on_node_updated();

    btTransform get_node_transform() const;

private:
    std::shared_ptr<erhe::scene::Node> m_node;

public:
    erhe::physics::Rigid_body         rigid_body;
    std::shared_ptr<btCollisionShape> collision_shape;
};

} // namespace editor
