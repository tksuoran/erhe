#include "erhe_geometry/topology.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>

#include <cstdint>
#include <optional>

namespace erhe::geometry {

namespace {

// Per-thread scratch shared by the walkers. Each walker is a leaf (no walker
// calls another), so one buffer set per thread is never live across a nested
// use. Cleared / re-assigned per call with capacity kept.
class Walk_scratch
{
public:
    std::vector<std::uint8_t> marks;    // per edge / facet / vertex visited bits
    std::vector<GEO::index_t> backward; // the second direction of a two-way walk
};

auto get_walk_scratch() -> Walk_scratch&
{
    static thread_local Walk_scratch scratch;
    return scratch;
}

void verify_walkable(const Geometry& geometry)
{
    ERHE_VERIFY(geometry.has_connectivity());
    ERHE_VERIFY(geometry.has_edge_connectivity());
}

auto other_vertex(const GEO::Mesh& mesh, const GEO::index_t edge, const GEO::index_t vertex) -> GEO::index_t
{
    const GEO::index_t v0 = mesh.edges.vertex(edge, 0);
    const GEO::index_t v1 = mesh.edges.vertex(edge, 1);
    return (v0 == vertex) ? v1 : v0;
}

auto get_facet_count(const Geometry& geometry, const GEO::index_t edge) -> std::size_t
{
    return geometry.get_edge_facets(edge).size();
}

auto is_crease(const Geometry& geometry, const GEO::index_t edge) -> bool
{
    const std::optional<float> sharpness = geometry.get_attributes().edge_sharpness.try_get(edge);
    return sharpness.has_value() && (sharpness.value() > 0.0f);
}

// The facet across a manifold edge from `facet`; NO_INDEX when the edge is
// not manifold.
auto get_other_facet(const Geometry& geometry, const GEO::index_t edge, const GEO::index_t facet) -> GEO::index_t
{
    const std::span<const GEO::index_t> facets = geometry.get_edge_facets(edge);
    if (facets.size() != 2) {
        return GEO::NO_INDEX;
    }
    return (facets[0] == facet) ? facets[1] : facets[0];
}

// The other edge of `facet` at `vertex`, given one edge of `facet` at
// `vertex`; NO_EDGE when `edge` is not an edge of `facet` at `vertex`.
auto get_other_edge_at_vertex(
    const Geometry&    geometry,
    const GEO::index_t facet,
    const GEO::index_t vertex,
    const GEO::index_t edge
) -> GEO::index_t
{
    const GEO::Mesh&   mesh         = geometry.get_mesh();
    const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
    for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
        const GEO::index_t corner = mesh.facets.corner(facet, local_corner);
        if (mesh.facet_corners.vertex(corner) != vertex) {
            continue;
        }
        const GEO::index_t prev_corner   = mesh.facets.corner(facet, (local_corner + corner_count - 1) % corner_count);
        const GEO::index_t outgoing_edge = geometry.get_corner_edge(corner);      // vertex -> next
        const GEO::index_t incoming_edge = geometry.get_corner_edge(prev_corner); // prev -> vertex
        if (edge == outgoing_edge) {
            return incoming_edge;
        }
        if (edge == incoming_edge) {
            return outgoing_edge;
        }
    }
    return GEO::NO_EDGE;
}

// Local corner of `facet` whose facet edge is `edge`; NO_INDEX if none.
auto get_local_corner_of_edge(const Geometry& geometry, const GEO::index_t facet, const GEO::index_t edge) -> GEO::index_t
{
    const GEO::Mesh&   mesh         = geometry.get_mesh();
    const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
    for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
        if (geometry.get_corner_edge(mesh.facets.corner(facet, local_corner)) == edge) {
            return local_corner;
        }
    }
    return GEO::NO_INDEX;
}

// The edge opposite `edge` in quad `facet`; NO_EDGE when `facet` is not a
// quad or does not contain `edge`.
auto get_opposite_edge_in_quad(const Geometry& geometry, const GEO::index_t facet, const GEO::index_t edge) -> GEO::index_t
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    if (mesh.facets.nb_corners(facet) != 4) {
        return GEO::NO_EDGE;
    }
    const GEO::index_t local_corner = get_local_corner_of_edge(geometry, facet, edge);
    if (local_corner == GEO::NO_INDEX) {
        return GEO::NO_EDGE;
    }
    return geometry.get_corner_edge(mesh.facets.corner(facet, (local_corner + 2) % 4));
}

enum class Loop_class : unsigned int
{
    boundary,     // one facet
    interior,     // two facets
    non_manifold  // three or more facets
};

auto get_loop_class(const std::size_t facet_count) -> Loop_class
{
    if (facet_count == 1) {
        return Loop_class::boundary;
    }
    if (facet_count == 2) {
        return Loop_class::interior;
    }
    return Loop_class::non_manifold;
}

auto next_interior_loop_edge(
    const Geometry&         geometry,
    const GEO::index_t      edge,
    const GEO::index_t      vertex,
    const Edge_loop_delimit delimit
) -> GEO::index_t
{
    const std::span<const GEO::index_t> vertex_edges = geometry.get_vertex_edges(vertex);
    const std::size_t valence = vertex_edges.size();
    if ((valence != 4) && (valence != 2)) {
        return GEO::NO_EDGE;
    }
    const bool crease_delimit = has_flag(delimit, Edge_loop_delimit::crease);
    const bool edge_is_crease = crease_delimit && is_crease(geometry, edge);
    if (crease_delimit && !edge_is_crease) {
        for (const GEO::index_t vertex_edge : vertex_edges) {
            if (is_crease(geometry, vertex_edge)) {
                return GEO::NO_EDGE;
            }
        }
    }

    // Rotate around the vertex through valence / 2 facets.
    GEO::index_t       facet         = geometry.get_edge_facets(edge)[0];
    GEO::index_t       current_edge  = edge;
    const std::size_t  facet_steps   = valence / 2;
    GEO::index_t       result        = GEO::NO_EDGE;
    for (std::size_t step = 0; step < facet_steps; ++step) {
        const GEO::index_t other_edge = get_other_edge_at_vertex(geometry, facet, vertex, current_edge);
        if (other_edge == GEO::NO_EDGE) {
            return GEO::NO_EDGE;
        }
        if (step + 1 == facet_steps) {
            result = other_edge;
            break;
        }
        const GEO::index_t next_facet = get_other_facet(geometry, other_edge, facet);
        if (next_facet == GEO::NO_INDEX) {
            return GEO::NO_EDGE; // crossed edge is not manifold
        }
        facet        = next_facet;
        current_edge = other_edge;
    }
    if ((result == GEO::NO_EDGE) || (result == edge)) {
        return GEO::NO_EDGE;
    }
    if (edge_is_crease && !is_crease(geometry, result)) {
        return GEO::NO_EDGE;
    }
    return result;
}

auto next_boundary_loop_edge(
    const Geometry&         geometry,
    const GEO::index_t      edge,
    const GEO::index_t      vertex,
    const Edge_loop_delimit delimit
) -> GEO::index_t
{
    const std::size_t valence = geometry.get_vertex_edges(vertex).size();
    if ((valence == 2) && has_flag(delimit, Edge_loop_delimit::outer_corners)) {
        return GEO::NO_EDGE;
    }
    GEO::index_t facet        = geometry.get_edge_facets(edge)[0];
    GEO::index_t current_edge = edge;
    for (std::size_t step = 0; step < valence; ++step) {
        const GEO::index_t other_edge = get_other_edge_at_vertex(geometry, facet, vertex, current_edge);
        if ((other_edge == GEO::NO_EDGE) || (other_edge == edge)) {
            return GEO::NO_EDGE;
        }
        const std::size_t facet_count = get_facet_count(geometry, other_edge);
        if (facet_count == 1) {
            return other_edge;
        }
        if (facet_count != 2) {
            return GEO::NO_EDGE; // non-manifold edge
        }
        facet        = get_other_facet(geometry, other_edge, facet);
        current_edge = other_edge;
    }
    return GEO::NO_EDGE;
}

auto next_non_manifold_loop_edge(
    const Geometry&    geometry,
    const GEO::index_t edge,
    const GEO::index_t vertex
) -> GEO::index_t
{
    const std::span<const GEO::index_t> edge_facets = geometry.get_edge_facets(edge);
    const std::size_t facet_count = edge_facets.size();
    const std::size_t valence     = geometry.get_vertex_edges(vertex).size();
    GEO::index_t candidate = GEO::NO_EDGE;
    for (const GEO::index_t start_facet : edge_facets) {
        GEO::index_t facet        = start_facet;
        GEO::index_t current_edge = edge;
        for (std::size_t step = 0; step < valence; ++step) {
            const GEO::index_t other_edge = get_other_edge_at_vertex(geometry, facet, vertex, current_edge);
            if ((other_edge == GEO::NO_EDGE) || (other_edge == edge)) {
                break;
            }
            const std::size_t other_count = get_facet_count(geometry, other_edge);
            if (other_count == 2) {
                facet        = get_other_facet(geometry, other_edge, facet);
                current_edge = other_edge;
                continue;
            }
            if (other_count == facet_count) {
                if ((candidate != GEO::NO_EDGE) && (candidate != other_edge)) {
                    return GEO::NO_EDGE; // junction: two candidates
                }
                candidate = other_edge;
            }
            break;
        }
    }
    return candidate;
}

auto next_loop_edge(
    const Geometry&         geometry,
    const Loop_class        loop_class,
    const GEO::index_t      edge,
    const GEO::index_t      vertex,
    const Edge_loop_delimit delimit
) -> GEO::index_t
{
    switch (loop_class) {
        case Loop_class::interior:     return next_interior_loop_edge    (geometry, edge, vertex, delimit);
        case Loop_class::boundary:     return next_boundary_loop_edge    (geometry, edge, vertex, delimit);
        case Loop_class::non_manifold: return next_non_manifold_loop_edge(geometry, edge, vertex);
    }
    return GEO::NO_EDGE;
}

// Hub of an interior seed edge: when either end vertex has valence 3 and
// three facets, the largest facet of the seed edge, provided it has more
// than four corners (a quad is no hub). NO_INDEX when there is no hub.
auto find_loop_hub_facet(const Geometry& geometry, const GEO::index_t seed_edge) -> GEO::index_t
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    bool has_hub_vertex = false;
    for (GEO::index_t end = 0; end < 2; ++end) {
        const GEO::index_t vertex = mesh.edges.vertex(seed_edge, end);
        if ((geometry.get_vertex_edges(vertex).size() == 3) && (geometry.get_vertex_corners(vertex).size() == 3)) {
            has_hub_vertex = true;
        }
    }
    if (!has_hub_vertex) {
        return GEO::NO_INDEX;
    }
    GEO::index_t largest_facet        = GEO::NO_INDEX;
    GEO::index_t largest_corner_count = 0;
    for (const GEO::index_t facet : geometry.get_edge_facets(seed_edge)) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        if (corner_count > largest_corner_count) {
            largest_facet        = facet;
            largest_corner_count = corner_count;
        }
    }
    return (largest_corner_count > 4) ? largest_facet : GEO::NO_INDEX;
}

// Loop step around a hub facet: at `vertex` of valence 3, the edge to the
// neighbour of `vertex` in the hub facet that is not the previous vertex.
// The edge must exist and not be a boundary edge.
auto next_hub_loop_edge(
    const Geometry&    geometry,
    const GEO::index_t hub_facet,
    const GEO::index_t edge,
    const GEO::index_t vertex
) -> GEO::index_t
{
    if (geometry.get_vertex_edges(vertex).size() != 3) {
        return GEO::NO_EDGE;
    }
    const GEO::Mesh&   mesh            = geometry.get_mesh();
    const GEO::index_t previous_vertex = other_vertex(mesh, edge, vertex);
    const GEO::index_t corner_count    = mesh.facets.nb_corners(hub_facet);
    for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
        if (mesh.facets.vertex(hub_facet, local_corner) != vertex) {
            continue;
        }
        const GEO::index_t prev_neighbor = mesh.facets.vertex(hub_facet, (local_corner + corner_count - 1) % corner_count);
        const GEO::index_t next_neighbor = mesh.facets.vertex(hub_facet, (local_corner + 1) % corner_count);
        const GEO::index_t neighbor      = (prev_neighbor == previous_vertex) ? next_neighbor : prev_neighbor;
        if ((neighbor == previous_vertex) || (neighbor == vertex)) {
            return GEO::NO_EDGE;
        }
        const GEO::index_t next = geometry.get_edge(vertex, neighbor);
        if ((next == GEO::NO_EDGE) || (get_facet_count(geometry, next) < 2)) {
            return GEO::NO_EDGE;
        }
        return next;
    }
    return GEO::NO_EDGE;
}

// Prepends `backward` reversed to `out`, giving end-to-end order.
void prepend_reversed(std::vector<GEO::index_t>& out, const std::vector<GEO::index_t>& backward)
{
    if (backward.empty()) {
        return;
    }
    out.insert(out.begin(), backward.rbegin(), backward.rend());
}

// Walks an edge ring from `edge` into `facet`, appending to `out`. Returns
// closed when the walk came back to `seed_edge`.
auto walk_ring_direction(
    const Geometry&            geometry,
    const GEO::index_t         seed_edge,
    GEO::index_t               edge,
    GEO::index_t               facet,
    std::vector<std::uint8_t>& marks,
    std::vector<GEO::index_t>& out
) -> Walk_shape
{
    for (;;) {
        const GEO::index_t opposite = get_opposite_edge_in_quad(geometry, facet, edge);
        if (opposite == GEO::NO_EDGE) {
            return Walk_shape::open;
        }
        if (marks[opposite] != 0) {
            return (opposite == seed_edge) ? Walk_shape::closed : Walk_shape::open;
        }
        const std::size_t facet_count = get_facet_count(geometry, opposite);
        if ((facet_count < 1) || (facet_count > 2)) {
            return Walk_shape::open;
        }
        marks[opposite] = 1;
        out.push_back(opposite);
        if (facet_count == 1) {
            return Walk_shape::open;
        }
        facet = get_other_facet(geometry, opposite, facet);
        edge  = opposite;
    }
}

constexpr std::uint8_t face_loop_emitted = 0x4u;

// Walks a face loop entering `facet` through `edge`, appending facets to
// `out`. A facet is visited once per crossing direction (the parity of the
// local corner of the entry edge) and emitted once.
auto walk_face_loop_direction(
    const Geometry&            geometry,
    GEO::index_t               edge,
    GEO::index_t               facet,
    std::vector<std::uint8_t>& marks,
    std::vector<GEO::index_t>& out
) -> Walk_shape
{
    const GEO::Mesh&   mesh        = geometry.get_mesh();
    const GEO::index_t start_facet = facet;
    std::uint8_t       start_bit   = 0;
    for (;;) {
        if (mesh.facets.nb_corners(facet) != 4) {
            return Walk_shape::open;
        }
        const GEO::index_t local_corner = get_local_corner_of_edge(geometry, facet, edge);
        if (local_corner == GEO::NO_INDEX) {
            return Walk_shape::open;
        }
        const std::uint8_t direction_bit = static_cast<std::uint8_t>(1u << (local_corner & 1u));
        if ((marks[facet] & direction_bit) != 0) {
            return ((facet == start_facet) && (direction_bit == start_bit)) ? Walk_shape::closed : Walk_shape::open;
        }
        if (start_bit == 0) {
            start_bit = direction_bit;
        }
        marks[facet] = static_cast<std::uint8_t>(marks[facet] | direction_bit);
        if ((marks[facet] & face_loop_emitted) == 0) {
            marks[facet] = static_cast<std::uint8_t>(marks[facet] | face_loop_emitted);
            out.push_back(facet);
        }
        const GEO::index_t opposite = geometry.get_corner_edge(mesh.facets.corner(facet, (local_corner + 2) % 4));
        const GEO::index_t next_facet = get_other_facet(geometry, opposite, facet);
        if (next_facet == GEO::NO_INDEX) {
            return Walk_shape::open;
        }
        edge  = opposite;
        facet = next_facet;
    }
}

// Appends to `out` the unmarked boundary edges walked from `vertex` onwards,
// taking the first unmarked boundary edge at each vertex.
void walk_boundary_direction(
    const Geometry&            geometry,
    GEO::index_t               vertex,
    std::vector<std::uint8_t>& marks,
    std::vector<GEO::index_t>& out
)
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    for (;;) {
        GEO::index_t next = GEO::NO_EDGE;
        for (const GEO::index_t vertex_edge : geometry.get_vertex_edges(vertex)) {
            if ((marks[vertex_edge] == 0) && (get_facet_count(geometry, vertex_edge) == 1)) {
                next = vertex_edge;
                break;
            }
        }
        if (next == GEO::NO_EDGE) {
            return;
        }
        marks[next] = 1;
        out.push_back(next);
        vertex = other_vertex(mesh, next, vertex);
    }
}

auto facets_disagree_in_winding(const Geometry& geometry, const GEO::index_t edge) -> bool
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    const std::span<const GEO::index_t> facets = geometry.get_edge_facets(edge);
    if (facets.size() < 2) {
        return false;
    }
    // Each facet traverses the edge from the vertex of the corner the edge
    // starts at; two facets starting it at the same vertex disagree.
    GEO::index_t first_start = GEO::NO_INDEX;
    for (const GEO::index_t facet : facets) {
        const GEO::index_t local_corner = get_local_corner_of_edge(geometry, facet, edge);
        if (local_corner == GEO::NO_INDEX) {
            continue;
        }
        const GEO::index_t start = mesh.facet_corners.vertex(mesh.facets.corner(facet, local_corner));
        if (first_start == GEO::NO_INDEX) {
            first_start = start;
        } else if (start == first_start) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

auto walk_edge_loop(
    const Geometry&            geometry,
    const GEO::index_t         seed_edge,
    const Edge_loop_delimit    delimit,
    std::vector<GEO::index_t>& out_edges
) -> Walk_shape
{
    verify_walkable(geometry);
    const GEO::Mesh& mesh = geometry.get_mesh();
    ERHE_VERIFY(seed_edge < mesh.edges.nb());

    Walk_scratch& scratch = get_walk_scratch();
    scratch.marks.assign(mesh.edges.nb(), 0);
    scratch.backward.clear();
    out_edges.clear();

    const std::size_t seed_facet_count = get_facet_count(geometry, seed_edge);
    out_edges.push_back(seed_edge);
    scratch.marks[seed_edge] = 1;
    if (seed_facet_count == 0) {
        return Walk_shape::open;
    }
    const Loop_class loop_class = get_loop_class(seed_facet_count);

    const GEO::index_t hub_facet = (loop_class == Loop_class::interior) ? find_loop_hub_facet(geometry, seed_edge) : GEO::NO_INDEX;

    // One direction: from the seed through `vertex`, into `out`.
    const auto walk_direction = [&](GEO::index_t vertex, std::vector<GEO::index_t>& out) -> Walk_shape {
        GEO::index_t edge = seed_edge;
        for (;;) {
            const GEO::index_t next = (hub_facet != GEO::NO_INDEX)
                ? next_hub_loop_edge(geometry, hub_facet, edge, vertex)
                : next_loop_edge    (geometry, loop_class, edge, vertex, delimit);
            if (next == GEO::NO_EDGE) {
                return Walk_shape::open;
            }
            if (scratch.marks[next] != 0) {
                return (next == seed_edge) ? Walk_shape::closed : Walk_shape::open;
            }
            scratch.marks[next] = 1;
            out.push_back(next);
            vertex = other_vertex(mesh, next, vertex);
            edge   = next;
        }
    };

    if (walk_direction(mesh.edges.vertex(seed_edge, 1), out_edges) == Walk_shape::closed) {
        return Walk_shape::closed;
    }
    walk_direction(mesh.edges.vertex(seed_edge, 0), scratch.backward);
    prepend_reversed(out_edges, scratch.backward);
    return Walk_shape::open;
}

auto walk_edge_ring(
    const Geometry&            geometry,
    const GEO::index_t         seed_edge,
    std::vector<GEO::index_t>& out_edges
) -> Walk_shape
{
    verify_walkable(geometry);
    const GEO::Mesh& mesh = geometry.get_mesh();
    ERHE_VERIFY(seed_edge < mesh.edges.nb());

    Walk_scratch& scratch = get_walk_scratch();
    scratch.marks.assign(mesh.edges.nb(), 0);
    scratch.backward.clear();
    out_edges.clear();

    out_edges.push_back(seed_edge);
    scratch.marks[seed_edge] = 1;
    const std::span<const GEO::index_t> seed_facets = geometry.get_edge_facets(seed_edge);
    if ((seed_facets.size() < 1) || (seed_facets.size() > 2)) {
        return Walk_shape::open;
    }
    if (walk_ring_direction(geometry, seed_edge, seed_edge, seed_facets[0], scratch.marks, out_edges) == Walk_shape::closed) {
        return Walk_shape::closed;
    }
    if (seed_facets.size() == 2) {
        walk_ring_direction(geometry, seed_edge, seed_edge, seed_facets[1], scratch.marks, scratch.backward);
        prepend_reversed(out_edges, scratch.backward);
    }
    return Walk_shape::open;
}

auto walk_face_loop(
    const Geometry&            geometry,
    const GEO::index_t         seed_edge,
    std::vector<GEO::index_t>& out_facets
) -> Walk_shape
{
    verify_walkable(geometry);
    const GEO::Mesh& mesh = geometry.get_mesh();
    ERHE_VERIFY(seed_edge < mesh.edges.nb());

    Walk_scratch& scratch = get_walk_scratch();
    scratch.marks.assign(mesh.facets.nb(), 0);
    scratch.backward.clear();
    out_facets.clear();

    const std::span<const GEO::index_t> seed_facets = geometry.get_edge_facets(seed_edge);
    if ((seed_facets.size() < 1) || (seed_facets.size() > 2)) {
        return Walk_shape::open;
    }
    if (walk_face_loop_direction(geometry, seed_edge, seed_facets[0], scratch.marks, out_facets) == Walk_shape::closed) {
        return Walk_shape::closed;
    }
    if (seed_facets.size() == 2) {
        walk_face_loop_direction(geometry, seed_edge, seed_facets[1], scratch.marks, scratch.backward);
        prepend_reversed(out_facets, scratch.backward);
    }
    return Walk_shape::open;
}

void walk_boundary_loop(
    const Geometry&            geometry,
    const GEO::index_t         seed_edge,
    std::vector<GEO::index_t>& out_edges
)
{
    verify_walkable(geometry);
    const GEO::Mesh& mesh = geometry.get_mesh();
    ERHE_VERIFY(seed_edge < mesh.edges.nb());

    out_edges.clear();
    if (get_facet_count(geometry, seed_edge) != 1) {
        return;
    }

    Walk_scratch& scratch = get_walk_scratch();
    scratch.marks.assign(mesh.edges.nb(), 0);
    scratch.backward.clear();

    out_edges.push_back(seed_edge);
    scratch.marks[seed_edge] = 1;
    walk_boundary_direction(geometry, mesh.edges.vertex(seed_edge, 1), scratch.marks, out_edges);
    walk_boundary_direction(geometry, mesh.edges.vertex(seed_edge, 0), scratch.marks, scratch.backward);
    prepend_reversed(out_edges, scratch.backward);

    // Flood the remaining boundary edges reachable through the vertices
    // (branches at vertices with more than two boundary edges).
    for (std::size_t i = 0; i < out_edges.size(); ++i) {
        const GEO::index_t edge = out_edges[i];
        for (GEO::index_t end = 0; end < 2; ++end) {
            for (const GEO::index_t vertex_edge : geometry.get_vertex_edges(mesh.edges.vertex(edge, end))) {
                if ((scratch.marks[vertex_edge] == 0) && (get_facet_count(geometry, vertex_edge) == 1)) {
                    scratch.marks[vertex_edge] = 1;
                    out_edges.push_back(vertex_edge);
                }
            }
        }
    }
}

void walk_connected_region(
    const Geometry&                     geometry,
    const std::span<const GEO::index_t> seed_vertices,
    const Region_delimit                delimit,
    std::vector<GEO::index_t>&          out_vertices
)
{
    verify_walkable(geometry);
    const GEO::Mesh& mesh = geometry.get_mesh();

    Walk_scratch& scratch = get_walk_scratch();
    scratch.marks.assign(mesh.vertices.nb(), 0);
    out_vertices.clear();

    for (const GEO::index_t seed_vertex : seed_vertices) {
        ERHE_VERIFY(seed_vertex < mesh.vertices.nb());
        if (scratch.marks[seed_vertex] == 0) {
            scratch.marks[seed_vertex] = 1;
            out_vertices.push_back(seed_vertex);
        }
    }

    const bool crease_delimit  = has_flag(delimit, Region_delimit::crease);
    const bool winding_delimit = has_flag(delimit, Region_delimit::winding);
    for (std::size_t i = 0; i < out_vertices.size(); ++i) {
        const GEO::index_t vertex = out_vertices[i];
        for (const GEO::index_t edge : geometry.get_vertex_edges(vertex)) {
            if (crease_delimit && is_crease(geometry, edge)) {
                continue;
            }
            if (winding_delimit && facets_disagree_in_winding(geometry, edge)) {
                continue;
            }
            const GEO::index_t neighbor = other_vertex(mesh, edge, vertex);
            if (scratch.marks[neighbor] == 0) {
                scratch.marks[neighbor] = 1;
                out_vertices.push_back(neighbor);
            }
        }
    }
}

}
