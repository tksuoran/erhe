#include "rig/bone_connect.hpp"

#include "operations/node_transform_operation.hpp"
#include "scene/rig_properties.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/trs_transform.hpp"

namespace editor {

namespace {

void append_snap(
    const std::shared_ptr<erhe::scene::Node>& bone,
    const glm::vec3                           head,
    std::vector<std::shared_ptr<Operation>>&  out_operations
)
{
    const erhe::scene::Trs_transform before = bone->parent_from_node_transform();
    if (before.get_translation() == head) {
        return;
    }
    erhe::scene::Trs_transform after = before;
    after.set_translation(head);
    out_operations.push_back(
        std::make_shared<Node_transform_operation>(
            Node_transform_operation::Parameters{
                .node                    = bone,
                .parent_from_node_before = before,
                .parent_from_node_after  = after,
                .xform_op_stack_before   = bone->copy_xform_op_stack()
            }
        )
    );
}

} // anonymous namespace

void append_bone_connect_follow_ups(
    const erhe::Item_base&                     item,
    const erhe::property::Dependency_property& property,
    std::vector<std::shared_ptr<Operation>>&   out_operations
)
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&item);
    if (node == nullptr) {
        return;
    }
    if (&property == &Rig::tail_property().get()) {
        const glm::vec3 tail = node->get_value(Rig::tail_property());
        for (const std::shared_ptr<erhe::Hierarchy>& child : node->get_children()) {
            const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
            if (!child_node || !erhe::scene::is_bone(child_node.get()) || !child_node->get_value(Rig::connected_property())) {
                continue;
            }
            append_snap(child_node, tail, out_operations);
        }
        return;
    }
    if (&property == &Rig::connected_property().get()) {
        if (!erhe::scene::is_bone(node) || !node->get_value(Rig::connected_property())) {
            return;
        }
        const std::shared_ptr<erhe::scene::Node> parent = node->get_parent_node();
        if (!parent || !erhe::scene::is_bone(parent.get())) {
            return;
        }
        const std::shared_ptr<erhe::scene::Node> self = std::dynamic_pointer_cast<erhe::scene::Node>(
            std::const_pointer_cast<erhe::Item_base>(item.shared_from_this())
        );
        if (self) {
            append_snap(self, parent->get_value(Rig::tail_property()), out_operations);
        }
    }
}

} // namespace editor
