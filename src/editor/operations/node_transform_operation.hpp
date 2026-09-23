#pragma once

#include "operations/operation.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/xform_op.hpp"

#include <memory>
#include <optional>

namespace erhe        { class Hierarchy; }
namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

// What the first execute() does with the node.
enum class Node_transform_first_execute : unsigned int {
    // Writes parent_from_node_after and snaps the node's rigid body to it at
    // rest (a discrete move).
    apply       = 0,
    // The node already sits at parent_from_node_after, placed there by the
    // physics simulation during a drag that pulled its dynamic body: record
    // the xformOp stack only, leaving the node and its moving body alone so
    // the body keeps the velocity it was released with. Undo and redo snap
    // as usual.
    record_only = 1
};

class Node_transform_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        // TRS components, not a matrix: re-decomposing a matrix picks one of
        // the two quaternions of a rotation, and a Trs_transform keeps the one
        // the edit wrote.
        erhe::scene::Trs_transform         parent_from_node_before;
        erhe::scene::Trs_transform         parent_from_node_after;
        // The node's authored xformOp stack as it was next to
        // parent_from_node_before (no value when the node has no stack).
        // Undo restores it verbatim, so a stack the edit collapsed comes
        // back whole.
        std::optional<erhe::scene::Xform_op_stack> xform_op_stack_before;
        float                              time_duration{0.0f};
        Node_transform_first_execute       first_execute{Node_transform_first_execute::apply};
        // record_only: the node's stack next to parent_from_node_after, taken
        // when the drag ended (no value when the node has no stack).
        std::optional<erhe::scene::Xform_op_stack> xform_op_stack_after;
    };

    explicit Node_transform_operation(const Parameters& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    Parameters m_parameters;
    // The stack the first execute() produced, recorded so that redo restores
    // it instead of running the write-back a second time.
    std::optional<erhe::scene::Xform_op_stack> m_xform_op_stack_after;
    bool                                       m_xform_op_stack_after_recorded{false};
};

}
