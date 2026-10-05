#include "operations/collision_shape_set_operation.hpp"

#include "editor_log.hpp"
#include "scene/node_physics_system.hpp"

#include "erhe_physics/icollision_shape.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace editor {

Collision_shape_set_operation::Collision_shape_set_operation(
    const std::shared_ptr<erhe::scene::Node>&               node,
    const std::shared_ptr<erhe::physics::ICollision_shape>& after
)
    : m_node {node}
    , m_after{after}
{
    ERHE_VERIFY(m_node);
    ERHE_VERIFY(m_after);
    m_before = get_node_authored_collision_shape(*m_node.get());
    set_description(fmt::format("[{}] Collision shape {} -> {} on '{}'", get_serial(), m_before ? m_before->describe() : std::string{"(none)"}, m_after->describe(), m_node->get_name()));
}

void Collision_shape_set_operation::apply(const std::shared_ptr<erhe::physics::ICollision_shape>& shape)
{
    Node_physics_system* const system = find_node_physics_system(*m_node.get());
    if (system == nullptr) {
        // Undo / redo run in history order, so the node is back in its scene
        // whenever this operation is reached again.
        log_operations->error("Op {}: node '{}' is in no scene", describe(), m_node->get_name());
        return;
    }
    system->set_collision_shape(*m_node.get(), shape);
}

void Collision_shape_set_operation::execute(App_context& context)
{
    static_cast<void>(context);
    log_operations->trace("Op Execute {}", describe());
    if (!m_executed) {
        m_executed = true;
        // All or nothing: a node in no scene has no physics system to take
        // the shape, so the first execute changes nothing and is not
        // recorded (a Compound_child_error::roll_back compound undoes its
        // siblings).
        if (find_node_physics_system(*m_node.get()) == nullptr) {
            set_error(fmt::format("node '{}' is in no scene", m_node->get_name()));
            return;
        }
    }
    apply(m_after);
}

void Collision_shape_set_operation::undo(App_context& context)
{
    static_cast<void>(context);
    log_operations->trace("Op Undo {}", describe());
    apply(m_before);
}

void Collision_shape_set_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    out_items.insert(m_node.get());
}

}
