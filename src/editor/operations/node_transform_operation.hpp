#pragma once

#include "operations/operation.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/xform_op.hpp"

#include <memory>
#include <optional>

namespace erhe        { class Hierarchy; }
namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class Node_transform_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        erhe::scene::Transform             parent_from_node_before;
        erhe::scene::Transform             parent_from_node_after;
        // The node's authored xformOp stack as it was next to
        // parent_from_node_before (no value when the node has no stack).
        // Undo restores it verbatim, so a stack the edit collapsed comes
        // back whole.
        std::optional<erhe::scene::Xform_op_stack> xform_op_stack_before;
        float                              time_duration{0.0f};
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
