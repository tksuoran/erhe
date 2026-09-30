#pragma once

#include <geogram/basic/numeric.h>

#include <span>
#include <vector>

// Topology walkers over a Geometry: edge loop, edge ring, face loop,
// boundary loop and connected region. See doc/erhe/geometry.md "Topology
// walkers" and doc/plans/mesh_modeling.md section 4.1 for the rules.
//
// Every walker requires a Geometry whose process() ran with
// process_flag_connect | process_flag_build_edges (has_edge_connectivity()),
// and fills a caller-owned buffer (cleared, capacity kept) in end-to-end
// order. A closed loop / ring holds each element once, starting at the seed.

namespace erhe::geometry {

class Geometry;

// Where an edge loop stops besides valence and visited edges.
enum class Edge_loop_delimit : unsigned int
{
    none          = 0u,
    crease        = (1u << 0u), // a crease edge continues only onto crease edges; a plain edge stops at a vertex with a crease edge
    outer_corners = (1u << 1u)  // a boundary loop stops at a convex corner (valence 2)
};

// Which edges a connected region refuses to cross.
// A material delimit (an edge between facets of different materials) is part
// of doc/plans/mesh_modeling.md section 4.1, but Geometry carries no facet
// material, so it is not offered here.
enum class Region_delimit : unsigned int
{
    none    = 0u,
    crease  = (1u << 0u), // an edge with edge_sharpness > 0
    winding = (1u << 1u)  // an edge whose two facets traverse it in the same direction
};

[[nodiscard]] constexpr auto operator|(const Edge_loop_delimit a, const Edge_loop_delimit b) -> Edge_loop_delimit
{
    return static_cast<Edge_loop_delimit>(static_cast<unsigned int>(a) | static_cast<unsigned int>(b));
}
[[nodiscard]] constexpr auto has_flag(const Edge_loop_delimit set, const Edge_loop_delimit flag) -> bool
{
    return (static_cast<unsigned int>(set) & static_cast<unsigned int>(flag)) != 0u;
}
[[nodiscard]] constexpr auto operator|(const Region_delimit a, const Region_delimit b) -> Region_delimit
{
    return static_cast<Region_delimit>(static_cast<unsigned int>(a) | static_cast<unsigned int>(b));
}
[[nodiscard]] constexpr auto has_flag(const Region_delimit set, const Region_delimit flag) -> bool
{
    return (static_cast<unsigned int>(set) & static_cast<unsigned int>(flag)) != 0u;
}

// Whether a walk came back to its seed (closed) or ended at both ends (open).
enum class Walk_shape : unsigned int
{
    open   = 0u,
    closed = 1u
};

// Edge loop through seed_edge. Output: edge indices.
auto walk_edge_loop(
    const Geometry&            geometry,
    GEO::index_t               seed_edge,
    Edge_loop_delimit          delimit,
    std::vector<GEO::index_t>& out_edges
) -> Walk_shape;

// Edge ring through seed_edge, across quads. Output: edge indices.
auto walk_edge_ring(
    const Geometry&            geometry,
    GEO::index_t               seed_edge,
    std::vector<GEO::index_t>& out_edges
) -> Walk_shape;

// Face loop across seed_edge, through quads. Output: facet indices; a facet
// the loop crosses twice (self-crossing loop) is listed once.
auto walk_face_loop(
    const Geometry&            geometry,
    GEO::index_t               seed_edge,
    std::vector<GEO::index_t>& out_facets
) -> Walk_shape;

// Boundary edges connected to seed_edge through their vertices. Empty when
// seed_edge is not a boundary edge. Output: edge indices.
void walk_boundary_loop(
    const Geometry&            geometry,
    GEO::index_t               seed_edge,
    std::vector<GEO::index_t>& out_edges
);

// Vertices reachable from seed_vertices across edges, in flood order, seeds
// first. Output: vertex indices.
void walk_connected_region(
    const Geometry&                geometry,
    std::span<const GEO::index_t>  seed_vertices,
    Region_delimit                 delimit,
    std::vector<GEO::index_t>&     out_vertices
);

}
