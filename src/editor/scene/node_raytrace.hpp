#pragma once

#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/scene/node.hpp"

#include <functional>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::raytrace
{
    class IBuffer;
    class IGeometry;
    class IInstance;
    class IScene;
}

namespace editor
{

class Raytrace_primitive
{
public:
    explicit Raytrace_primitive(const std::shared_ptr<erhe::geometry::Geometry>& geometry);

    //std::string                               name;
    std::shared_ptr<erhe::raytrace::IBuffer>  vertex_buffer;
    std::shared_ptr<erhe::raytrace::IBuffer>  index_buffer;
    std::shared_ptr<erhe::geometry::Geometry> geometry;
    erhe::primitive::Primitive_geometry       primitive_geometry;
};

class Node_raytrace
    : public erhe::scene::INode_attachment
{
public:
    explicit Node_raytrace(const std::shared_ptr<Raytrace_primitive>& primitive);
    ~Node_raytrace() override;

	// Implements INode_attachment
    [[nodiscard]] auto node_attachment_type() const -> const char* override;
    void on_node_transform_changed() override;

    // Public API
    [[nodiscard]] auto raytrace_primitive() -> Raytrace_primitive*;
    [[nodiscard]] auto raytrace_geometry()       ->       erhe::raytrace::IGeometry*;
    [[nodiscard]] auto raytrace_geometry() const -> const erhe::raytrace::IGeometry*;
    [[nodiscard]] auto raytrace_instance()       ->       erhe::raytrace::IInstance*;
    [[nodiscard]] auto raytrace_instance() const -> const erhe::raytrace::IInstance*;

private:
    std::shared_ptr<Raytrace_primitive>        m_primitive;
    std::unique_ptr<erhe::raytrace::IGeometry> m_geometry;
    std::unique_ptr<erhe::raytrace::IScene>    m_scene;
    std::unique_ptr<erhe::raytrace::IInstance> m_instance;
};

auto is_raytrace(const erhe::scene::INode_attachment* const attachment) -> bool;
auto is_raytrace(const std::shared_ptr<erhe::scene::INode_attachment>& attachment) -> bool;
auto as_raytrace(erhe::scene::INode_attachment* attachment) -> Node_raytrace*;
auto as_raytrace(const std::shared_ptr<erhe::scene::INode_attachment>& attachment) -> std::shared_ptr<Node_raytrace>;

auto get_raytrace(erhe::scene::Node* node) -> std::shared_ptr<Node_raytrace>;

} // namespace editor
