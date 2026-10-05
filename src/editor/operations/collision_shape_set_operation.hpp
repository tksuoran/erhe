#pragma once

#include "operations/operation.hpp"

#include <memory>

namespace erhe::physics { class ICollision_shape; }
namespace erhe::scene   { class Xformable; using Node = Xformable; }

namespace editor {

// Replaces the collision shape a node's rigid body is made from
// (Node_physics_system::set_collision_shape, which recreates a live body).
// The shape is not a property, so a Property_edit_operation cannot record
// it (doc/editor/operations.md "Property_edit_operation"). The before state
// is the node's authored shape when the operation is constructed
// (Node_physics_system::get_authored_collision_shape: without the
// center-of-mass wrapper set_collision_shape adds), so undo restores the
// same body shape whatever the node's center-of-mass offset is then. A
// first execute on a node in no scene is an error (nothing changes, not
// recorded).
class Collision_shape_set_operation : public Operation
{
public:
    Collision_shape_set_operation(
        const std::shared_ptr<erhe::scene::Node>&               node,
        const std::shared_ptr<erhe::physics::ICollision_shape>& after
    );

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

private:
    void apply(const std::shared_ptr<erhe::physics::ICollision_shape>& shape);

    std::shared_ptr<erhe::scene::Node>               m_node;
    std::shared_ptr<erhe::physics::ICollision_shape> m_before;
    std::shared_ptr<erhe::physics::ICollision_shape> m_after;
    bool                                             m_executed{false};
};

}
