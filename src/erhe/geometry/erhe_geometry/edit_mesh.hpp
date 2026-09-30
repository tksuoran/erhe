#pragma once

#include <geogram/basic/geometry.h>
#include <geogram/basic/numeric.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

// Edit_mesh: a mutable polygon mesh scratch on which the modeling operations
// of doc/plans/mesh_modeling.md compose local edits (split this edge, join
// these facets, collapse this vertex, ...). See doc/erhe/geometry.md
// "Edit_mesh" for the model and the emission rule, and
// erhe_geometry/operation/edit_mesh_operation.hpp for the emission.
//
// Every element carries provenance into the Geometry it was loaded from:
// a vertex as weighted source vertices, a corner as weighted source corners,
// a facet as one source facet (or GEO::NO_INDEX). Deleted elements are
// tombstoned, so vertex, edge and facet handles stay valid until emission
// compacts them. Every primitive keeps the adjacency (vertex facets, vertex
// edges, edge facets) current.

namespace erhe::geometry {

class Geometry;

// (weight, source index); the same layout as Source_table entries.
using Edit_source = std::pair<float, GEO::index_t>;

class Edit_vertex
{
public:
    GEO::vec3f                position{0.0f, 0.0f, 0.0f};
    std::vector<Edit_source>  sources;  // weighted source Geometry vertices
    std::vector<GEO::index_t> facets;   // facets using this vertex
    std::vector<GEO::index_t> edges;    // edges at this vertex (including wire edges)
    bool                      deleted{false};
};

class Edit_corner
{
public:
    GEO::index_t             vertex{GEO::NO_INDEX};
    std::vector<Edit_source> sources; // weighted source Geometry corners
};

class Edit_facet
{
public:
    std::vector<Edit_corner> corners;
    GEO::index_t             source_facet{GEO::NO_INDEX};
    bool                     deleted{false};
};

class Edit_edge
{
public:
    std::array<GEO::index_t, 2> vertices{GEO::NO_INDEX, GEO::NO_INDEX}; // canonical: vertices[0] < vertices[1]
    std::vector<GEO::index_t>   facets; // empty for a wire edge
    std::optional<float>        sharpness;
    bool                        deleted{false};
};

// The five delete contexts of doc/plans/mesh_modeling.md section 4.3. The
// element indices passed to Edit_mesh::delete_elements() are vertices for
// `vertices`, edges for `edges` and `only_edges_and_faces`, facets for
// `faces` and `only_faces`.
enum class Delete_context : unsigned int
{
    vertices,             // the vertices, their edges and their facets
    edges,                // the edges, their facets, and endpoints left without edges
    faces,                // the facets, then their edges and vertices no surviving facet uses
    only_edges_and_faces, // the edges and their facets; vertices stay
    only_faces            // the facets; edges and vertices stay
};

enum class Join_result : unsigned int
{
    joined,
    empty_region,
    not_edge_connected, // the region's facets do not form one edge-connected group
    non_manifold,       // an edge inside the region has more than two facets
    invalid_boundary    // the region's boundary is not a single manifold loop (or winding flips inside)
};

enum class Collapse_result : unsigned int
{
    collapsed,
    not_two_valent
};

enum class Edgenet_result : unsigned int
{
    split,        // the facet was replaced by two or more facets
    unchanged,    // no edge survived (all dangling), the facet is as it was
    invalid_edges // an edge is degenerate, names a deleted vertex, or the net yields an invalid facet
};

// Mean value coordinates of p with respect to a planar polygon (positions in
// order, unit normal), signed through the normal so they reproduce linear
// functions on the polygon's plane. p on a vertex takes that vertex, p on an
// edge the edge's linear weights. out_weights receives one weight per polygon
// vertex, summing to 1.
void compute_mean_value_weights(
    std::span<const GEO::vec3f> polygon,
    const GEO::vec3f&           normal,
    const GEO::vec3f&           p,
    std::vector<float>&         out_weights
);

// The unit Newell normal of a polygon (+Z for a degenerate one).
[[nodiscard]] auto compute_newell_normal(std::span<const GEO::vec3f> polygon) -> GEO::vec3f;

class Edit_mesh
{
public:
    // Requires source.has_edge_connectivity(). Copies positions (provenance
    // weight 1), facets (corner provenance weight 1, source facet) and edge
    // sharpness. Clears the previous contents.
    void load(const Geometry& source);
    void clear();

    // Live element counts.
    [[nodiscard]] auto get_vertex_count() const -> GEO::index_t;
    [[nodiscard]] auto get_edge_count  () const -> GEO::index_t;
    [[nodiscard]] auto get_facet_count () const -> GEO::index_t;

    // Handle ranges (live and tombstoned): valid handles are below these.
    [[nodiscard]] auto get_vertex_slot_count() const -> GEO::index_t;
    [[nodiscard]] auto get_edge_slot_count  () const -> GEO::index_t;
    [[nodiscard]] auto get_facet_slot_count () const -> GEO::index_t;

    [[nodiscard]] auto is_vertex_alive(GEO::index_t vertex) const -> bool;
    [[nodiscard]] auto is_edge_alive  (GEO::index_t edge  ) const -> bool;
    [[nodiscard]] auto is_facet_alive (GEO::index_t facet ) const -> bool;

    [[nodiscard]] auto get_vertex(GEO::index_t vertex) const -> const Edit_vertex&;
    [[nodiscard]] auto get_edge  (GEO::index_t edge  ) const -> const Edit_edge&;
    [[nodiscard]] auto get_facet (GEO::index_t facet ) const -> const Edit_facet&;

    [[nodiscard]] auto get_position(GEO::index_t vertex) const -> GEO::vec3f;
    void               set_position(GEO::index_t vertex, const GEO::vec3f& position);
    // Replaces the vertex provenance (weighted source vertices), e.g. to make
    // a merged vertex interpolate its attributes over the merged cluster.
    void               set_vertex_sources(GEO::index_t vertex, std::span<const Edit_source> sources);

    // Queries
    [[nodiscard]] auto find_edge            (GEO::index_t vertex_a, GEO::index_t vertex_b) const -> GEO::index_t; // GEO::NO_INDEX when absent
    [[nodiscard]] auto get_facet_corners    (GEO::index_t facet) const -> std::span<const Edit_corner>;
    [[nodiscard]] auto get_vertex_facets    (GEO::index_t vertex) const -> std::span<const GEO::index_t>;
    [[nodiscard]] auto get_vertex_edges     (GEO::index_t vertex) const -> std::span<const GEO::index_t>;
    [[nodiscard]] auto get_edge_facet_count (GEO::index_t edge) const -> std::size_t;
    [[nodiscard]] auto get_edge_other_vertex(GEO::index_t edge, GEO::index_t vertex) const -> GEO::index_t;
    [[nodiscard]] auto find_facet_corner    (GEO::index_t facet, GEO::index_t vertex) const -> GEO::index_t; // local corner, GEO::NO_INDEX when absent
    [[nodiscard]] auto are_adjacent_in_facet(GEO::index_t facet, GEO::index_t vertex_a, GEO::index_t vertex_b) const -> bool;
    [[nodiscard]] auto get_edge_sharpness   (GEO::index_t edge) const -> std::optional<float>;
    void               set_edge_sharpness   (GEO::index_t edge, std::optional<float> sharpness);

    // A new loose vertex (no edges, no facets); split_facet_edgenet() and
    // create_facet() connect it.
    auto add_vertex(const GEO::vec3f& position, std::span<const Edit_source> sources) -> GEO::index_t;

    // Inserts a vertex at t along the edge: position and vertex provenance
    // are (1 - t) of edge.vertices[0] plus t of edge.vertices[1]; its corner
    // in each facet of the edge interpolates the facet's two neighbouring
    // corners the same way. Both halves keep the edge's sharpness. Returns
    // the new vertex.
    auto split_edge(GEO::index_t edge, float t) -> GEO::index_t;

    // Splits the facet along a new edge between two of its non-adjacent
    // local corners. The facet keeps the part from corner_a to corner_b,
    // the returned new facet the part from corner_b to corner_a; both keep
    // the source facet, the corners at a and b are duplicated with their
    // provenance. Returns GEO::NO_INDEX (mesh unchanged) when the corners
    // are equal, adjacent or out of range.
    auto split_facet(GEO::index_t facet, GEO::index_t corner_a, GEO::index_t corner_b) -> GEO::index_t;

    // Splits the facet along a net of new edges (vertex pairs) between its
    // vertices and interior vertices already present in the scratch (see
    // add_vertex()): chords from boundary to boundary and chains through
    // interior vertices. A dangling edge no resulting facet uses is dropped.
    // A floating island (a closed cycle of new edges not touching the
    // boundary) is first connected to the boundary through an edge from its
    // vertex nearest to the boundary to that boundary vertex, and through a
    // second non-crossing edge from the next-nearest pair, so the ring
    // between island and boundary becomes facets without a repeated vertex.
    // A corner at an interior vertex takes mean value coordinate weights of
    // the facet's corners. out_facets receives the resulting facets (the
    // original facet first).
    auto split_facet_edgenet(
        GEO::index_t                                           facet,
        std::span<const std::pair<GEO::index_t, GEO::index_t>> edges,
        std::vector<GEO::index_t>&                             out_facets
    ) -> Edgenet_result;

    // Joins an edge-connected region of facets whose boundary is one
    // manifold loop into one facet: interior edges and the vertices left
    // without edges go, each new corner keeps the provenance of the region
    // corner at its vertex, the facet provenance is the first region facet's.
    // Any other result leaves the mesh unchanged. out_facet receives the
    // joined facet (GEO::NO_INDEX on failure).
    auto join_facets    (std::span<const GEO::index_t> region, GEO::index_t& out_facet) -> Join_result;
    auto join_facet_pair(GEO::index_t edge, GEO::index_t& out_facet) -> Join_result;

    // Removes a vertex with exactly two edges, joining them into one edge
    // (sharpness: the larger of the two, and of an existing edge between the
    // two neighbours). Each facet using the vertex loses that corner; a facet
    // left with fewer than three corners is deleted.
    auto collapse_vertex(GEO::index_t vertex) -> Collapse_result;

    // The weld core of doc/plans/mesh_modeling.md section 4.4. merges holds
    // (vertex, survivor) pairs; chains resolve transitively. Survivor
    // positions are unchanged.
    void weld_vertices(std::span<const std::pair<GEO::index_t, GEO::index_t>> merges);

    // Rip: separates the vertex into one vertex per fan of facets, where two
    // facets are in one fan when they share an edge at the vertex that is
    // not listed in edges. The first fan (and every wire edge) keeps the
    // vertex; each other fan gets a copy (position and provenance). An edge
    // listed in edges is duplicated for each fan using it. out_vertices
    // receives the vertex followed by the copies.
    void separate_vertex(GEO::index_t vertex, std::span<const GEO::index_t> edges, std::vector<GEO::index_t>& out_vertices);

    void delete_elements(std::span<const GEO::index_t> elements, Delete_context context);

    // Creates a facet over existing vertices (at least three, distinct, no
    // existing facet with the same vertex set). Facet provenance and corner
    // provenance come from reference_facet (a scratch facet, or
    // GEO::NO_INDEX for none); a vertex not in reference_facet takes its
    // corner provenance from the first facet using it. Returns
    // GEO::NO_INDEX (mesh unchanged) when the vertices are invalid.
    auto create_facet(std::span<const GEO::index_t> vertices, GEO::index_t reference_facet) -> GEO::index_t;

    // Creates a facet from explicit corners (vertex and corner provenance
    // each) with the given source facet (GEO::NO_INDEX for none). The vertex
    // rules of create_facet() apply; GEO::NO_INDEX (mesh unchanged) when the
    // corners are invalid.
    auto create_facet_from_corners(std::span<const Edit_corner> corners, GEO::index_t source_facet) -> GEO::index_t;

    // Moves the facet's corners onto other vertices: vertices[i] becomes the
    // vertex of local corner i (as many as the facet has corners; the corner
    // provenance stays). An edge the facet used before that no facet uses
    // afterwards is removed; a vertex left without facets is kept.
    void set_facet_vertices(GEO::index_t facet, std::span<const GEO::index_t> vertices);

    // Replaces the provenance (weighted source corners) of one corner.
    void set_corner_sources(GEO::index_t facet, GEO::index_t local_corner, std::span<const Edit_source> sources);

private:
    [[nodiscard]] static auto make_edge_key(GEO::index_t vertex_a, GEO::index_t vertex_b) -> std::uint64_t;

    auto ensure_edge (GEO::index_t vertex_a, GEO::index_t vertex_b) -> GEO::index_t;
    void remove_edge (GEO::index_t edge);
    void link_facet  (GEO::index_t facet);
    void unlink_facet(GEO::index_t facet);
    auto make_facet  (std::vector<Edit_corner>&& corners, GEO::index_t source_facet) -> GEO::index_t;
    void kill_facet  (GEO::index_t facet); // unlink + tombstone
    void kill_unlinked_facet(GEO::index_t facet); // tombstone a facet already unlinked
    void kill_vertex (GEO::index_t vertex); // requires no edges and no facets
    [[nodiscard]] auto has_facet_with_vertex_set(std::span<const Edit_corner> corners, GEO::index_t ignore_facet) const -> bool;

    std::vector<Edit_vertex>                        m_vertices;
    std::vector<Edit_facet>                         m_facets;
    std::vector<Edit_edge>                          m_edges;
    std::unordered_map<std::uint64_t, GEO::index_t> m_edge_map;
    GEO::index_t                                    m_live_vertex_count{0};
    GEO::index_t                                    m_live_edge_count  {0};
    GEO::index_t                                    m_live_facet_count {0};
};

} // namespace erhe::geometry
