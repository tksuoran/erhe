#pragma once

#include "erhe_geometry/geometry.hpp"
#include <geogram/mesh/mesh.h>

#include <set>
#include <utility>
#include <vector>
#include <unordered_map>

namespace erhe::geometry::operation {

class pair_hash {
public:
    template <typename T1, typename T2>
    auto operator() (const std::pair<T1, T2>& p) const -> std::size_t {
        std::size_t hash1 = std::hash<T1>{}(p.first);
        std::size_t hash2 = std::hash<T2>{}(p.second);
        return hash1 ^ (hash2 << 1);
    }
};

// A set of mesh sub-components addressed by GEO indices into one Geometry's
// GEO::Mesh: vertex indices, facet indices, and canonical (min, max) edge keys.
// Used to carry a component selection through an operation so it can be remapped
// from the source mesh to the components the operation produces.
class Geometry_component_selection
{
public:
    std::set<GEO::index_t>                          vertices;
    std::set<GEO::index_t>                          facets;
    std::set<std::pair<GEO::index_t, GEO::index_t>> edges; // canonical: first < second

    [[nodiscard]] auto is_empty() const -> bool
    {
        return vertices.empty() && facets.empty() && edges.empty();
    }
};

// In/out pair handed to a selection-aware operation: source carries the components
// selected on the source mesh; destination receives the components they map to on
// the operation's result. Both pointers must be non-null for remapping to happen.
class Component_remap
{
public:
    const Geometry_component_selection* source     {nullptr};
    Geometry_component_selection*       destination{nullptr};
};

// Tracks weighted provenance from source elements to destination elements.
// Each destination element can have multiple source elements with weights.
// Used for attribute interpolation (positions, normals, texcoords, etc.).
class Source_table
{
public:
    void add(GEO::index_t dst_index, float weight, GEO::index_t src_index);
    void resize(std::size_t count);
    [[nodiscard]] auto get(GEO::index_t dst_index) const -> const std::vector<std::pair<float, GEO::index_t>>&;
    [[nodiscard]] auto size() const -> std::size_t;
    [[nodiscard]] auto data() -> std::vector<std::vector<std::pair<float, GEO::index_t>>>&;

private:
    std::vector<std::vector<std::pair<float, GEO::index_t>>> m_entries;
};

class Geometry_operation
{
public:
    Geometry_operation(const erhe::geometry::Geometry& source, erhe::geometry::Geometry& destination)
        : source          {source}
        , destination     {destination}
        , source_mesh     {source.get_mesh()}
        , destination_mesh{destination.get_mesh()}
    {
    }

    Geometry_operation(const erhe::geometry::Geometry& lhs, const erhe::geometry::Geometry& rhs, erhe::geometry::Geometry& destination)
        : source          {lhs}
        , rhs             {&rhs}
        , destination     {destination}
        , source_mesh     {lhs.get_mesh()}
        , rhs_mesh        {&rhs.get_mesh()}
        , destination_mesh{destination.get_mesh()}
    {
    }

    // Map a source-side component selection to the components this operation
    // produced. Must be called after build(), while the operation's mapping tables
    // are still alive. Faces map to their descendant facets (every destination facet
    // whose provenance includes a selected source facet); vertices map to their
    // destination image; edges map to the chain of destination sub-edges along the
    // original edge (endpoint images plus any inserted split midpoints). Components
    // with no destination image are dropped. dst is cleared first.
    void remap_component_selection(const Geometry_component_selection& src, Geometry_component_selection& dst) const;

protected:
    // Default finalization: rebuild connectivity / edges / centroids and
    // regenerate smooth vertex normals and facet texture coordinates.
    static constexpr uint64_t default_post_process_flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;

    // Structural finalization only; callers that preserve source attributes
    // (topology-preserving operations) pass this to keep process() from
    // overwriting normals / texture coordinates.
    static constexpr uint64_t structural_post_process_flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids;

    void post_processing(uint64_t process_flags = default_post_process_flags);

    // Optional selection of source facets the operation should act on. When
    // nullptr, the operation processes the whole mesh (every facet is treated as
    // selected). When set, an operation restricts its topology change to the
    // selected facets and keeps the result connected to the rest of the mesh by
    // pinning the selection-boundary vertices, putting boundary-edge midpoints on
    // the original edges, and re-emitting each unselected boundary facet as an
    // n-gon that splices in those midpoints (no cracks, no T-junctions).
    const std::set<GEO::index_t>* m_selected_facets{nullptr};

    // True when the facet is in the active selection (or when there is no active
    // selection, i.e. whole-mesh operation).
    [[nodiscard]] auto is_facet_selected(GEO::index_t facet) const -> bool;

    // True when the vertex is on the boundary of the selection: incident to both a
    // selected and an unselected facet. Such vertices must be pinned so the
    // modified region stays welded to the unmodified remainder. Always false when
    // there is no active selection.
    [[nodiscard]] auto is_boundary_vertex(GEO::index_t vertex) const -> bool;

    // Re-emit every unselected facet into the destination. A facet not adjacent to
    // the selection is copied 1:1; a facet sharing one or more interface edges (an
    // edge that also borders a selected facet, hence carries operation-inserted
    // midpoints) is rebuilt as an n-gon with those midpoint vertices spliced into
    // its corner loop. Every split slot on an interface edge is spliced in, in the
    // unselected facet's traversal order, so multi-split operations (e.g. gyro, which
    // puts two midpoints on each edge) weld as cleanly as single-split ones. Relies
    // on m_src_edge_to_dst_vertex / get_src_edge_new_vertex so both sides of an
    // interface edge reference the same destination vertices. No-op when there is no
    // active selection.
    void emit_unselected_facets_with_boundary_splice();

    const erhe::geometry::Geometry&  source;
    const erhe::geometry::Geometry*  rhs{nullptr};
    erhe::geometry::Geometry&        destination;

    const GEO::Mesh&                 source_mesh;
    const GEO::Mesh*                 rhs_mesh{nullptr};
    GEO::Mesh&                       destination_mesh;

    std::vector<GEO::index_t>        m_vertex_src_to_dst;
    std::vector<GEO::index_t>        m_facet_src_to_dst;
    std::vector<GEO::index_t>        m_edge_src_to_dst;
    std::vector<GEO::index_t>        m_src_facet_centroid_to_dst_vertex;
    Source_table                      m_dst_vertex_sources;
    Source_table                      m_dst_vertex_corner_sources;
    Source_table                      m_dst_corner_sources;
    Source_table                      m_dst_facet_sources;
    Source_table                      m_dst_edge_sources;

    std::unordered_map<
        std::pair<GEO::index_t, GEO::index_t>,
        std::vector<GEO::index_t>,
        pair_hash
    > m_src_edge_to_dst_vertex;

    void make_dst_vertices_from_src_vertices();
    void make_facet_centroids               ();
    void build_edge_to_facets_map(
        const GEO::Mesh& mesh, 
        std::unordered_map<
            std::pair<GEO::index_t, GEO::index_t>,
            std::vector<GEO::index_t>,
            pair_hash
        >& edge_to_facets
    );

    //// [[nodiscard]] auto get_dst_vertex_from_src_edge(GEO::index_t src_edge) -> GEO::index_t;

    void make_edge_midpoints(std::initializer_list<float> relative_positions);

    // Selection-aware variant of make_edge_midpoints: creates the same split
    // midpoints (registered in m_src_edge_to_dst_vertex in relative_positions order)
    // but only for edges incident to at least one selected facet. With no active
    // selection every edge qualifies, reproducing make_edge_midpoints. Used by
    // selective operations so a midpoint is created exactly where the selected region
    // and its welded boundary need one.
    void make_selected_edge_midpoints(std::initializer_list<float> relative_positions);

    [[nodiscard]] auto get_src_edge_new_vertex(GEO::index_t src_vertex_a, GEO::index_t src_vertex_b, GEO::index_t vertex_split_position) const -> GEO::index_t;

    // Creates a new vertex to Destination from given src_vertex in Source and
    // registers src_vertex as source for the new vertex with specified weight.
    //
    // weight      Weight for old vertex as source.
    // src_vertex  Vertex in source used as source for the new vertex.
    // returns     The new vertex in destination.
    auto make_new_dst_vertex_from_src_vertex(float vertex_weight, GEO::index_t src_vertex) -> GEO::index_t;

    // Creates a new vertex to Destination from given src_vertex in Source and
    // registers src_vertex as source for the new vertex with weight 1.0.
    //
    // src_vertex  Vertex in source used as source for the new vertex.
    // returns     The new vertex in destination.
    auto make_new_dst_vertex_from_src_vertex(GEO::index_t src_vertex) -> GEO::index_t;

    // Creates a new vertex to Destination from centroid of given src_vertex in Source and
    // registers each corner of the src_facet as source for the new vertex with weight 1.0.
    auto make_new_dst_vertex_from_src_facet_centroid(GEO::index_t src_facet) -> GEO::index_t;

    void add_facet_centroid(GEO::index_t dst_vertex, float facet_weight, GEO::index_t src_facet);

    void add_vertex_ring(GEO::index_t dst_vertex, float vertex_weight, GEO::index_t src_vertex);

    auto make_new_dst_facet_from_src_facet(GEO::index_t src_facet, GEO::index_t corner_count) -> GEO::index_t;

    auto make_new_dst_corner_from_src_facet_centroid(GEO::index_t dst_facet, GEO::index_t dst_local_facet_corner, GEO::index_t src_facet) -> GEO::index_t;

    auto make_new_dst_corner_from_dst_vertex(GEO::index_t dst_facet, GEO::index_t dst_local_facet_corner, GEO::index_t dst_point) -> GEO::index_t;

    auto make_new_dst_corner_from_src_corner(GEO::index_t dst_facet, GEO::index_t dst_local_facet_corner, GEO::index_t src_corner) -> GEO::index_t;

    void add_facet_corners(GEO::index_t dst_facet, GEO::index_t src_facet);

    void add_vertex_source(GEO::index_t dst_vertex, float point_weight, GEO::index_t src_vertex);

    void add_vertex_corner_source(GEO::index_t dst_vertex, float corner_weight, GEO::index_t src_corner);

    void add_corner_source(GEO::index_t dst_corner, float corner_weight, GEO::index_t src_corner);

    // Inherit vertex sources to corner
    void distribute_corner_sources(GEO::index_t dst_corner, float point_weight, GEO::index_t dst_vertex);

    void add_facet_source(GEO::index_t dst_facet, float polygon_weight, GEO::index_t src_facet);

    void add_edge_source(GEO::index_t dst_edge, float edge_weight, GEO::index_t src_edge);

    void interpolate_mesh_attributes();

    void copy_mesh_attributes();

protected:
    [[nodiscard]] static auto get_size_to_include(std::size_t old_size, std::size_t i) -> size_t;
};

} // namespace erhe::geometry::operation
