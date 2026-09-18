#pragma once

#include "operations/operation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

namespace erhe::primitive { class Buffer_info; }

namespace editor {

class PP_context;
class Node_physics;
class Mesh_raytrace;

class Merge_operation : public Operation
{
public:
    class Parameters
    {
    public:
        App_context&                context;
        // The items to merge, in target-first order: the first mesh-carrying
        // node is the survivor. Filled by Operations::resolve_operation_items,
        // which puts the active mesh first (doc/editor/active_item.md D6).
        std::vector<std::shared_ptr<erhe::Item_base>> items;
        erhe::primitive::Build_info build_info;
        std::function<erhe::geometry::Geometry(
            const erhe::geometry::Geometry& lhs,
            const erhe::geometry::Geometry& rhs)
        >                           operation{};
    };

    explicit Merge_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::shared_ptr<erhe::scene::Node> node;
        std::shared_ptr<erhe::scene::Node> before_parent;
        std::shared_ptr<Node_physics>      node_physics;
    };

    Parameters                                                 m_parameters;
    std::vector<Entry>                                         m_sources;
    std::shared_ptr<Node_physics>                              m_combined_node_physics;
    std::vector<erhe::scene::Mesh_primitive>                   m_first_mesh_primitives_before{};
    std::vector<erhe::scene::Mesh_primitive>                   m_first_mesh_primitives_after{};

    std::vector<std::shared_ptr<erhe::Item_base>>              m_selection_before;
    std::vector<std::shared_ptr<erhe::Item_base>>              m_selection_after;
    std::vector<std::shared_ptr<erhe::scene::Node>>            m_hold_nodes;
    std::vector<std::shared_ptr<erhe::scene::Node_attachment>> m_hold_node_attachments;
};

}
