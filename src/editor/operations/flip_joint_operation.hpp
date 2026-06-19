#pragma once

#include "operations/operation.hpp"

#include "erhe_scene/node.hpp"

#include <memory>

namespace erhe::scene { class Node; }

namespace editor {

class Node_joint;

// Reorients one rigid-body party of a hinge joint by a precomputed rigid world
// delta (a 180-degree "edge-endpoints-swapped" flip plus the collision-avoidance
// roll), re-pins that body's joint frame node back onto the unmoved hinge frame,
// and rebuilds the constraint so the hinge stays valid in the flipped pose.
//
// The rebuild must run *after* the two transforms are applied, in both the execute
// and the undo direction. A Compound_operation undoes its children in reverse order,
// so it cannot place a single rebuild step after the transforms in both directions;
// hence this dedicated operation, which sequences transforms-then-rebuild itself.
class Flip_joint_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::scene::Node> moved_node;   // selected party body (flipped)
        erhe::scene::Transform             moved_before;
        erhe::scene::Transform             moved_after;
        std::shared_ptr<erhe::scene::Node> frame_node;   // selected party's joint frame node (re-pinned to F)
        erhe::scene::Transform             frame_before;
        erhe::scene::Transform             frame_after;
        std::shared_ptr<Node_joint>        node_joint;
    };

    explicit Flip_joint_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    Parameters m_parameters;
};

}
