#include "tools/mesh_component_selection.hpp"

#include "erhe_scene/mesh.hpp"

namespace editor {

auto c_str(const Mesh_component_mode mode) -> const char*
{
    switch (mode) {
        case Mesh_component_mode::object: return "Object";
        case Mesh_component_mode::vertex: return "Vertex";
        case Mesh_component_mode::edge:   return "Edge";
        case Mesh_component_mode::face:   return "Face";
        default:                          return "?";
    }
}

auto Mesh_component_selection::get_mode() const -> Mesh_component_mode
{
    return m_mode;
}

void Mesh_component_selection::set_mode(const Mesh_component_mode mode)
{
    m_mode = mode;
}

auto Mesh_component_selection::get_active_mesh() const -> std::shared_ptr<erhe::scene::Mesh>
{
    return m_active_mesh.lock();
}

auto Mesh_component_selection::get_active_primitive_index() const -> std::size_t
{
    return m_active_primitive_index;
}

auto Mesh_component_selection::set_active_mesh(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const std::size_t                         primitive_index
) -> bool
{
    const std::shared_ptr<erhe::scene::Mesh> current = m_active_mesh.lock();
    if ((current == mesh) && (m_active_primitive_index == primitive_index)) {
        return false;
    }
    m_active_mesh            = mesh;
    m_active_primitive_index = primitive_index;
    clear();
    return true;
}

void Mesh_component_selection::set_active_geometry(const std::shared_ptr<erhe::geometry::Geometry>& geometry)
{
    if (m_active_geometry.lock() != geometry) {
        clear();
        m_active_geometry = geometry;
    }
}

void Mesh_component_selection::clear()
{
    m_vertices.clear();
    m_facets.clear();
    m_edges.clear();
}

auto Mesh_component_selection::is_empty() const -> bool
{
    return m_vertices.empty() && m_facets.empty() && m_edges.empty();
}

void Mesh_component_selection::add_vertex(const GEO::index_t vertex)
{
    m_vertices.insert(vertex);
}

void Mesh_component_selection::remove_vertex(const GEO::index_t vertex)
{
    m_vertices.erase(vertex);
}

void Mesh_component_selection::toggle_vertex(const GEO::index_t vertex)
{
    if (!m_vertices.erase(vertex)) {
        m_vertices.insert(vertex);
    }
}

auto Mesh_component_selection::contains_vertex(const GEO::index_t vertex) const -> bool
{
    return m_vertices.find(vertex) != m_vertices.end();
}

auto Mesh_component_selection::get_vertices() const -> const std::set<GEO::index_t>&
{
    return m_vertices;
}

void Mesh_component_selection::add_facet(const GEO::index_t facet)
{
    m_facets.insert(facet);
}

void Mesh_component_selection::remove_facet(const GEO::index_t facet)
{
    m_facets.erase(facet);
}

void Mesh_component_selection::toggle_facet(const GEO::index_t facet)
{
    if (!m_facets.erase(facet)) {
        m_facets.insert(facet);
    }
}

auto Mesh_component_selection::contains_facet(const GEO::index_t facet) const -> bool
{
    return m_facets.find(facet) != m_facets.end();
}

auto Mesh_component_selection::get_facets() const -> const std::set<GEO::index_t>&
{
    return m_facets;
}

auto Mesh_component_selection::make_edge_key(const GEO::index_t a, const GEO::index_t b) -> Mesh_edge_key
{
    return (a <= b) ? Mesh_edge_key{a, b} : Mesh_edge_key{b, a};
}

void Mesh_component_selection::add_edge(const GEO::index_t a, const GEO::index_t b)
{
    m_edges.insert(make_edge_key(a, b));
}

void Mesh_component_selection::remove_edge(const GEO::index_t a, const GEO::index_t b)
{
    m_edges.erase(make_edge_key(a, b));
}

void Mesh_component_selection::toggle_edge(const GEO::index_t a, const GEO::index_t b)
{
    const Mesh_edge_key key = make_edge_key(a, b);
    if (!m_edges.erase(key)) {
        m_edges.insert(key);
    }
}

auto Mesh_component_selection::contains_edge(const GEO::index_t a, const GEO::index_t b) const -> bool
{
    return m_edges.find(make_edge_key(a, b)) != m_edges.end();
}

auto Mesh_component_selection::get_edges() const -> const std::set<Mesh_edge_key>&
{
    return m_edges;
}

} // namespace editor
