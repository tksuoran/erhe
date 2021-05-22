#include "erhe/scene/mesh.hpp"

namespace erhe::scene
{

Mesh::Mesh(const std::string& name)
    : m_name{name}
{
}

Mesh::Mesh(const std::string&         name,
           erhe::primitive::Primitive primitive)
    : m_name{name}
{
    primitives.emplace_back(primitive);
}

auto Mesh::name() const -> const std::string&
{
    return m_name;
}

void Mesh::on_attach(Node& node)
{
    m_node = node.shared_from_this();
}

void Mesh::on_detach(Node& node)
{
    m_node.reset();
}

}