#include "erhe/scene/mesh.hpp"

namespace erhe::scene
{

Mesh::Mesh() = default;

Mesh::Mesh(const std::string_view name)
    : m_name{name}
    , m_id{}
{
}

Mesh::Mesh(
    const std::string_view           name,
    const erhe::primitive::Primitive primitive
)
    : m_name{name}
    , m_id{}
{
    primitives.emplace_back(primitive);
}

Mesh::~Mesh() = default;

void Mesh::on_attach(Node& node)
{
    m_node = node.shared_from_this();
}

void Mesh::on_detach()
{
    m_node.reset();
}

auto Mesh::name() const -> const std::string&
{
    return m_name;
}

auto Mesh::node() const -> const std::shared_ptr<Node>&
{
    return m_node;
}

auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool
{
    return lhs.m_id.get_id() < rhs.m_id.get_id();
}


}