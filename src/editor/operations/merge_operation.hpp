#pragma once

#include "operations/ioperation.hpp"

#include "erhe/primitive/build_info.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"

namespace erhe::primitive {
    class Buffer_info;
}

namespace editor
{

class Editor_context;
class Node_physics;
class Node_raytrace;

class Merge_operation
    : public IOperation
{
public:
    class Parameters
    {
    public:
        Editor_context&             context;
        erhe::primitive::Build_info build_info;
    };

    explicit Merge_operation(Parameters&& parameters);

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(Editor_context& context) override;
    void undo   (Editor_context& context) override;

private:
    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::shared_ptr<erhe::scene::Node> node;
        std::shared_ptr<erhe::scene::Node> before_parent;
        std::shared_ptr<Node_physics>      node_physics;
        std::shared_ptr<Node_raytrace>     node_raytrace;
    };

    Parameters                                                 m_parameters;
    std::vector<Entry>                                         m_sources;
    std::shared_ptr<Node_physics>                              m_combined_node_physics;
    std::shared_ptr<Node_raytrace>                             m_combined_node_raytrace;
    std::vector<erhe::primitive::Primitive>                    m_first_mesh_primitives_before{};
    std::vector<erhe::primitive::Primitive>                    m_first_mesh_primitives_after{};

    std::vector<std::shared_ptr<erhe::Item>>                   m_selection_before;
    std::vector<std::shared_ptr<erhe::Item>>                   m_selection_after;
    std::vector<std::shared_ptr<erhe::scene::Node>>            m_hold_nodes;
    std::vector<std::shared_ptr<erhe::scene::Node_attachment>> m_hold_node_attachments;
};

}
