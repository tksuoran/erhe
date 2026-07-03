#include "geometry_graph/graph_mesh.hpp"
#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/node.hpp"

namespace editor {

Graph_mesh::Graph_mesh()
    : Item{"Graph Mesh"}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui | erhe::Item_flags::content);
}

Graph_mesh::Graph_mesh(const std::string_view name)
    : Item{name}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui | erhe::Item_flags::content);
}

Graph_mesh::~Graph_mesh() noexcept = default;

auto Graph_mesh::graph() -> Geometry_graph&
{
    return m_graph;
}

auto Graph_mesh::graph() const -> const Geometry_graph&
{
    return m_graph;
}

auto Graph_mesh::nodes() -> std::vector<std::shared_ptr<Geometry_graph_node>>&
{
    return m_nodes;
}

auto Graph_mesh::nodes() const -> const std::vector<std::shared_ptr<Geometry_graph_node>>&
{
    return m_nodes;
}

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

} // namespace editor
