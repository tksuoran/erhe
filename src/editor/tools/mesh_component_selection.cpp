#include "tools/mesh_component_selection.hpp"

#include "app_message_bus.hpp"

#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <vector>

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

auto make_edge_key(const GEO::index_t a, const GEO::index_t b) -> Mesh_edge_key
{
    return (a <= b) ? Mesh_edge_key{a, b} : Mesh_edge_key{b, a};
}

#pragma region Mesh_component_entry
auto Mesh_component_entry::is_empty() const -> bool
{
    return vertices.empty() && facets.empty() && edges.empty();
}

void Mesh_component_entry::clear()
{
    vertices.clear();
    facets.clear();
    edges.clear();
}

void Mesh_component_entry::add_vertex(const GEO::index_t vertex)
{
    vertices.insert(vertex);
}

void Mesh_component_entry::toggle_vertex(const GEO::index_t vertex)
{
    if (!vertices.erase(vertex)) {
        vertices.insert(vertex);
    }
}

void Mesh_component_entry::add_facet(const GEO::index_t facet)
{
    facets.insert(facet);
}

void Mesh_component_entry::toggle_facet(const GEO::index_t facet)
{
    if (!facets.erase(facet)) {
        facets.insert(facet);
    }
}

void Mesh_component_entry::add_edge(const GEO::index_t a, const GEO::index_t b)
{
    edges.insert(make_edge_key(a, b));
}

void Mesh_component_entry::toggle_edge(const GEO::index_t a, const GEO::index_t b)
{
    const Mesh_edge_key key = make_edge_key(a, b);
    if (!edges.erase(key)) {
        edges.insert(key);
    }
}
#pragma endregion Mesh_component_entry

Mesh_component_selection::Mesh_component_selection(App_message_bus& app_message_bus)
{
    // Kept as an eager-housekeeping safety net: when an operation announces a
    // geometry swap, prune entries that can no longer become live. Correctness
    // comes from the content-addressed is_live() check, not from this message.
    m_mesh_geometry_changed_subscription = app_message_bus.mesh_geometry_changed.subscribe(
        [this](Mesh_geometry_changed_message& message) {
            on_mesh_geometry_changed(message);
        }
    );
}

void Mesh_component_selection::on_mesh_geometry_changed(Mesh_geometry_changed_message&)
{
    // Drop entries that can never become live again (mesh or geometry freed) or
    // that hold nothing. Dormant-but-alive entries (the swapped-away Geometry is
    // not currently bound but is still held by an undo operation) are retained so
    // undo can make them live again.
    prune();
}

auto Mesh_component_selection::get_mode() const -> Mesh_component_mode
{
    return m_mode;
}

void Mesh_component_selection::set_mode(const Mesh_component_mode mode)
{
    m_mode = mode;
}

auto Mesh_component_selection::find_entry(
    const std::shared_ptr<erhe::scene::Mesh>&        mesh,
    const std::size_t                                primitive_index,
    const std::shared_ptr<erhe::geometry::Geometry>& geometry
) -> Mesh_component_entry*
{
    for (Mesh_component_entry& entry : m_entries) {
        if (
            (entry.mesh.lock()     == mesh)            &&
            (entry.primitive_index == primitive_index) &&
            (entry.geometry.lock() == geometry)
        ) {
            return &entry;
        }
    }
    return nullptr;
}

auto Mesh_component_selection::find_or_create_entry(
    const std::shared_ptr<erhe::scene::Mesh>&        mesh,
    const std::size_t                                primitive_index,
    const std::shared_ptr<erhe::geometry::Geometry>& geometry
) -> Mesh_component_entry&
{
    Mesh_component_entry* const existing = find_entry(mesh, primitive_index, geometry);
    if (existing != nullptr) {
        return *existing;
    }
    Mesh_component_entry& entry = m_entries.emplace_back();
    entry.mesh            = mesh;
    entry.primitive_index = primitive_index;
    entry.geometry        = geometry;
    return entry;
}

void Mesh_component_selection::clear_all()
{
    m_entries.clear();
}

void Mesh_component_selection::set_after_operation(
    const std::shared_ptr<erhe::scene::Mesh>&        mesh,
    const std::size_t                                primitive_index,
    const std::shared_ptr<erhe::geometry::Geometry>& after_geometry,
    const std::set<GEO::index_t>&                    vertices,
    const std::set<GEO::index_t>&                    facets,
    const std::set<Mesh_edge_key>&                   edges
)
{
    if (vertices.empty() && facets.empty() && edges.empty()) {
        return;
    }
    Mesh_component_entry& entry = find_or_create_entry(mesh, primitive_index, after_geometry);
    entry.vertices = vertices;
    entry.facets   = facets;
    entry.edges    = edges;
}

auto Mesh_component_selection::is_empty() const -> bool
{
    for (const Mesh_component_entry& entry : m_entries) {
        if (!entry.is_empty()) {
            return false;
        }
    }
    return true;
}

void Mesh_component_selection::prune()
{
    std::erase_if(
        m_entries,
        [](const Mesh_component_entry& entry) {
            return entry.mesh.expired() || entry.geometry.expired() || entry.is_empty();
        }
    );
}

auto Mesh_component_selection::is_live(const Mesh_component_entry& entry) const -> bool
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = entry.mesh.lock();
    if (!mesh) {
        return false;
    }
    const erhe::scene::Node* node = mesh->get_node();
    if ((node == nullptr) || (node->get_item_host() == nullptr)) {
        return false; // mesh removed from the scene (e.g. an undone insert)
    }
    const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
    if (!geometry) {
        return false;
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if (entry.primitive_index >= primitives.size()) {
        return false;
    }
    const std::shared_ptr<erhe::primitive::Primitive>& primitive = primitives[entry.primitive_index].primitive;
    if (!primitive) {
        return false;
    }
    // Two-geometry primitives (a separate collision Primitive_shape) are not
    // valid component-selection targets: the picked/stored geometry is the
    // collision geometry, which differs from the render geometry, so neither
    // rendering the highlight nor editing the vertices would match the visible
    // mesh. Both tool_render and Mesh_component_transform::gather rely on this.
    if (primitive->collision_shape) {
        return false;
    }
    const std::shared_ptr<erhe::primitive::Primitive_shape> shape = primitive->get_shape_for_raytrace();
    if (!shape) {
        return false;
    }
    // Live only if the primitive still carries the exact Geometry these indices
    // address; a swap installs a different object and the entry goes dormant.
    return shape->get_geometry() == geometry;
}

auto Mesh_component_selection::get_entries() -> std::vector<Mesh_component_entry>&
{
    return m_entries;
}

auto Mesh_component_selection::get_entries() const -> const std::vector<Mesh_component_entry>&
{
    return m_entries;
}

} // namespace editor
