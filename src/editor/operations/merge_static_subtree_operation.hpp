#pragma once

#include "operations/operation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"

namespace editor {

class App_context;

// Bakes the static geometry hanging under a node into that node's own mesh,
// so a rigid segment of a physics-driven assembly (a sway spine and its
// dozens of visual-only part instances) becomes ONE node instead of a deep
// chain. This is the runtime-cost fix for the per-frame world-transform
// propagation fan-out: every node under a moving body recomputes its world
// matrix each frame, so fewer nodes = less per-frame work (and fewer draws).
//
// Per target root, descendants are classified:
// - MERGED: nodes whose only attachment is a Mesh, not no_transform_update
//   and without a rigid body. Their primitives are baked into the root's
//   frame, grouped by material (one combined primitive per material), and
//   the nodes are removed.
// - BOUNDARY: nodes with a rigid body or the no_transform_update flag
//   (nested sway spines, joint carrier sensors). Not merged, not descended
//   into; with recurse enabled each boundary becomes its own target, so one
//   call flattens a whole tree rig segment by segment.
// - PRUNED: attachment-less nodes that HAD children and whose whole subtree
//   was merged/pruned (part pose nodes, chain groups) - removed with the
//   geometry they carried. Attachment-less LEAF nodes are never pruned:
//   zero-child markers (joint pivot anchors) may be referenced from outside
//   the hierarchy.
// - KEPT: everything else (joint anchors, attachment-carrying nodes). Not
//   merged; descended into.
// Kept and boundary nodes whose parent was merged away are reparented to the
// target root preserving their world transform.
//
// The setup cost (geometry merge + renderable/raytrace build per material
// group) is accepted; instances lose their per-part pickability and their
// geometry sharing (each merged primitive is unique data).
class Merge_static_subtree_operation : public Operation
{
public:
    class Parameters
    {
    public:
        App_context&                       context;
        erhe::primitive::Build_info        build_info;
        std::shared_ptr<erhe::scene::Node> root;
        bool                               recurse{true};
    };

    explicit Merge_static_subtree_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

    [[nodiscard]] auto get_target_count() const -> std::size_t { return m_targets.size(); }
    [[nodiscard]] auto get_merged_count() const -> std::size_t;

private:
    class Reparented_entry
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        std::shared_ptr<erhe::scene::Node> before_parent;
        erhe::scene::Trs_transform         before_parent_from_node;
    };

    class Target
    {
    public:
        std::shared_ptr<erhe::scene::Node>                          root;
        std::shared_ptr<erhe::scene::Mesh>                          mesh;              // root's mesh (possibly created)
        bool                                                        mesh_created{false};
        // Removed (merged + pruned) nodes in depth-first pre-order (parents
        // before children), with their pre-merge parents for undo.
        std::vector<std::shared_ptr<erhe::scene::Node>>             removed_nodes;
        std::vector<std::shared_ptr<erhe::scene::Node>>             removed_before_parents;
        // Kept/boundary nodes whose parent is merged away: reparented to root.
        std::vector<Reparented_entry>                               reparented;
        std::vector<erhe::scene::Mesh_primitive>                    primitives_before;
        std::vector<erhe::scene::Mesh_primitive>                    primitives_after;
    };

    void build_target(const std::shared_ptr<erhe::scene::Node>& root, std::vector<std::shared_ptr<erhe::scene::Node>>& out_boundaries);

    Parameters          m_parameters;
    std::vector<Target> m_targets;
};

}
