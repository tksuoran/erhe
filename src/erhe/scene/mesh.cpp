#include "erhe/scene/mesh.hpp"

#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/geometry/geometry.hpp"

namespace erhe::scene
{


Mesh::Mesh()
    : Node{{}}
{
    m_flag_bits |= (Node::c_flag_bit_is_mesh | Node::c_flag_bit_is_transform);
}

Mesh::Mesh(const std::string_view name)
    : Node{name}
{
    m_flag_bits |= (Node::c_flag_bit_is_mesh | Node::c_flag_bit_is_transform);
}

Mesh::Mesh(
    const std::string_view           name,
    const erhe::primitive::Primitive primitive
)
    : Node{name}
{
    m_flag_bits |= (Node::c_flag_bit_is_mesh | Node::c_flag_bit_is_transform);
    data.primitives.emplace_back(primitive);
}

Mesh::~Mesh() = default;

auto Mesh::node_type() const -> const char*
{
    return "Mesh";
}

auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool
{
    return lhs.m_id.get_id() < rhs.m_id.get_id();
}

auto is_mesh(const Node* const node) -> bool
{
    if (node == nullptr)
    {
        return false;
    }
    return (node->flag_bits() & Node::c_flag_bit_is_mesh) == Node::c_flag_bit_is_mesh;
}

auto is_mesh(const std::shared_ptr<Node>& node) -> bool
{
    return is_mesh(node.get());
}

auto as_mesh(Node* const node) -> Mesh*
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if ((node->flag_bits() & Node::c_flag_bit_is_mesh) == 0)
    {
        return nullptr;
    }
    return reinterpret_cast<Mesh*>(node);
}

auto as_mesh(const std::shared_ptr<Node>& node) -> std::shared_ptr<Mesh>
{
    if (!node)
    {
        return {};
    }
    if ((node->flag_bits() & Node::c_flag_bit_is_mesh) == 0)
    {
        return {};
    }
    return std::dynamic_pointer_cast<Mesh>(node);
}


}