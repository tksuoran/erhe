#pragma once

#include "erhe/scene/node.hpp"

#include <functional>

namespace erhe::raytrace
{
    class IGeometry;
}

namespace editor
{

class Node_raytrace
    : public erhe::scene::INode_attachment
{
public:
    explicit Node_raytrace();

	// Implements INode_attachment
    void on_attached_to           (erhe::scene::Node& node) override;
    void on_detached_from         (erhe::scene::Node& node) override;
    void on_node_transform_changed()                        override;
    auto node_attachment_type     () const -> const char*   override;

    auto raytrace_geometry()       ->       erhe::raytrace::IGeometry*;
    auto raytrace_geometry() const -> const erhe::raytrace::IGeometry*;

private:
    erhe::scene::Node*                         m_node{nullptr};
    std::shared_ptr<erhe::raytrace::IGeometry> m_geometry;
};

auto is_raytrace(const erhe::scene::INode_attachment* const attachment) -> bool;
auto is_raytrace(const std::shared_ptr<erhe::scene::INode_attachment>& attachment) -> bool;
auto as_raytrace(erhe::scene::INode_attachment* attachment) -> Node_raytrace*;
auto as_raytrace(const std::shared_ptr<erhe::scene::INode_attachment>& attachment) -> std::shared_ptr<Node_raytrace>;

auto get_raytrace(erhe::scene::Node* node) -> std::shared_ptr<Node_raytrace>;

} // namespace editor
