#pragma once

#include "operations/ioperation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

namespace erhe::primitive {
    class Buffer_info;
}

namespace editor {

class Editor_context;
class Node_physics;
class Mesh_raytrace;

class Merge_operation : public Operation
{
public:
    class Parameters
    {
    public:
        Editor_context&             context;
        erhe::primitive::Build_info build_info;
        std::function<erhe::geometry::Geometry(
            const erhe::geometry::Geometry& lhs,
            const erhe::geometry::Geometry& rhs)
        >                           operation{};
    };

    explicit Merge_operation(Parameters&& parameters);

    // Implements Operation
    auto describe() const -> std::string   override;
    void execute (Editor_context& context) override;
    void undo    (Editor_context& context) override;

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
    std::vector<erhe::primitive::Primitive>                    m_first_mesh_primitives_before{};
    std::vector<erhe::primitive::Primitive>                    m_first_mesh_primitives_after{};

    std::vector<std::shared_ptr<erhe::Item_base>>              m_selection_before;
    std::vector<std::shared_ptr<erhe::Item_base>>              m_selection_after;
    std::vector<std::shared_ptr<erhe::scene::Node>>            m_hold_nodes;
    std::vector<std::shared_ptr<erhe::scene::Node_attachment>> m_hold_node_attachments;
};

}
