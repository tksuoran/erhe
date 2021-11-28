#include "scene/node_raytrace.hpp"

#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/tracy_client.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor
{

using namespace erhe::scene;
using namespace erhe::raytrace;
using namespace std;

Node_raytrace::Node_raytrace()
{
    m_flag_bits |= INode_attachment::c_flag_bit_is_raytrace;
}

auto Node_raytrace::node_attachment_type() const -> const char*
{
    return "Node_raytrace";
}

void Node_raytrace::on_attached_to(Node& node)
{
    ZoneScoped;

    m_node = &node;
    on_node_transform_changed();
}

void Node_raytrace::on_detached_from(Node& node)
{
    static_cast<void>(node);
    m_node = nullptr;
}

void Node_raytrace::on_node_transform_changed()
{
    VERIFY(m_node);
    node()->update_transform();
    VERIFY(m_geometry);
    const glm::mat4 m = m_node->world_from_node();
    //m_geometry->set_world_transform(world_from_node);
}

auto Node_raytrace::raytrace_geometry() -> IGeometry*
{
    return m_geometry.get();
}

auto Node_raytrace::raytrace_geometry() const -> const IGeometry*
{
    return m_geometry.get();
}

auto is_raytrace(const INode_attachment* const attachment) -> bool
{
    if (attachment == nullptr)
    {
        return false;
    }
    return (attachment->flag_bits() & INode_attachment::c_flag_bit_is_raytrace) == INode_attachment::c_flag_bit_is_raytrace;
}

auto is_raytrace(const std::shared_ptr<INode_attachment>& attachment) -> bool
{
    return is_raytrace(attachment.get());
}

auto as_raytrace(INode_attachment* attachment) -> Node_raytrace*
{
    if (attachment == nullptr)
    {
        return nullptr;
    }
    if ((attachment->flag_bits() & INode_attachment::c_flag_bit_is_raytrace) == 0)
    {
        return nullptr;
    }
    return reinterpret_cast<Node_raytrace*>(attachment);
}

auto as_raytrace(const std::shared_ptr<INode_attachment>& attachment) -> std::shared_ptr<Node_raytrace>
{
    if (!attachment)
    {
        return {};
    }
    if ((attachment->flag_bits() & INode_attachment::c_flag_bit_is_physics) == 0)
    {
        return {};
    }
    return std::dynamic_pointer_cast<Node_raytrace>(attachment);
}

auto get_raytrace(Node* node) -> std::shared_ptr<Node_raytrace>
{
    for (const auto& attachment : node->attachments())
    {
        auto node_physics = as_raytrace(attachment);
        if (node_physics)
        {
            return node_physics;
        }
    }
    return {};
}

}