#pragma once

#include <geogram/basic/numeric.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <set>
#include <utility>

namespace erhe::geometry { class Geometry; }
namespace erhe::scene    { class Mesh; }

namespace editor {

// Selection granularity for the Mesh_component_selection_tool.
//   object - whole mesh / node selection (the existing Selection handles it)
//   vertex - mesh vertices (points)
//   edge   - mesh edges (vertex pairs)
//   face   - mesh facets (polygons)
enum class Mesh_component_mode {
    object = 0,
    vertex = 1,
    edge   = 2,
    face   = 3
};

[[nodiscard]] auto c_str(Mesh_component_mode mode) -> const char*;

// Canonical undirected edge key: (min vertex, max vertex), so the same edge
// reached from either adjacent facet maps to a single entry.
using Mesh_edge_key = std::pair<GEO::index_t, GEO::index_t>;

// Holds the current mesh sub-component selection. Component selection applies
// to a single active mesh + primitive at a time (initial scope); switching to
// a different mesh clears the selected components. This is a standalone part
// (owned by the editor, reachable via App_context) so that both the object
// Selection (which defers to it when a component mode is active) and any
// future mesh-editing code can read it without depending on the tool.
class Mesh_component_selection
{
public:
    [[nodiscard]] auto get_mode() const -> Mesh_component_mode;
    void               set_mode(Mesh_component_mode mode);

    // Active mesh + primitive index. set_active_mesh() clears all component
    // sets when the target changes and returns true in that case.
    [[nodiscard]] auto get_active_mesh           () const -> std::shared_ptr<erhe::scene::Mesh>;
    [[nodiscard]] auto get_active_primitive_index() const -> std::size_t;
    auto               set_active_mesh(const std::shared_ptr<erhe::scene::Mesh>& mesh, std::size_t primitive_index) -> bool;

    // Associates the stored component indices with a specific Geometry object
    // and invalidates (clears) them when that object changes. Geometry edits
    // (Catmull-Clark, Conway, ...) and undo replace a mesh's Geometry with a
    // freshly allocated one via Mesh::set_primitives(), so the stored facet /
    // vertex / edge indices no longer address the live mesh; comparing the
    // Geometry pointer detects that and drops the stale selection before it is
    // used to index the new (possibly smaller) mesh. Call once per frame with
    // the active mesh's current geometry, and on each pick.
    void               set_active_geometry(const std::shared_ptr<erhe::geometry::Geometry>& geometry);

    void               clear   ();
    [[nodiscard]] auto is_empty() const -> bool;

    // Vertices
    void               add_vertex     (GEO::index_t vertex);
    void               remove_vertex  (GEO::index_t vertex);
    void               toggle_vertex  (GEO::index_t vertex);
    [[nodiscard]] auto contains_vertex(GEO::index_t vertex) const -> bool;
    [[nodiscard]] auto get_vertices   () const -> const std::set<GEO::index_t>&;

    // Facets
    void               add_facet     (GEO::index_t facet);
    void               remove_facet  (GEO::index_t facet);
    void               toggle_facet  (GEO::index_t facet);
    [[nodiscard]] auto contains_facet(GEO::index_t facet) const -> bool;
    [[nodiscard]] auto get_facets    () const -> const std::set<GEO::index_t>&;

    // Edges (stored as canonical vertex pairs)
    [[nodiscard]] static auto make_edge_key(GEO::index_t a, GEO::index_t b) -> Mesh_edge_key;
    void               add_edge     (GEO::index_t a, GEO::index_t b);
    void               remove_edge  (GEO::index_t a, GEO::index_t b);
    void               toggle_edge  (GEO::index_t a, GEO::index_t b);
    [[nodiscard]] auto contains_edge(GEO::index_t a, GEO::index_t b) const -> bool;
    [[nodiscard]] auto get_edges    () const -> const std::set<Mesh_edge_key>&;

private:
    Mesh_component_mode                     m_mode                  {Mesh_component_mode::object};
    std::weak_ptr<erhe::scene::Mesh>        m_active_mesh           {};
    std::weak_ptr<erhe::geometry::Geometry> m_active_geometry       {};
    std::size_t                             m_active_primitive_index{std::numeric_limits<std::size_t>::max()};
    std::set<GEO::index_t>                  m_vertices              {};
    std::set<GEO::index_t>                  m_facets                {};
    std::set<Mesh_edge_key>                 m_edges                 {};
};

} // namespace editor
