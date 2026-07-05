#include "geometry_graph/graph_mesh.hpp"
#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/node.hpp"

namespace editor {

Graph_mesh::Graph_mesh()
    : Graph_asset{"Graph Mesh"}
{
}

Graph_mesh::Graph_mesh(const std::string_view name)
    : Graph_asset{name}
{
}

Graph_mesh::~Graph_mesh() noexcept = default;

void Graph_mesh::set_baked_products(const Graph_mesh_baked_products& products)
{
    m_baked_products = products;
    ++m_baked_revision;
}

auto Graph_mesh::get_baked_products() const -> const Graph_mesh_baked_products&
{
    return m_baked_products;
}

auto Graph_mesh::get_baked_revision() const -> uint64_t
{
    return m_baked_revision;
}

void Graph_mesh::request_attachment_push()
{
    m_attachment_push_requested = true;
}

auto Graph_mesh::consume_attachment_push_request() -> bool
{
    const bool requested = m_attachment_push_requested;
    m_attachment_push_requested = false;
    return requested;
}

} // namespace editor
